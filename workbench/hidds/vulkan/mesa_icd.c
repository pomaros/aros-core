/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: a Mesa Vulkan driver behind vulkan.hidd.

    Mesa's drivers share one runtime and one ICD entry point. These
    callbacks resolve every function through that entry point, so the
    loader hands applications the driver's own functions with their exact
    semantics once an instance exists. The driver module that compiles
    this file supplies vk_icdGetInstanceProcAddr.
*/

#include <hidd/vulkan_hidd.h>

#include <vulkan/vulkan_core.h>

#include "vk_instance.h"
#include "vk_physical_device.h"
#include "vk_device.h"

#include "mesa_icd.h"

PUBLIC VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName);

static PFN_vkVoidFunction icd_global(const char *name)
{
    return vk_icdGetInstanceProcAddr(VK_NULL_HANDLE, name);
}

static PFN_vkVoidFunction icd_physical(APTR physDev, const char *name)
{
    VK_FROM_HANDLE(vk_physical_device, pdevice, (VkPhysicalDevice)physDev);

    return vk_icdGetInstanceProcAddr(vk_instance_to_handle(pdevice->instance), name);
}

static PFN_vkVoidFunction icd_device(APTR device, const char *name)
{
    VK_FROM_HANDLE(vk_device, dev, (VkDevice)device);

    return vk_device_get_proc_addr(dev, name);
}

static APTR Icd_GetInstanceProcAddr(APTR instance, const char *pName, APTR dd)
{
    return (APTR)vk_icdGetInstanceProcAddr((VkInstance)instance, pName);
}

static APTR Icd_GetDeviceProcAddr(APTR device, const char *pName, APTR dd)
{
    return (APTR)icd_device(device, pName);
}

static APTR Icd_CreateInstance(const void *createInfo, APTR dd)
{
    static const VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .apiVersion = VK_API_VERSION_1_3,
    };
    const VkInstanceCreateInfo fallback = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app,
    };
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)icd_global("vkCreateInstance");
    VkInstance instance = VK_NULL_HANDLE;

    if (!create)
        return NULL;
    if (create(createInfo ? createInfo : &fallback, NULL, &instance) != VK_SUCCESS)
        return NULL;
    return instance;
}

static void Icd_DestroyInstance(APTR instance, APTR dd)
{
    PFN_vkDestroyInstance destroy =
        (PFN_vkDestroyInstance)vk_icdGetInstanceProcAddr((VkInstance)instance, "vkDestroyInstance");

    if (destroy)
        destroy((VkInstance)instance, NULL);
}

static int32_t Icd_EnumeratePhysicalDevices(APTR instance, ULONG *pCount,
                                            APTR *pDevices, APTR dd)
{
    PFN_vkEnumeratePhysicalDevices enumerate = (PFN_vkEnumeratePhysicalDevices)
        vk_icdGetInstanceProcAddr((VkInstance)instance, "vkEnumeratePhysicalDevices");
    uint32_t count = pCount ? *pCount : 0;
    VkResult result;

    if (!enumerate)
        return VK_ERROR_INITIALIZATION_FAILED;
    result = enumerate((VkInstance)instance, &count, (VkPhysicalDevice *)pDevices);
    if (pCount)
        *pCount = count;
    return result;
}

static void Icd_GetPhysicalDeviceProperties(APTR physDev, APTR pProperties, APTR dd)
{
    ((PFN_vkGetPhysicalDeviceProperties)icd_physical(physDev, "vkGetPhysicalDeviceProperties"))
        ((VkPhysicalDevice)physDev, pProperties);
}

static void Icd_GetPhysicalDeviceProperties2(APTR physDev, APTR pProperties, APTR dd)
{
    ((PFN_vkGetPhysicalDeviceProperties2)icd_physical(physDev, "vkGetPhysicalDeviceProperties2"))
        ((VkPhysicalDevice)physDev, pProperties);
}

static void Icd_GetPhysicalDeviceFeatures(APTR physDev, APTR pFeatures, APTR dd)
{
    ((PFN_vkGetPhysicalDeviceFeatures)icd_physical(physDev, "vkGetPhysicalDeviceFeatures"))
        ((VkPhysicalDevice)physDev, pFeatures);
}

static void Icd_GetPhysicalDeviceFeatures2(APTR physDev, APTR pFeatures, APTR dd)
{
    ((PFN_vkGetPhysicalDeviceFeatures2)icd_physical(physDev, "vkGetPhysicalDeviceFeatures2"))
        ((VkPhysicalDevice)physDev, pFeatures);
}

static void Icd_GetPhysicalDeviceMemoryProperties(APTR physDev, APTR pMemProps, APTR dd)
{
    ((PFN_vkGetPhysicalDeviceMemoryProperties)icd_physical(physDev, "vkGetPhysicalDeviceMemoryProperties"))
        ((VkPhysicalDevice)physDev, pMemProps);
}

