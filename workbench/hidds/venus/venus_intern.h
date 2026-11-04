#ifndef VENUS_INTERN_H
#define VENUS_INTERN_H

/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.
*/

#include <stdint.h>

#include <exec/libraries.h>
#include <oop/oop.h>

#include LC_LIBDEFS_FILE

#define CLID_Hidd_Vulkan_Venus  "hidd.vulkan.venus"

struct venusstaticdata
{
    OOP_Class       *vulkanclass;
    OOP_AttrBase    hiddVulkanAB;
    OOP_MethodID    nativeGipa;     /* the native driver's GetInstanceProcAddr */
    OOP_Object      *native;        /* the native driver the renderer runs on */
    struct Library  *UtilityBase;
    struct Library  *nativehidd;
    struct Library  *venuslib;
};

LIBBASETYPE
{
    struct Library          LibNode;
    struct venusstaticdata  sd;
};

#define METHOD(base, id, name) \
  base ## __ ## id ## __ ## name (OOP_Class *cl, OOP_Object *o, struct p ## id ## _ ## name *msg)

#define BASE(lib) ((LIBBASETYPEPTR)(lib))

#define SD(cl) (&BASE(cl->UserData)->sd)

#endif
