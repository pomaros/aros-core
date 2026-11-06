/* Copyright (C) 2026, The AROS Development Team. All rights reserved.
 * Protocol client: PCI, queues, DMA and reset belong to virtiogfx. */
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/oop.h>
#include <aros/debug.h>
#include "virgl_transport.h"
#include "virgl_native.h"
static OOP_MethodID method(ULONG i){return OOP_GetMethodID(IID_Hidd_VirtioGPU,i);}
void avgl_manager_init(struct avgl_manager *m,struct Library *u)
{InitSemaphore(&m->mutex);m->utility_base=u;m->references=0;m->service=NULL;m->initialized=TRUE;}
BOOL avgl_manager_can_expunge(struct avgl_manager *m){return !m->initialized || !m->references;}
BOOL avgl_healthy(struct avgl_transport *s)
{
    struct pHidd_VirtioGPU_Healthy msg={method(moHidd_VirtioGPU_Healthy)};
    return s->service && !s->failed && OOP_DoMethod(s->service,(OOP_Msg)&msg);
}
static BOOL command(struct avgl_transport *s,const void *p,ULONG n,ULONG type,ULONG size,void *out)
{
    struct pHidd_VirtioGPU_Command msg={method(moHidd_VirtioGPU_Command),p,n,out,size,type};
    return s->service && OOP_DoMethod(s->service,(OOP_Msg)&msg);
}
BOOL avgl_command(struct avgl_transport *s,const void *p,ULONG n,ULONG type,ULONG min,ULONG *got,BOOL fenced)
{
    struct gpu_header reply;(void)fenced;
    if(!got || min!=sizeof(reply) || type!=GPU_OK_NODATA || s->failed)return FALSE;
    *got=0;BOOL ok=command(s,p,n,type,sizeof(reply),&reply);
    if(ok){*got=sizeof(reply);if(((const struct gpu_header*)p)->type==GPU_RESOURCE_CREATE_3D)s->resources_live++;}
    return ok;
}
BOOL avgl_cleanup_command(struct avgl_transport *s,const void *p,ULONG n)
{
    struct gpu_header reply;BOOL ok=command(s,p,n,GPU_OK_NODATA,sizeof(reply),&reply);
    if(ok && ((const struct gpu_header*)p)->type==GPU_RESOURCE_UNREF && s->resources_live)s->resources_live--;
    return ok;
}
ULONG avgl_resource_id(struct avgl_transport *s)
{
    struct pHidd_VirtioGPU_AllocateID msg={method(moHidd_VirtioGPU_AllocateID),FALSE};
    return avgl_healthy(s)?OOP_DoMethod(s->service,(OOP_Msg)&msg):0;
}
APTR avgl_alloc(struct avgl_transport *s,ULONG n)
{
    struct pHidd_VirtioGPU_Allocate msg={method(moHidd_VirtioGPU_Allocate),n};
    return avgl_healthy(s)?(APTR)OOP_DoMethod(s->service,(OOP_Msg)&msg):NULL;
}
void avgl_free(struct avgl_transport *s,APTR p)
{
    struct pHidd_VirtioGPU_Free msg={method(moHidd_VirtioGPU_Free),p};
    if(s->service)OOP_DoMethod(s->service,(OOP_Msg)&msg);
}
UQUAD avgl_dma_address(struct avgl_transport *s,APTR p)
{
    UQUAD address=~(UQUAD)0;
    struct pHidd_VirtioGPU_Address msg={method(moHidd_VirtioGPU_Address),p,&address};
    if(s->service)OOP_DoMethod(s->service,(OOP_Msg)&msg);return address;
}
BOOL avgl_backing_begin(struct avgl_transport *s,APTR p,ULONG flags)
{
    struct pHidd_VirtioGPU_BackingBegin msg={method(moHidd_VirtioGPU_BackingBegin),p,flags};
    return s->service && OOP_DoMethod(s->service,(OOP_Msg)&msg);
}
void avgl_backing_end(struct avgl_transport *s,APTR p)
{
    struct pHidd_VirtioGPU_BackingEnd msg={method(moHidd_VirtioGPU_BackingEnd),p};
    if(s->service)OOP_DoMethod(s->service,(OOP_Msg)&msg);
}
BOOL avgl_close(struct avgl_transport *s)
{
    if(!s->service)return TRUE;
    if(s->context_created){
        struct gpu_header req={.type=GPU_CTX_DESTROY,.context=s->context},reply;
        if(!command(s,&req,sizeof(req),GPU_OK_NODATA,sizeof(reply),&reply))s->failed=TRUE;
    }
    bug("[virgl] shared close ctx=%lu resources=%lu failed=%lu\n",(unsigned long)s->context,(unsigned long)s->resources_live,(unsigned long)s->failed);
    struct pHidd_VirtioGPU_Release msg={method(moHidd_VirtioGPU_Release)};
    OOP_DoMethod(s->service,(OOP_Msg)&msg);s->service=NULL;s->context_created=FALSE;
    ObtainSemaphore(&s->manager->mutex);s->manager->references--;ReleaseSemaphore(&s->manager->mutex);
    return !s->failed;
}
BOOL avgl_open(struct avgl_transport *s,struct avgl_manager *m)
{
    struct pHidd_VirtioGPU_Acquire acquire={method(moHidd_VirtioGPU_Acquire),VIRTIOGPU_TRANSPORT_VERSION};
    struct pHidd_VirtioGPU_AllocateID id={method(moHidd_VirtioGPU_AllocateID),TRUE};
    struct render_context req={.header.type=GPU_CTX_CREATE,.length=17,.name="AROS shared VirGL"};
    struct gpu_header reply;
    avgl_native_clear(m->utility_base,s,sizeof(*s));s->manager=m;s->utility_base=m->utility_base;
    if(!m->service || !OOP_DoMethod(m->service,(OOP_Msg)&acquire))return FALSE;
    s->service=m->service;ObtainSemaphore(&m->mutex);m->references++;ReleaseSemaphore(&m->mutex);
    for(ULONG i=0;i<16;i++){
        struct capset_request query={.header.type=GPU_GET_CAPSET_INFO,.id=i};struct capset_info info;
        if(!command(s,&query,sizeof(query),GPU_OK_CAPSET_INFO,sizeof(info),&info))goto fail;
        if(info.id!=1)continue;
        if(!info.size || info.size>sizeof(s->caps)-sizeof(struct gpu_header))goto fail;
        UBYTE *data=AllocVec(sizeof(struct gpu_header)+info.size,MEMF_PUBLIC);if(!data)goto fail;
        query.header.type=GPU_GET_CAPSET;query.id=1;query.version=info.version;
        BOOL ok=command(s,&query,sizeof(query),GPU_OK_CAPSET,sizeof(struct gpu_header)+info.size,data);
        if(ok){CopyMem(data+sizeof(struct gpu_header),s->caps,info.size);s->caps_size=info.size;}
        FreeVec(data);if(!ok)goto fail;break;
    }
    if(!s->caps_size || !(s->context=OOP_DoMethod(s->service,(OOP_Msg)&id)))goto fail;
    req.header.context=s->context;
    if(!command(s,&req,sizeof(req),GPU_OK_NODATA,sizeof(reply),&reply))goto fail;
    s->context_created=TRUE;return TRUE;
fail:
    avgl_close(s);return FALSE;
}
