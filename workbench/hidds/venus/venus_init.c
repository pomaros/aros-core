/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    venus.hidd - Mesa's Venus Vulkan driver behind the Vulkan HIDD interface.

    Venus encodes Vulkan commands for a renderer. Here the renderer runs in
    this system, in venusrenderer.library, over a native driver: the module
    makes that driver's object and starts the renderer on its root entry
    point. hidd.vulkan.venus then serves applications through the same
    protocol an isolated Windows process uses.
*/

#include <aros/symbolsets.h>
#include <aros/debug.h>
#include <hidd/vulkan_hidd.h>
#include <proto/exec.h>
#include <proto/oop.h>
#include <proto/venusrenderer.h>

#include <venus/avs.h>

#include "venus_intern.h"

/* The native driver the renderer runs its commands on */
#define VENUS_NATIVE_DRIVER     "lavapipe.hidd"
#define VENUS_NATIVE_CLASS      "hidd.vulkan.lavapipe"

/* The renderer's functions are C calls through this base */
struct Library *VenusRendererBase;

/* The renderer's entry point takes no context: one renderer per system */
static struct venusstaticdata *venus_sd;

static avs_void_function native_gipa(void *instance, const char *name)
{
    struct pHidd_Vulkan_GetInstanceProcAddr msg = {
        .mID      = venus_sd->nativeGipa,
        .instance = instance,
        .pName    = name,
    };

    return (avs_void_function)OOP_DoMethod(venus_sd->native, (OOP_Msg)&msg);
}

static void venus_release(struct venusstaticdata *sd)
{
    if (sd->venuslib)
        CloseLibrary(sd->venuslib);
    VenusRendererBase = NULL;
    if (sd->native)
        OOP_DisposeObject(sd->native);
    if (sd->nativehidd)
        CloseLibrary(sd->nativehidd);
    if (sd->hiddVulkanAB)
        OOP_ReleaseAttrBase((STRPTR)IID_Hidd_Vulkan);
    if (sd->UtilityBase)
        CloseLibrary(sd->UtilityBase);
    sd->venuslib = sd->nativehidd = sd->UtilityBase = NULL;
    sd->native = NULL;
    sd->hiddVulkanAB = 0;
}

static int HiddVenus_InitLib(LIBBASETYPEPTR LIBBASE)
{
    struct venusstaticdata *sd = &LIBBASE->sd;
    struct TagItem tags[] = {
        { 0, VULKAN_INTERFACE_VERSION },
        { TAG_DONE, 0 }
    };

    venus_sd = sd;
    if (!(sd->UtilityBase = OpenLibrary((STRPTR)"utility.library", 0)) ||
        !(sd->hiddVulkanAB = OOP_ObtainAttrBase((STRPTR)IID_Hidd_Vulkan)))
        goto fail;
    sd->nativeGipa = OOP_GetMethodID((STRPTR)IID_Hidd_Vulkan, moHidd_Vulkan_GetInstanceProcAddr);
    tags[0].ti_Tag = sd->hiddVulkanAB + aoHidd_Vulkan_InterfaceVersion;

    if (!(sd->nativehidd = OpenLibrary((STRPTR)VENUS_NATIVE_DRIVER, 0)) ||
        !(sd->native = OOP_NewObject(NULL, VENUS_NATIVE_CLASS, tags)))
    {
        bug("[venus] no native driver %s\n", VENUS_NATIVE_DRIVER);
        goto fail;
    }

    if (!(VenusRendererBase = OpenLibrary((STRPTR)"venusrenderer.library", 0)))
        goto fail;
    sd->venuslib = VenusRendererBase;
    if (!avs_server_init(avs_local_memory(), native_gipa))
    {
        bug("[venus] the renderer did not start\n");
        goto fail;
    }
    return TRUE;

fail:
    venus_release(sd);
    return FALSE;
}

/* The renderer's threads run from this module: it stays resident */
static int HiddVenus_ExpungeLib(LIBBASETYPEPTR LIBBASE)
{
    return LIBBASE->sd.native == NULL;
}

ADD2INITLIB(HiddVenus_InitLib, 0)
ADD2EXPUNGELIB(HiddVenus_ExpungeLib, 0)

ADD2LIBS((STRPTR)"vulkan.hidd", 8, static struct Library *, VulkanHiddBase);
