/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1. */
#include "frontend.h"
#include <hidd/gallium.h>

/* The framebuffer frontend's CreateObject serves everything but the GPU
 * transport service, the Gallium client and Vulkan. */
#define EFIFBGfxDisplay__Hidd_Display__CreateObject VirtioGfxDisplay_CreateFramebufferObject
#include "../efifbgfx/efifbgfx_displayclass.c"
#undef EFIFBGfxDisplay__Hidd_Display__CreateObject

#define VIRTIOGFX_VULKAN_CLASS  "hidd.vulkan"
#define VIRTIOGFX_GALLIUM_CLASS "hidd.gallium"
#define VIRTIOGFX_VIRGL_CLASS   "hidd.gallium.virgl"
#define VIRTIOGFX_VIRGL_DRIVER  "virgl.hidd"

/*
 * Vulkan: this display answers NULL until a host-backed Vulkan transport
 * exists, so the loader uses the software driver (lavapipe). The local Venus
 * class (hidd.vulkan.venus in venus.hidd) stays available to a caller that
 * opens the driver and names the class explicitly; the display no longer
 * offers it on the strength of the VirGL capability alone.
 */

/*
 * Gallium: a VirGL client bound to this display's device. The owner keeps
 * virgl.hidd open for its own lifetime, so a client's destructor can never
 * close the last reference to the module whose code it is executing.
 * The service object is created with one reference, which the VirGL
 * constructor retains for itself; the display drops its own reference on
 * either outcome.
 */
static OOP_Object *VirtioGfxDisplay_CreateGalliumObject(OOP_Class *cl, struct pHidd_Display_CreateObject *msg)
{
    struct VirtioGPU *gpu = XSD(cl)->data.present_data;
    struct VirtioGfxBase *base = (APTR)cl->UserData;
    struct Library *module;
    OOP_Object *service, *result = NULL;
    OOP_AttrBase ab;

    if (!VirtioGPUHasVirGL(gpu))
        return NULL;
    module = OpenLibrary((STRPTR)VIRTIOGFX_VIRGL_DRIVER, 0);
    if (!module)
        return NULL;
    ObtainSemaphore(&base->loader_lock);
    if (!base->virglmodule) {
        base->virglmodule = module;
        module = NULL;
    }
    ReleaseSemaphore(&base->loader_lock);
    if (module)
        CloseLibrary(module);

    {
        struct TagItem owner[] = { { VIRTIOGPU_OWNER_TAG, (IPTR)gpu }, { TAG_DONE, 0 } };
        service = OOP_NewObject(NULL, CLID_Hidd_VirtioGPU, owner);
    }
    ab = OOP_ObtainAttrBase(IID_Hidd_Gallium);
    if (service && ab) {
        struct TagItem tags[] = {
            { ab + aoHidd_Gallium_Transport, (IPTR)service },
            { TAG_MORE, (IPTR)msg->attrList }
        };
        result = OOP_NewObject(NULL, VIRTIOGFX_VIRGL_CLASS, tags);
    }
    if (ab)
        OOP_ReleaseAttrBase(IID_Hidd_Gallium);
    if (service) {
        struct pHidd_VirtioGPU_Release release = { OOP_GetMethodID(IID_Hidd_VirtioGPU, moHidd_VirtioGPU_Release) };
        OOP_DoMethod(service, (OOP_Msg)&release);
    }
    return result;
}

OOP_Object *EFIFBGfxDisplay__Hidd_Display__CreateObject(OOP_Class *cl, OOP_Object *o, struct pHidd_Display_CreateObject *msg)
{
    if (msg->cl && msg->cl == OOP_FindClass(CLID_Hidd_VirtioGPU)) {
        struct VirtioGPU *gpu = XSD(cl)->data.present_data;
        struct TagItem tags[] = { { VIRTIOGPU_OWNER_TAG, (IPTR)gpu }, { TAG_DONE, 0 } };
        return OOP_NewObject(NULL, CLID_Hidd_VirtioGPU, tags);
    }
    if (msg->cl && msg->cl == OOP_FindClass(VIRTIOGFX_GALLIUM_CLASS))
        return VirtioGfxDisplay_CreateGalliumObject(cl, msg);
    if (msg->cl && msg->cl != XSD(cl)->basebm && msg->cl == OOP_FindClass(VIRTIOGFX_VULKAN_CLASS))
        return NULL;
    return VirtioGfxDisplay_CreateFramebufferObject(cl, o, msg);
}
