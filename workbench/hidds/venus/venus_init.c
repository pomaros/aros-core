/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    venus.hidd - Mesa's Venus Vulkan driver behind vulkan.hidd.

    Venus encodes Vulkan commands for a renderer. Here the renderer runs
    in this system, in venusrenderer.library, over a native driver: the module
    opens that driver, starts the renderer on its root entry point and
    registers Venus as a GPU of its own. Applications then reach the native
    driver through the same protocol an isolated Windows process uses.
*/

#include <aros/symbolsets.h>
#include <aros/debug.h>
#include <hidd/vulkan_hidd.h>
#include <proto/exec.h>
#include <proto/oop.h>
#include <proto/vulkan.h>
#include <proto/venusrenderer.h>

#include <venus/avs.h>

#include "mesa_icd.h"
#include "venus_intern.h"

#include LC_LIBDEFS_FILE

/* The native driver the renderer runs its commands on */
#define VENUS_NATIVE_DRIVER     "lavapipe.hidd"
#define VENUS_NATIVE_CLASS      CLID_Hidd_Vulkan_Lavapipe

/* The renderer's functions are C calls through this base */
struct Library *VenusRendererBase;

static const struct VulkanDriverCallbacks *native;
static APTR native_data;

static avs_void_function native_gipa(void *instance, const char *name)
{
    return (avs_void_function)native->GetInstanceProcAddr(instance, name, native_data);
}

OOP_Object *METHOD(HiddVenus, Root, New)
{
    return (OOP_Object *)OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
}

static int HiddVenus_InitLib(LIBBASETYPEPTR LIBBASE)
{
    struct Library *HiddVulkanBase;

    if (!(HiddVulkanBase = OpenLibrary("vulkan.hidd", 0)))
        return FALSE;
    LIBBASE->vulkanhidd = HiddVulkanBase;

    /* The native driver registers itself as it opens */
    LIBBASE->nativehidd = OpenLibrary(VENUS_NATIVE_DRIVER, 0);
    native = VulkanHidd_FindDriver(VENUS_NATIVE_CLASS, &native_data);
    if (!native || !native->GetInstanceProcAddr)
    {
        bug("[venus] no native driver %s\n", VENUS_NATIVE_DRIVER);
        goto fail;
    }

    if (!(VenusRendererBase = OpenLibrary("venusrenderer.library", 0)))
        goto fail;
    LIBBASE->venuslib = VenusRendererBase;
    if (!avs_server_init(avs_local_memory(), native_gipa))
    {
        bug("[venus] the renderer did not start\n");
        goto fail;
    }

    /* Preferred over the native driver: lower priority values win */
    LIBBASE->slot = VulkanHidd_RegisterGPU(CLID_Hidd_Vulkan_Venus, NULL, 0x1af4, 0x1050,
                                           VULKAN_GPU_VGPU | VULKAN_GPU_COMPUTE,
                                           0, "Venus", -1,
                                           &mesa_icd_callbacks, LIBBASE, 0, 0, 0);
    if (LIBBASE->slot >= 0)
        return TRUE;
    bug("[venus] vulkan.hidd refused the registration\n");

fail:
    if (LIBBASE->venuslib)
        CloseLibrary(LIBBASE->venuslib);
    VenusRendererBase = NULL;
    if (LIBBASE->nativehidd)
        CloseLibrary(LIBBASE->nativehidd);
    CloseLibrary(HiddVulkanBase);
    LIBBASE->venuslib = LIBBASE->nativehidd = LIBBASE->vulkanhidd = NULL;
    return FALSE;
}

static int HiddVenus_ExpungeLib(LIBBASETYPEPTR LIBBASE)
{
    /* The registry keeps our callbacks: stay resident while vulkan.hidd is */
    return LIBBASE->vulkanhidd == NULL;
}

ADD2INITLIB(HiddVenus_InitLib, 0)
ADD2EXPUNGELIB(HiddVenus_ExpungeLib, 0)
