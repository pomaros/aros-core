/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1. */
#include "frontend.h"
#include <hidd/gallium.h>

/* The framebuffer frontend's CreateObject serves everything but Vulkan */
#define EFIFBGfxDisplay__Hidd_Display__CreateObject VirtioGfxDisplay_CreateFramebufferObject
#include "../efifbgfx/efifbgfx_displayclass.c"
#undef EFIFBGfxDisplay__Hidd_Display__CreateObject

#define VIRTIOGFX_VULKAN_CLASS  "hidd.vulkan"
#define VIRTIOGFX_VENUS_CLASS   "hidd.vulkan.venus"
#define VIRTIOGFX_VENUS_DRIVER  "venus.hidd"

/*
 * A Vulkan object is Venus, loaded on first use with the caller's interface
 * version. NULL when Venus is not installed or refuses: the loader then
 * makes the software driver. The driver stays loaded with this one; a second
 * caller racing the first only takes one more reference.
 */
static OOP_Object *VirtioGfxDisplay_CreateVulkanObject(struct pHidd_Display_CreateObject *msg)
{
    static struct Library *venus;

    if (!venus)
        venus = OpenLibrary((STRPTR)VIRTIOGFX_VENUS_DRIVER, 0);
    return venus ? OOP_NewObject(NULL, VIRTIOGFX_VENUS_CLASS, msg->attrList) : NULL;
}

static OOP_Object *VirtioGfxDisplay_CreateGalliumObject(OOP_Class *cl,struct pHidd_Display_CreateObject *msg)
{
    struct VirtioGPU *gpu=XSD(cl)->data.present_data;
    if(!VirtioGPUHasVirGL(gpu))return NULL;
    struct Library *module=OpenLibrary("virgl.hidd",0);
    if(!module)return NULL;
    /* The installed device owner pins protocol code. Never make a client's
     * destructor close the last reference to its own executing module. */
    struct VirtioGfxBase *base=(APTR)cl->UserData;
    ObtainSemaphore(&base->loader_lock);
    if(!base->virglmodule){base->virglmodule=module;module=NULL;}
    ReleaseSemaphore(&base->loader_lock);
    if(module)CloseLibrary(module);
    struct TagItem owner[]={{VIRTIOGPU_OWNER_TAG,(IPTR)gpu},{TAG_DONE,0}};
    OOP_Object *service=OOP_NewObject(NULL,CLID_Hidd_VirtioGPU,owner),*result=NULL;
    OOP_AttrBase ab=OOP_ObtainAttrBase(IID_Hidd_Gallium);
    if(service && ab){
        struct TagItem tags[]={{ab+aoHidd_Gallium_Transport,(IPTR)service},
            {TAG_MORE,(IPTR)msg->attrList}};
        result=OOP_NewObject(NULL,"hidd.gallium.virgl",tags);
    }
    if(ab)OOP_ReleaseAttrBase(IID_Hidd_Gallium);
    if(service){
        struct pHidd_VirtioGPU_Release release={OOP_GetMethodID(IID_Hidd_VirtioGPU,moHidd_VirtioGPU_Release)};
        OOP_DoMethod(service,(OOP_Msg)&release);
    }
    return result;
}
OOP_Object *EFIFBGfxDisplay__Hidd_Display__CreateObject(OOP_Class *cl, OOP_Object *o, struct pHidd_Display_CreateObject *msg)
{
    if(msg->cl && msg->cl==OOP_FindClass(CLID_Hidd_VirtioGPU)){
        struct VirtioGPU *gpu=XSD(cl)->data.present_data;
        struct TagItem tags[]={{VIRTIOGPU_OWNER_TAG,(IPTR)gpu},{TAG_DONE,0}};
        return OOP_NewObject(NULL,CLID_Hidd_VirtioGPU,tags);
    }
    if(msg->cl && msg->cl==OOP_FindClass("hidd.gallium"))
        return VirtioGfxDisplay_CreateGalliumObject(cl,msg);
    if (msg->cl && msg->cl != XSD(cl)->basebm && msg->cl == OOP_FindClass(VIRTIOGFX_VULKAN_CLASS))
        return VirtioGfxDisplay_CreateVulkanObject(msg);
    return VirtioGfxDisplay_CreateFramebufferObject(cl, o, msg);
}