static void Icd_GetPhysicalDeviceQueueFamilyProperties(APTR physDev, ULONG *pCount,
                                                       APTR pProps, APTR dd)
{
    uint32_t count = pCount ? *pCount : 0;

    ((PFN_vkGetPhysicalDeviceQueueFamilyProperties)
        icd_physical(physDev, "vkGetPhysicalDeviceQueueFamilyProperties"))
        ((VkPhysicalDevice)physDev, &count, pProps);
    if (pCount)
        *pCount = count;
}

static int32_t Icd_EnumerateDeviceExtensionProperties(APTR physDev, const char *pLayerName,
                                                      ULONG *pCount, APTR pProps, APTR dd)
{
    uint32_t count = pCount ? *pCount : 0;
    VkResult result;

    result = ((PFN_vkEnumerateDeviceExtensionProperties)
        icd_physical(physDev, "vkEnumerateDeviceExtensionProperties"))
        ((VkPhysicalDevice)physDev, pLayerName, &count, pProps);
    if (pCount)
        *pCount = count;
    return result;
}

static APTR Icd_CreateDevice(APTR physDev, APTR pCreateInfo, APTR dd)
{
    VkDevice device = VK_NULL_HANDLE;

    if (((PFN_vkCreateDevice)icd_physical(physDev, "vkCreateDevice"))
            ((VkPhysicalDevice)physDev, pCreateInfo, NULL, &device) != VK_SUCCESS)
        return NULL;
    return device;
}

static void Icd_DestroyDevice(APTR device, APTR dd)
{
    ((PFN_vkDestroyDevice)icd_device(device, "vkDestroyDevice"))((VkDevice)device, NULL);
}

static void Icd_GetDeviceQueue(APTR device, ULONG family, ULONG index, APTR *pQueue, APTR dd)
{
    ((PFN_vkGetDeviceQueue)icd_device(device, "vkGetDeviceQueue"))
        ((VkDevice)device, family, index, (VkQueue *)pQueue);
}

static int32_t Icd_DeviceWaitIdle(APTR device, APTR dd)
{
    return ((PFN_vkDeviceWaitIdle)icd_device(device, "vkDeviceWaitIdle"))((VkDevice)device);
}

static int32_t Icd_EnumerateInstanceVersion(ULONG *pApiVersion, APTR dd)
{
    PFN_vkEnumerateInstanceVersion version =
        (PFN_vkEnumerateInstanceVersion)icd_global("vkEnumerateInstanceVersion");
    uint32_t v = 0;
    VkResult result;

    if (!version)
        return VK_ERROR_INITIALIZATION_FAILED;
    result = version(&v);
    if (pApiVersion)
        *pApiVersion = v;
    return result;
}

static int32_t Icd_EnumerateInstanceExtensionProperties(const char *pLayerName, ULONG *pCount,
                                                        APTR pProps, APTR dd)
{
    PFN_vkEnumerateInstanceExtensionProperties enumerate =
        (PFN_vkEnumerateInstanceExtensionProperties)
            icd_global("vkEnumerateInstanceExtensionProperties");
    uint32_t count = pCount ? *pCount : 0;
    VkResult result;

    if (!enumerate)
        return VK_ERROR_INITIALIZATION_FAILED;
    result = enumerate(pLayerName, &count, pProps);
    if (pCount)
        *pCount = count;
    return result;
}

static int32_t Icd_EnumerateInstanceLayerProperties(ULONG *pCount, APTR pProps, APTR dd)
{
    if (pCount)
        *pCount = 0;
    return VK_SUCCESS;
}

const struct VulkanDriverCallbacks mesa_icd_callbacks =
{
    .GetInstanceProcAddr                    = Icd_GetInstanceProcAddr,
    .GetDeviceProcAddr                      = Icd_GetDeviceProcAddr,
    .CreateInstance                         = Icd_CreateInstance,
    .DestroyInstance                        = Icd_DestroyInstance,
    .EnumeratePhysicalDevices               = Icd_EnumeratePhysicalDevices,
    .GetPhysicalDeviceProperties            = Icd_GetPhysicalDeviceProperties,
    .GetPhysicalDeviceProperties2           = Icd_GetPhysicalDeviceProperties2,
    .GetPhysicalDeviceFeatures              = Icd_GetPhysicalDeviceFeatures,
    .GetPhysicalDeviceFeatures2             = Icd_GetPhysicalDeviceFeatures2,
    .GetPhysicalDeviceMemoryProperties      = Icd_GetPhysicalDeviceMemoryProperties,
    .GetPhysicalDeviceQueueFamilyProperties = Icd_GetPhysicalDeviceQueueFamilyProperties,
    .EnumerateDeviceExtensionProperties     = Icd_EnumerateDeviceExtensionProperties,
    .CreateDevice                           = Icd_CreateDevice,
    .DestroyDevice                          = Icd_DestroyDevice,
    .GetDeviceQueue                         = Icd_GetDeviceQueue,
    .DeviceWaitIdle                         = Icd_DeviceWaitIdle,
    .EnumerateInstanceVersion               = Icd_EnumerateInstanceVersion,
    .EnumerateInstanceExtensionProperties   = Icd_EnumerateInstanceExtensionProperties,
    .EnumerateInstanceLayerProperties       = Icd_EnumerateInstanceLayerProperties,
};
