/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    lavapipe.hidd - Mesa's software Vulkan driver behind vulkan.hidd.

    The module registers one software GPU with vulkan.hidd, with the Mesa
    ICD callbacks (vulkan/mesa_icd.c) over lavapipe's entry point.
*/

#include <aros/symbolsets.h>
#include <aros/debug.h>
#include <hidd/vulkan_hidd.h>
#include <proto/exec.h>
#include <proto/oop.h>
#include <proto/vulkan.h>

#include "mesa_icd.h"
#include "lavapipe_intern.h"

#include LC_LIBDEFS_FILE

OOP_Object *METHOD(HiddLavapipe, Root, New)
{
    return (OOP_Object *)OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
}

static int HiddLavapipe_InitLib(LIBBASETYPEPTR LIBBASE)
{
    struct Library *HiddVulkanBase = OpenLibrary("vulkan.hidd", 0);

    if (!HiddVulkanBase)
        return FALSE;

    LIBBASE->vulkanhidd = HiddVulkanBase;
    LIBBASE->slot = VulkanHidd_RegisterGPU(CLID_Hidd_Vulkan_Lavapipe, NULL, 0x10005, 0,
                                           VULKAN_GPU_SOFTWARE | VULKAN_GPU_COMPUTE,
                                           0, "llvmpipe (lavapipe)", 0,
                                           &mesa_icd_callbacks, LIBBASE, 0, 0, 0);
    if (LIBBASE->slot < 0)
    {
        bug("[lavapipe] vulkan.hidd refused the registration\n");
        CloseLibrary(HiddVulkanBase);
        LIBBASE->vulkanhidd = NULL;
        return FALSE;
    }
    D(bug("[lavapipe] registered as GPU slot %ld\n", (long)LIBBASE->slot));
    return TRUE;
}

static int HiddLavapipe_ExpungeLib(LIBBASETYPEPTR LIBBASE)
{
    /* The registry keeps our callbacks: stay resident while vulkan.hidd is */
    return LIBBASE->vulkanhidd == NULL;
}

ADD2INITLIB(HiddLavapipe_InitLib, 0)
ADD2EXPUNGELIB(HiddLavapipe_ExpungeLib, 0)
