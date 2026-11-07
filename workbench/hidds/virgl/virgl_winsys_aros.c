/* Copyright (C) 2026, The AROS Development Team. All rights reserved.
 * Single-device synchronous proof of a Mesa VirGL winsys for AROS.
 * No DRM descriptor, native sync-fd, blob memory or external resource import.
 */
#include <exec/semaphores.h>
#include <proto/exec.h>
#include <aros/debug.h>


#include <stdint.h>
#include "pipe/p_state.h"
#include "virgl_winsys_aros.h"
#include "virgl_transport.h"
#include "virgl_native.h"

struct avgl_ws;
struct virgl_hw_res {
    struct virgl_hw_res *next;
    struct avgl_ws *ws;
    uint32_t id, size, refs, pending;
    void *map;
};
struct pipe_fence_handle { unsigned refs; };
struct avgl_cb {
    struct virgl_cmd_buf base;
    struct avgl_ws *ws;
    struct virgl_hw_res **resources;
    unsigned capacity, count, resource_capacity;
};
struct avgl_ws {
    struct virgl_winsys base;
    struct avgl_transport transport;
    struct GalliumScreenFactory factory;
    struct SignalSemaphore mutex;
    struct virgl_hw_res *resources;
    struct virgl_winsys **owner_slot;
};
static struct avgl_ws *ws_cast(struct virgl_winsys *ws) { return (struct avgl_ws *)ws; }
static int ws_fd(struct virgl_winsys *ws) { (void)ws; return -1; }
static bool send_request(struct avgl_ws *ws, const void *req, ULONG size, bool fence)
{
    ULONG received;
    return avgl_command(&ws->transport,req,size,GPU_OK_NODATA,sizeof(struct gpu_header),&received,fence);
}
/* Only confirmed host cleanup authorizes releasing backing. On failure
 * the root registry retains it until the last client's safe physical reset. */
