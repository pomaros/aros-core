/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    lavapipe.hidd - Mesa's software Vulkan driver behind vulkan.hidd.

    The module registers one software GPU with vulkan.hidd. Its callbacks
    resolve every function through lavapipe's own ICD entry points, so the
    loader hands applications the driver's functions with their exact
    semantics once an instance exists.
*/

#include <aros/symbolsets.h>
#include <aros/debug.h>
#include <hidd/vulkan_hidd.h>
#include <proto/exec.h>
#include <proto/oop.h>
#include <proto/vulkan.h>

#include <vulkan/vulkan_core.h>

#include "vk_instance.h"
#include "vk_physical_device.h"
#include "vk_device.h"

#include "lavapipe_intern.h"

#include LC_LIBDEFS_FILE

PUBLIC VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName);

static PFN_vkVoidFunction lvp_global(const char *name)
{
    return vk_icdGetInstanceProcAddr(VK_NULL_HANDLE, name);
}

static PFN_vkVoidFunction lvp_physical(APTR physDev, const char *name)
{
    VK_FROM_HANDLE(vk_physical_device, pdevice, (VkPhysicalDevice)physDev);

    return vk_icdGetInstanceProcAddr(vk_instance_to_handle(pdevice->instance), name);
}

static PFN_vkVoidFunction lvp_device(APTR device, const char *name)
{
    VK_FROM_HANDLE(vk_device, dev, (VkDevice)device);

    return vk_device_get_proc_addr(dev, name);
}

static APTR Lvp_GetInstanceProcAddr(APTR instance, const char *pName, APTR dd)
{
    return (APTR)vk_icdGetInstanceProcAddr((VkInstance)instance, pName);
}

static APTR Lvp_GetDeviceProcAddr(APTR device, const char *pName, APTR dd)
{
    return (APTR)lvp_device(device, pName);
}

static APTR Lvp_CreateInstance(const void *createInfo, APTR dd)
{
    static const VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .apiVersion = VK_API_VERSION_1_3,
    };
    const VkInstanceCreateInfo fallback = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app,
    };
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)lvp_global("vkCreateInstance");
    VkInstance instance = VK_NULL_HANDLE;

    if (!create)
        return NULL;
    if (create(createInfo ? createInfo : &fallback, NULL, &instance) != VK_SUCCESS)
        return NULL;
    return instance;
}

static void Lvp_DestroyInstance(APTR instance, APTR dd)
{
    PFN_vkDestroyInstance destroy =
        (PFN_vkDestroyInstance)vk_icdGetInstanceProcAddr((VkInstance)instance, "vkDestroyInstance");

    if (destroy)
        destroy((VkInstance)instance, NULL);
}

