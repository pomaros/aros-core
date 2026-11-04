/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1. */
#include "frontend.h"

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

OOP_Object *EFIFBGfxDisplay__Hidd_Display__CreateObject(OOP_Class *cl, OOP_Object *o, struct pHidd_Display_CreateObject *msg)
{
    if (msg->cl && msg->cl != XSD(cl)->basebm && msg->cl == OOP_FindClass(VIRTIOGFX_VULKAN_CLASS))
        return VirtioGfxDisplay_CreateVulkanObject(msg);
    return VirtioGfxDisplay_CreateFramebufferObject(cl, o, msg);
}