static bool cleanup_resource(struct avgl_ws *ws,uint32_t id,bool attached,bool backed,bool created)
{
    struct render_resource req={.resource=id};bool ok=true;
    if(attached){req.header.type=GPU_CTX_DETACH_RESOURCE;req.header.context=ws->transport.context;
        ok=avgl_cleanup_command(&ws->transport,&req,sizeof(req)) && ok;}
    req.header.context=0;
    if(backed){req.header.type=GPU_DETACH_BACKING;
        ok=avgl_cleanup_command(&ws->transport,&req,sizeof(req)) && ok;}
    if(created){req.header.type=GPU_RESOURCE_UNREF;
        ok=avgl_cleanup_command(&ws->transport,&req,sizeof(req)) && ok;}
    if(!ok)bug("[virgl] resource %lu backing retained for safe reset\n",(unsigned long)id);
    return ok;
}
static void resource_ref_raw(struct avgl_ws *ws,struct virgl_hw_res **dst,struct virgl_hw_res *src)
{
    struct virgl_hw_res *old=*dst;
    if (old==src) return;
    if (src) src->refs++;
    *dst=src;
    if (old && --old->refs==0) {
        struct virgl_hw_res **link=&ws->resources;
        if(cleanup_resource(ws,old->id,true,true,true))avgl_free(&ws->transport,old->map);
        while (*link && *link!=old) link=&(*link)->next;
        if (*link) *link=old->next;
        FreeVec(old);
    }
}
static void ws_resource_reference(struct virgl_winsys *vws,struct virgl_hw_res **dst,struct virgl_hw_res *src)
{
    struct avgl_ws *ws=ws_cast(vws);
    ObtainSemaphore(&ws->mutex);resource_ref_raw(ws,dst,src);ReleaseSemaphore(&ws->mutex);
}
static struct virgl_hw_res *ws_resource_create(struct virgl_winsys *vws,
    enum pipe_texture_target target,const void *front,uint32_t format,uint32_t bind,
    uint32_t width,uint32_t height,uint32_t depth,uint32_t array_size,uint32_t last_level,
    uint32_t samples,uint32_t flags,uint32_t size)
{
    struct avgl_ws *ws=ws_cast(vws);
    struct virgl_hw_res *res=NULL;
    bool created=false,attached=false,backed=false;
    ULONG native_format = ws->factory.format(ws->factory.module, GALLIUM_SCREEN_BACKEND_VIRGL, format);
    if (native_format == GALLIUM_SCREEN_FORMAT_INVALID) return NULL;
    struct render_create create={.header.type=GPU_RESOURCE_CREATE_3D,
        .target=target,.format=native_format,.bind=bind,.width=width,.height=height,
        .depth=depth,.array_size=array_size,.last_level=last_level,.samples=samples,.flags=flags};
    struct render_resource attach={.header.type=GPU_CTX_ATTACH_RESOURCE,.header.context=ws->transport.context};
    struct render_backing backing={.header.type=GPU_ATTACH_BACKING,.count=1,.length=size};
    (void)front;
    ObtainSemaphore(&ws->mutex);
    if (!avgl_healthy(&ws->transport) || !size) goto done;
    res=avgl_native_alloc(sizeof(*res));
    if (!res) goto done;
    res->ws=ws;res->id=avgl_resource_id(&ws->transport);res->refs=1;res->size=size;
    if(!res->id){FreeVec(res);res=NULL;goto done;}
    res->map=avgl_alloc(&ws->transport,size);
    if (!res->map) {FreeVec(res);res=NULL;goto done;}
    create.resource=attach.resource=backing.resource=res->id;
    backing.address=avgl_dma_address(&ws->transport,res->map);
    if(!backing.address || backing.address==~(UQUAD)0)goto rollback;
    if(!send_request(ws,&create,sizeof(create),true))goto rollback;
    created=true;
    if(!send_request(ws,&attach,sizeof(attach),true))goto rollback;
    attached=true;
    if(!send_request(ws,&backing,sizeof(backing),true))goto rollback;
    backed=true;
    res->next=ws->resources;ws->resources=res;
    goto done;
 rollback:
    if(cleanup_resource(ws,res->id,attached,backed,created))avgl_free(&ws->transport,res->map);
    FreeVec(res);res=NULL;
 done:
    ReleaseSemaphore(&ws->mutex);
    return res;
}
static int ws_transfer(struct virgl_winsys *vws,struct virgl_hw_res *res,
    const struct pipe_box *box,uint32_t stride,uint32_t layer_stride,
    uint32_t offset,uint32_t level,bool upload)
{
    struct avgl_ws *ws=ws_cast(vws);
    struct render_transfer req={.header.type=upload?GPU_TRANSFER_TO_HOST_3D:GPU_TRANSFER_FROM_HOST_3D,
        .header.context=ws->transport.context,.resource=res->id,.x=box->x,.y=box->y,.z=box->z,
        .width=box->width,.height=box->height,.depth=box->depth,
        .offset=offset,.level=level,.stride=stride,.layer_stride=layer_stride};
    ULONG flags=upload?DMA_ReadFromRAM:0;
    bool ok=false;
    ObtainSemaphore(&ws->mutex);
    if (offset>=res->size || box->x<0 || box->y<0 || box->z<0 ||
        box->width<=0 || box->height<=0 || box->depth<=0) goto done;
    if (!avgl_backing_begin(&ws->transport,res->map,flags)) goto done;
    ok=send_request(ws,&req,sizeof(req),true);
    avgl_backing_end(&ws->transport,res->map);
 done:
    /* Mesa's transfer queue does not propagate this return value. Losing a
     * transfer invalidates this client's stream, not the shared device. */
    if(!ok)ws->transport.failed=TRUE;
    ReleaseSemaphore(&ws->mutex);
    return ok?0:-1;
}
static int ws_put(struct virgl_winsys *ws,struct virgl_hw_res *r,const struct pipe_box *b,
    uint32_t stride,uint32_t layer,uint32_t off,uint32_t level)
{ return ws_transfer(ws,r,b,stride,layer,off,level,true); }
static int ws_get(struct virgl_winsys *ws,struct virgl_hw_res *r,const struct pipe_box *b,
    uint32_t stride,uint32_t layer,uint32_t off,uint32_t level)
{ return ws_transfer(ws,r,b,stride,layer,off,level,false); }
static void *ws_map(struct virgl_winsys *ws,struct virgl_hw_res *res)
{ return avgl_healthy(&ws_cast(ws)->transport)?res->map:NULL; }
static void ws_wait(struct virgl_winsys *ws,struct virgl_hw_res *res)
{ (void)ws;(void)res; /* Every submitted batch already waited for its GPU fence. */ }
static bool ws_busy(struct virgl_winsys *ws,struct virgl_hw_res *res)
{ (void)ws;(void)res;return false; }
static uint32_t ws_storage(struct virgl_winsys *ws,struct virgl_hw_res *res)
{ (void)ws;return res->size; }
static struct virgl_hw_res *ws_import(struct virgl_winsys *ws,struct winsys_handle *handle,
    struct pipe_resource *templ,uint32_t *plane,uint32_t *stride,uint32_t *offset,
    uint64_t *modifier,uint32_t *blob)
{ (void)ws;(void)handle;(void)templ;(void)plane;(void)stride;(void)offset;(void)modifier;(void)blob;return NULL; }
static bool ws_export(struct virgl_winsys *ws,struct virgl_hw_res *res,uint32_t stride,struct winsys_handle *handle)
{ (void)ws;(void)res;(void)stride;(void)handle;return false; }
static void ws_set_type(struct virgl_winsys *ws,struct virgl_hw_res *res,uint32_t format,
    uint32_t bind,uint32_t width,uint32_t height,uint32_t usage,uint64_t modifier,
    uint32_t count,const uint32_t *strides,const uint32_t *offsets)
{ (void)res;(void)format;(void)bind;(void)width;(void)height;(void)usage;(void)modifier;(void)count;(void)strides;(void)offsets;
  ws_cast(ws)->transport.failed=TRUE;bug("[virgl] untyped resource unsupported\n"); }
