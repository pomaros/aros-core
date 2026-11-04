/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    hidd.vulkan.venus: Mesa's Venus behind the Vulkan HIDD interface. The
    ICD's vk_icdGetInstanceProcAddr serves every entry point; the base class
    implements the other methods through it.
*/

#ifndef DEBUG
#define DEBUG 0
#endif
#include <aros/debug.h>

#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>

#include "vk_device.h"

#include <proto/oop.h>
#include <proto/utility.h>

#include <hidd/vulkan_hidd.h>

#include "venus_intern.h"

#include <string.h>

#define UtilityBase    (&BASE(cl->UserData)->sd)->UtilityBase

#undef HiddVulkanAttrBase
#define HiddVulkanAttrBase   (SD(cl)->hiddVulkanAB)

/* Venus's ICD entry point (vn_icd.c) */
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName);

OOP_Object *METHOD(HiddVenus, Root, New)
{
    IPTR interfaceVers = GetTagData(aHidd_Vulkan_InterfaceVersion, -1, msg->attrList);

    if (interfaceVers != VULKAN_INTERFACE_VERSION)
    {
        D(bug("[venus] interface version mismatch (got %ld, want %d)\n",
              (long)interfaceVers, VULKAN_INTERFACE_VERSION));
        return NULL;
    }

    /* The instance is made on the first vkCreateInstance, through the entry
       points below; the renderer started with the module */
    return (OOP_Object *)OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
}

VOID METHOD(HiddVenus, Root, Dispose)
{
    OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
}

/*
 * The ICD's vkGetInstanceProcAddr. With a NULL instance the global commands
 * (vkCreateInstance, vkEnumerateInstance*) come back, and the name
 * "vkGetInstanceProcAddr" the entry point itself.
 */
APTR METHOD(HiddVenus, Hidd_Vulkan, GetInstanceProcAddr)
{
    if (!msg->pName)
        return NULL;

    if (!strcmp(msg->pName, "vkGetInstanceProcAddr"))
        return (APTR)vk_icdGetInstanceProcAddr;

    return (APTR)vk_icdGetInstanceProcAddr((VkInstance)msg->instance, msg->pName);
}

/* The device's dispatch table, as the runtime's vkGetDeviceProcAddr reads it */
APTR METHOD(HiddVenus, Hidd_Vulkan, GetDeviceProcAddr)
{
    VK_FROM_HANDLE(vk_device, device, (VkDevice)msg->device);

    if (!device || !msg->pName)
        return NULL;

    return (APTR)vk_device_get_proc_addr(device, msg->pName);
}
