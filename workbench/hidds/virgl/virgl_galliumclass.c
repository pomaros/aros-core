/* Copyright (C) 2026, The AROS Development Team. All rights reserved. */
#include <aros/debug.h>
#include <proto/oop.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>
#include <cybergraphx/cybergraphics.h>
#include <hidd/gallium.h>
#include <gallium/gallium.h>
#include "pipe/p_screen.h"

#include "virgl/virgl_resource.h"
#include "virgl_winsys_aros.h"
#include "virgl_intern.h"
#define CyberGfxBase (SD(cl)->CyberGfxBase)
#define UtilityBase (SD(cl)->UtilityBase)
#undef HiddGalliumAttrBase
#define HiddGalliumAttrBase (SD(cl)->hiddGalliumAB)
OOP_Object *METHOD(HiddVirgl,Root,New)
{
    bug("[virgl.hidd] client New\n");
    if(GetTagData(aHidd_Gallium_InterfaceVersion,-1,msg->attrList)!=GALLIUM_INTERFACE_VERSION)return NULL;
    o=(OOP_Object*)OOP_DoSuperMethod(cl,o,(OOP_Msg)msg);
    if(o){
        struct HiddGalliumVirglData *d=OOP_INST_DATA(cl,o);
        const struct GalliumScreenFactory *factory=(const void *)
            GetTagData(aHidd_Gallium_ScreenFactory,0,msg->attrList);
        d->winsys=NULL;d->screen=NULL;d->factory.module=NULL;
        avgl_manager_init(&d->manager,UtilityBase);
        InitSemaphore(&d->screen_lock);
        if(factory){
            struct Library *module=NULL;
            if(factory->version==GALLIUM_SCREEN_FACTORY_VERSION &&
               factory->size>=sizeof(*factory) &&
               (factory->backends&GALLIUM_SCREEN_BACKEND_VIRGL) &&
               factory->module && factory->create && factory->destroy && factory->format)
                module=OpenLibrary(factory->module->lib_Node.ln_Name,factory->module->lib_Version);
            bug("[virgl.hidd] factory module=%p opened=%p\n",factory->module,module);
            if(!module){OOP_DisposeObject(o);return NULL;}
            d->factory=*factory;d->factory.module=module;
        }
        OOP_Object *service=(APTR)GetTagData(aHidd_Gallium_Transport,0,msg->attrList);
        struct pHidd_VirtioGPU_Acquire acquire={OOP_GetMethodID(IID_Hidd_VirtioGPU,moHidd_VirtioGPU_Acquire),VIRTIOGPU_TRANSPORT_VERSION};
        if(!d->factory.module || !service || !OOP_DoMethod(service,(OOP_Msg)&acquire)){
            OOP_DisposeObject(o);return NULL;
        }
        d->manager.service=service;
    }
    return o;
}
VOID METHOD(HiddVirgl,Root,Dispose)
{
    struct HiddGalliumVirglData *d=OOP_INST_DATA(cl,o);
    if(d->screen)d->factory.destroy(d->factory.module,d->screen);
    d->screen=NULL;
    if(d->winsys)avgl_winsys_dispose(d->winsys);
    if(d->manager.service){
        struct pHidd_VirtioGPU_Release release={OOP_GetMethodID(IID_Hidd_VirtioGPU,moHidd_VirtioGPU_Release)};
        OOP_DoMethod(d->manager.service,(OOP_Msg)&release);
    }
    if(d->factory.module){
        struct Library *module=d->factory.module;d->factory.module=NULL;CloseLibrary(module);
    }
    OOP_DoSuperMethod(cl,o,(OOP_Msg)msg);
}
VOID METHOD(HiddVirgl,Root,Get)
{
    ULONG idx;
    if(IS_GALLIUM_ATTR(msg->attrID,idx) && idx==aoHidd_Gallium_InterfaceVersion){*msg->storage=GALLIUM_INTERFACE_VERSION;return;}
    OOP_DoSuperMethod(cl,o,(OOP_Msg)msg);
}
APTR METHOD(HiddVirgl,Hidd_Gallium,CreatePipeScreen)
{
    struct HiddGalliumVirglData *d=OOP_INST_DATA(cl,o);
    struct pipe_screen *screen=NULL;
    ObtainSemaphore(&d->screen_lock);
    if(!d->winsys && !d->screen && d->factory.module){
        d->winsys=avgl_winsys_create(&d->winsys,&d->factory,&d->manager);
        if(d->winsys){
            d->screen=d->factory.create(d->factory.module,GALLIUM_SCREEN_BACKEND_VIRGL,d->winsys);
            /* A failed core constructor may already consume the winsys;
             * its native destroy clears this owner slot. */
            if(!d->screen && d->winsys)avgl_winsys_dispose(d->winsys);
        }
    }
    screen=d->screen;
    ReleaseSemaphore(&d->screen_lock);
    return screen;
}
VOID METHOD(HiddVirgl,Hidd_Gallium,DestroyPipeScreen)
{
    struct HiddGalliumVirglData *d=OOP_INST_DATA(cl,o);
    ObtainSemaphore(&d->screen_lock);
    if(d->screen && msg->screen==d->screen){
        d->factory.destroy(d->factory.module,d->screen);d->screen=NULL;
    }
    ReleaseSemaphore(&d->screen_lock);
}
VOID METHOD(HiddVirgl,Hidd_Gallium,DisplayResource)
{
    struct HiddGalliumVirglData *d=OOP_INST_DATA(cl,o);
    struct virgl_resource *res=virgl_resource(msg->resource);
    struct RastPort *rp;
    APTR pixels;
    ULONG format;
    if(!d->winsys || !res || !res->hw_res)return;
    switch(res->b.format){
      case PIPE_FORMAT_R8G8B8A8_UNORM:case PIPE_FORMAT_R8G8B8A8_SRGB:format=RECTFMT_RGBA;break;
      case PIPE_FORMAT_B8G8R8A8_UNORM:case PIPE_FORMAT_B8G8R8X8_UNORM:
      case PIPE_FORMAT_B8G8R8A8_SRGB:case PIPE_FORMAT_B8G8R8X8_SRGB:format=RECTFMT_BGRA32;break;
      default:bug("[virgl.hidd] unsupported presentation format %u\n",res->b.format);return;
    }
    if(!avgl_read_resource(d->winsys,res->hw_res,res->b.width0,res->b.height0,res->metadata.stride[0]))return;
    pixels=d->winsys->resource_map(d->winsys,res->hw_res);
    if(!pixels)return;
    rp=CreateRastPort();if(!rp)return;
    rp->BitMap=msg->bitmap;
    WritePixelArray(pixels,msg->srcx,msg->srcy,
        res->metadata.stride[0],rp,msg->dstx,msg->dsty,msg->width,msg->height,format);
    FreeRastPort(rp);
}