static void release_references(struct avgl_cb *cb)
{
    unsigned i;
    for(i=0;i<cb->count;i++) {
        cb->resources[i]->pending--;
        resource_ref_raw(cb->ws,&cb->resources[i],NULL);
    }
    cb->count=0;
}
static struct virgl_cmd_buf *ws_cb_create(struct virgl_winsys *vws,uint32_t words)
{
    struct avgl_cb *cb;
    /* The submission header shares the transport's request allocation. */
    if (!words || words>(VIRTIOGPU_MAX_REQUEST_BYTES-sizeof(struct gpu_header)-2*sizeof(ULONG))/sizeof(uint32_t)) return NULL;
    cb=avgl_native_alloc(sizeof(*cb));if(!cb)return NULL;
    cb->base.buf=avgl_native_alloc((size_t)words*sizeof(uint32_t));
    if(!cb->base.buf){FreeVec(cb);return NULL;}
    cb->capacity=words;cb->ws=ws_cast(vws);
    return &cb->base;
}
static void ws_cb_destroy(struct virgl_cmd_buf *base)
{
    struct avgl_cb *cb=(struct avgl_cb*)base;
    ObtainSemaphore(&cb->ws->mutex);release_references(cb);ReleaseSemaphore(&cb->ws->mutex);
    FreeVec(cb->resources);FreeVec(cb->base.buf);FreeVec(cb);
}
static void ws_emit(struct virgl_winsys *vws,struct virgl_cmd_buf *base,
    struct virgl_hw_res *res,bool write)
{
    struct avgl_cb *cb=(struct avgl_cb*)base;
    struct avgl_ws *ws=ws_cast(vws);unsigned i;
    ObtainSemaphore(&ws->mutex);
    if(write) {
        if(base->cdw>=cb->capacity){ws->transport.failed=TRUE;goto done;}
        base->buf[base->cdw++]=res?res->id:0;
    }
    if(!res)goto done;
    for(i=0;i<cb->count;i++)if(cb->resources[i]==res)goto done;
    if(cb->count==cb->resource_capacity) {
        unsigned capacity=cb->resource_capacity?cb->resource_capacity*2:32;
        struct virgl_hw_res **array=avgl_native_grow(cb->resources,(size_t)cb->resource_capacity*sizeof(*array),(size_t)capacity*sizeof(*array));
        if(!array){ws->transport.failed=TRUE;goto done;}
        cb->resources=array;cb->resource_capacity=capacity;
    }
    cb->resources[cb->count++]=res;res->refs++;res->pending++;
 done:
    ReleaseSemaphore(&ws->mutex);
}
static bool ws_referenced(struct virgl_winsys *ws,struct virgl_cmd_buf *cb,struct virgl_hw_res *res)
{ (void)ws;(void)cb;return res->pending!=0; }
static struct pipe_fence_handle *new_fence(void)
{
    struct pipe_fence_handle *f=avgl_native_alloc(sizeof(*f));if(f)f->refs=1;return f;
}
static int ws_submit(struct virgl_winsys *vws,struct virgl_cmd_buf *base,struct pipe_fence_handle **fence)
{
    struct avgl_ws *ws=ws_cast(vws);struct avgl_cb *cb=(struct avgl_cb*)base;
    struct submit_header {struct gpu_header header;ULONG size,padding;} *req=NULL;
    bool ok;
    ObtainSemaphore(&ws->mutex);
    ok=avgl_healthy(&ws->transport);
    if(fence)*fence=NULL;
    if(base->cdw>cb->capacity)ok=false;
    if(ok && fence){*fence=new_fence();if(!*fence)ok=false;}
    if(ok && base->cdw) {
        ULONG size=base->cdw*sizeof(uint32_t);
        req=avgl_native_alloc(sizeof(*req)+size);
        if(!req)ok=false;
        else {
            req->header.type=GPU_SUBMIT_3D;req->header.context=ws->transport.context;req->size=size;
            CopyMem(base->buf,req+1,size);ok=send_request(ws,req,sizeof(*req)+size,true);
        }
    }
    /* The pinned core ignores submit's return value and reuses its command
     * buffer. Never acknowledge later work after discarding this batch. */
    if(!ok)ws->transport.failed=TRUE;
    if(!ok && fence){FreeVec(*fence);*fence=NULL;}
    FreeVec(req);release_references(cb);base->cdw=0;
    ReleaseSemaphore(&ws->mutex);
    return ok?0:-1;
}
static int ws_caps(struct virgl_winsys *vws,struct virgl_drm_caps *caps)
{
    struct avgl_ws *ws=ws_cast(vws);size_t bytes=ws->transport.caps_size;
    avgl_native_clear(ws->transport.utility_base,caps,sizeof(*caps));virgl_ws_fill_new_caps_defaults(caps);
    if(bytes>sizeof(caps->caps.v1))bytes=sizeof(caps->caps.v1);
    CopyMem(ws->transport.caps,&caps->caps.v1,bytes);
    caps->caps.v1.max_version=1;
    return 0;
}
static struct pipe_fence_handle *ws_fence_create(struct virgl_winsys *ws,int fd)
{ return fd<0 && avgl_healthy(&ws_cast(ws)->transport)?new_fence():NULL; }
static bool ws_fence_wait(struct virgl_winsys *ws,struct pipe_fence_handle *f,uint64_t timeout)
{ (void)f;(void)timeout;return avgl_healthy(&ws_cast(ws)->transport); }
static void ws_fence_ref(struct virgl_winsys *ws,struct pipe_fence_handle **dst,struct pipe_fence_handle *src)
{
    struct pipe_fence_handle *old=*dst;
    if(old==src)return;
    ObtainSemaphore(&ws_cast(ws)->mutex);
    if(src)src->refs++;*dst=src;
    if(old && --old->refs==0)FreeVec(old);
    ReleaseSemaphore(&ws_cast(ws)->mutex);
}
static int ws_fence_fd(struct virgl_winsys *ws,struct pipe_fence_handle *f)
{ (void)ws;(void)f;return -1; }
static void ws_fence_sync(struct virgl_winsys *ws,struct virgl_cmd_buf *cb,struct pipe_fence_handle *f)
{ (void)ws;(void)cb;(void)f; }
static void ws_front(struct virgl_winsys *ws,struct virgl_cmd_buf *cb,struct virgl_hw_res *res,
    unsigned level,unsigned layer,void *handle,struct pipe_box *box)
{ (void)ws;(void)cb;(void)res;(void)level;(void)layer;(void)handle;(void)box; /* HIDD presents readback. */ }
void avgl_winsys_dispose(struct virgl_winsys *vws)
{
    struct avgl_ws *ws=ws_cast(vws);
    if(!ws)return;
    ObtainSemaphore(&ws->mutex);
    while(ws->resources){
        struct virgl_hw_res *r=ws->resources;ws->resources=r->next;
        if(cleanup_resource(ws,r->id,true,true,true))avgl_free(&ws->transport,r->map);
        FreeVec(r);
    }
    avgl_close(&ws->transport);
    if(ws->owner_slot)*ws->owner_slot=NULL;
    ReleaseSemaphore(&ws->mutex);FreeVec(ws);
}
struct virgl_winsys *avgl_winsys_create(struct virgl_winsys **owner_slot,
    const struct GalliumScreenFactory *factory, struct avgl_manager *manager)
{
    struct avgl_ws *ws=avgl_native_alloc(sizeof(*ws));
    if(!ws)return NULL;
    InitSemaphore(&ws->mutex);ws->owner_slot=owner_slot;ws->factory=*factory;
    if(!avgl_open(&ws->transport,manager)){FreeVec(ws);return NULL;}
    ws->base.destroy=avgl_winsys_dispose;ws->base.get_fd=ws_fd;
    ws->base.transfer_put=ws_put;ws->base.transfer_get=ws_get;
    ws->base.resource_create=ws_resource_create;ws->base.resource_reference=ws_resource_reference;
    ws->base.resource_map=ws_map;ws->base.resource_wait=ws_wait;ws->base.resource_is_busy=ws_busy;
    ws->base.resource_create_from_handle=ws_import;ws->base.resource_set_type=ws_set_type;
    ws->base.resource_get_handle=ws_export;ws->base.resource_get_storage_size=ws_storage;
    ws->base.cmd_buf_create=ws_cb_create;ws->base.cmd_buf_destroy=ws_cb_destroy;
    ws->base.emit_res=ws_emit;ws->base.submit_cmd=ws_submit;ws->base.res_is_referenced=ws_referenced;
    ws->base.get_caps=ws_caps;ws->base.cs_create_fence=ws_fence_create;
    ws->base.fence_wait=ws_fence_wait;ws->base.fence_reference=ws_fence_ref;
    ws->base.flush_frontbuffer=ws_front;ws->base.fence_server_sync=ws_fence_sync;
    ws->base.fence_get_fd=ws_fence_fd;
    return &ws->base;
}
bool avgl_read_resource(struct virgl_winsys *ws,struct virgl_hw_res *res,
    unsigned width,unsigned height,unsigned stride)
{
    struct pipe_box box={.width=width,.height=height,.depth=1};
    return ws_get(ws,res,&box,stride,stride*height,0,0)==0;
}