static int32_t Lvp_EnumeratePhysicalDevices(APTR instance, ULONG *pCount,
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

static void Lvp_GetPhysicalDeviceProperties(APTR physDev, APTR pProperties, APTR dd)
{
    ((PFN_vkGetPhysicalDeviceProperties)lvp_physical(physDev, "vkGetPhysicalDeviceProperties"))
        ((VkPhysicalDevice)physDev, pProperties);
}

static void Lvp_GetPhysicalDeviceProperties2(APTR physDev, APTR pProperties, APTR dd)
{
    ((PFN_vkGetPhysicalDeviceProperties2)lvp_physical(physDev, "vkGetPhysicalDeviceProperties2"))
        ((VkPhysicalDevice)physDev, pProperties);
}

static void Lvp_GetPhysicalDeviceFeatures(APTR physDev, APTR pFeatures, APTR dd)
{
    ((PFN_vkGetPhysicalDeviceFeatures)lvp_physical(physDev, "vkGetPhysicalDeviceFeatures"))
        ((VkPhysicalDevice)physDev, pFeatures);
}

static void Lvp_GetPhysicalDeviceFeatures2(APTR physDev, APTR pFeatures, APTR dd)
{
    ((PFN_vkGetPhysicalDeviceFeatures2)lvp_physical(physDev, "vkGetPhysicalDeviceFeatures2"))
        ((VkPhysicalDevice)physDev, pFeatures);
}

static void Lvp_GetPhysicalDeviceMemoryProperties(APTR physDev, APTR pMemProps, APTR dd)
{
    ((PFN_vkGetPhysicalDeviceMemoryProperties)lvp_physical(physDev, "vkGetPhysicalDeviceMemoryProperties"))
        ((VkPhysicalDevice)physDev, pMemProps);
}

static void Lvp_GetPhysicalDeviceQueueFamilyProperties(APTR physDev, ULONG *pCount,
                                                       APTR pProps, APTR dd)
{
    uint32_t count = pCount ? *pCount : 0;

    ((PFN_vkGetPhysicalDeviceQueueFamilyProperties)
        lvp_physical(physDev, "vkGetPhysicalDeviceQueueFamilyProperties"))
        ((VkPhysicalDevice)physDev, &count, pProps);
    if (pCount)
        *pCount = count;
}

static int32_t Lvp_EnumerateDeviceExtensionProperties(APTR physDev, const char *pLayerName,
                                                      ULONG *pCount, APTR pProps, APTR dd)
{
    uint32_t count = pCount ? *pCount : 0;
    VkResult result;

    result = ((PFN_vkEnumerateDeviceExtensionProperties)
        lvp_physical(physDev, "vkEnumerateDeviceExtensionProperties"))
        ((VkPhysicalDevice)physDev, pLayerName, &count, pProps);
    if (pCount)
        *pCount = count;
    return result;
}

static APTR Lvp_CreateDevice(APTR physDev, APTR pCreateInfo, APTR dd)
{
    VkDevice device = VK_NULL_HANDLE;

    if (((PFN_vkCreateDevice)lvp_physical(physDev, "vkCreateDevice"))
            ((VkPhysicalDevice)physDev, pCreateInfo, NULL, &device) != VK_SUCCESS)
        return NULL;
    return device;
}

static void Lvp_DestroyDevice(APTR device, APTR dd)
{
    ((PFN_vkDestroyDevice)lvp_device(device, "vkDestroyDevice"))((VkDevice)device, NULL);
}

static void Lvp_GetDeviceQueue(APTR device, ULONG family, ULONG index, APTR *pQueue, APTR dd)
{
    ((PFN_vkGetDeviceQueue)lvp_device(device, "vkGetDeviceQueue"))
        ((VkDevice)device, family, index, (VkQueue *)pQueue);
}

static int32_t Lvp_DeviceWaitIdle(APTR device, APTR dd)
{
    return ((PFN_vkDeviceWaitIdle)lvp_device(device, "vkDeviceWaitIdle"))((VkDevice)device);
}

static int32_t Lvp_EnumerateInstanceVersion(ULONG *pApiVersion, APTR dd)
{
    PFN_vkEnumerateInstanceVersion version =
        (PFN_vkEnumerateInstanceVersion)lvp_global("vkEnumerateInstanceVersion");
    uint32_t v = 0;
    VkResult result;

    if (!version)
        return VK_ERROR_INITIALIZATION_FAILED;
    result = version(&v);
    if (pApiVersion)
        *pApiVersion = v;
    return result;
}

static int32_t Lvp_EnumerateInstanceExtensionProperties(const char *pLayerName, ULONG *pCount,
                                                        APTR pProps, APTR dd)
{
    PFN_vkEnumerateInstanceExtensionProperties enumerate =
        (PFN_vkEnumerateInstanceExtensionProperties)
            lvp_global("vkEnumerateInstanceExtensionProperties");
    uint32_t count = pCount ? *pCount : 0;
    VkResult result;

    if (!enumerate)
        return VK_ERROR_INITIALIZATION_FAILED;
    result = enumerate(pLayerName, &count, pProps);
    if (pCount)
        *pCount = count;
    return result;
}

static int32_t Lvp_EnumerateInstanceLayerProperties(ULONG *pCount, APTR pProps, APTR dd)
{
    if (pCount)
        *pCount = 0;
    return VK_SUCCESS;
}

static const struct VulkanDriverCallbacks lavapipe_callbacks =
{
    .GetInstanceProcAddr                    = Lvp_GetInstanceProcAddr,
    .GetDeviceProcAddr                      = Lvp_GetDeviceProcAddr,
    .CreateInstance                         = Lvp_CreateInstance,
    .DestroyInstance                        = Lvp_DestroyInstance,
    .EnumeratePhysicalDevices               = Lvp_EnumeratePhysicalDevices,
    .GetPhysicalDeviceProperties            = Lvp_GetPhysicalDeviceProperties,
    .GetPhysicalDeviceProperties2           = Lvp_GetPhysicalDeviceProperties2,
    .GetPhysicalDeviceFeatures              = Lvp_GetPhysicalDeviceFeatures,
    .GetPhysicalDeviceFeatures2             = Lvp_GetPhysicalDeviceFeatures2,
    .GetPhysicalDeviceMemoryProperties      = Lvp_GetPhysicalDeviceMemoryProperties,
    .GetPhysicalDeviceQueueFamilyProperties = Lvp_GetPhysicalDeviceQueueFamilyProperties,
    .EnumerateDeviceExtensionProperties     = Lvp_EnumerateDeviceExtensionProperties,
    .CreateDevice                           = Lvp_CreateDevice,
    .DestroyDevice                          = Lvp_DestroyDevice,
    .GetDeviceQueue                         = Lvp_GetDeviceQueue,
    .DeviceWaitIdle                         = Lvp_DeviceWaitIdle,
    .EnumerateInstanceVersion               = Lvp_EnumerateInstanceVersion,
    .EnumerateInstanceExtensionProperties   = Lvp_EnumerateInstanceExtensionProperties,
    .EnumerateInstanceLayerProperties       = Lvp_EnumerateInstanceLayerProperties,
};

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
                                           &lavapipe_callbacks, LIBBASE, 0, 0, 0);
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
