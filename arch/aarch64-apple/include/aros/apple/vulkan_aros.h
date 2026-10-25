#ifndef AROS_APPLE_VULKAN_AROS_H
#define AROS_APPLE_VULKAN_AROS_H

/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: Vulkan surfaces on AROS windows (agxvulkan.hidd).

    vkCreateAROSWindowSurfaceAROS, which a program takes from
    vkGetInstanceProcAddr, makes a VkSurfaceKHR of a window's inner area;
    its swapchain's images are presented into the window. The instance has
    VK_KHR_surface and VK_EXT_headless_surface enabled (vulkan.hidd's loader
    passes no extension list); the device needs VK_KHR_swapchain in its
    VkDeviceCreateInfo.
*/

#include <vulkan/vulkan_core.h>

struct Window;

#define VK_AROS_WINDOW_SURFACE_EXTENSION_NAME "VK_AROS_window_surface"
/* An sType outside Khronos's ranges, for this structure only. */
#define VK_STRUCTURE_TYPE_AROS_WINDOW_SURFACE_CREATE_INFO_AROS ((VkStructureType)0x41524f53)

typedef struct VkAROSWindowSurfaceCreateInfoAROS {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    struct Window *window;
} VkAROSWindowSurfaceCreateInfoAROS;

typedef VkResult (VKAPI_PTR *PFN_vkCreateAROSWindowSurfaceAROS)(VkInstance instance,
    const VkAROSWindowSurfaceCreateInfoAROS *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface);

#endif
