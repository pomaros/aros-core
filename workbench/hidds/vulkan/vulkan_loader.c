/*
    vulkan_loader.c — AROS Vulkan Loader (libvulkan.a)

    Provides standard Vulkan entry points (vk*) that dispatch through
    vulkan.hidd via OOP method calls.

    This is the AROS equivalent of the Khronos Vulkan Loader (libvulkan.so
    on Linux).  Any AROS application that uses Vulkan should link -lvulkan.
    The loader opens vulkan.hidd at first use, keeps it open while it is
    loaded, and routes all calls through
    VulkanDriverCallbacks to whichever driver is registered (venus.hidd,
    llvmpipe.hidd, etc.).

    GPU path for Venus (virgl/virtio-gpu):
        vk*() → OOP_DoMethod(vk_obj, ...)
              → vulkan.hidd → VulkanDriverCallbacks
              → venus.hidd → vtest socket
              → virglrenderer host → real GPU

    Copyright 2025-2026, The AROS Development Team. All rights reserved.
*/

#include <proto/oop.h>
#include <proto/exec.h>
#include <oop/oop.h>
#include <hidd/vulkan_hidd.h>
#include <stdint.h>
#include <string.h>

/* Keep this loader C-only and untyped at the public entry points.  The
 * Vulkan-Hpp runtime still needs the Vulkan 1.1 memory-properties wrapper,
 * so mirror only the structs needed to implement that entry point here. */
#define VK_LOADER_MAX_MEMORY_TYPES 32U
#define VK_LOADER_MAX_MEMORY_HEAPS 16U
#define VK_LOADER_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT 1000237000

typedef uint32_t VkLoaderStructureType;
typedef uint32_t VkLoaderMemoryPropertyFlags;
typedef uint32_t VkLoaderMemoryHeapFlags;
typedef uint64_t VkLoaderDeviceSize;

struct VkLoaderMemoryHeap {
    VkLoaderDeviceSize        size;
    VkLoaderMemoryHeapFlags   flags;
};

struct VkLoaderMemoryType {
    VkLoaderMemoryPropertyFlags propertyFlags;
    uint32_t                    heapIndex;
};

struct VkLoaderPhysicalDeviceMemoryProperties {
    uint32_t                    memoryTypeCount;
    struct VkLoaderMemoryType   memoryTypes[VK_LOADER_MAX_MEMORY_TYPES];
    uint32_t                    memoryHeapCount;
    struct VkLoaderMemoryHeap   memoryHeaps[VK_LOADER_MAX_MEMORY_HEAPS];
};

struct VkLoaderPhysicalDeviceMemoryProperties2 {
    VkLoaderStructureType                         sType;
    void                                         *pNext;
    struct VkLoaderPhysicalDeviceMemoryProperties memoryProperties;
};

struct VkLoaderBaseOutStructure {
    VkLoaderStructureType            sType;
    struct VkLoaderBaseOutStructure *pNext;
};

struct VkLoaderPhysicalDeviceMemoryBudgetPropertiesEXT {
    VkLoaderStructureType   sType;
    void                   *pNext;
    VkLoaderDeviceSize      heapBudget[VK_LOADER_MAX_MEMORY_HEAPS];
    VkLoaderDeviceSize      heapUsage[VK_LOADER_MAX_MEMORY_HEAPS];
};

/* ---- Global loader state ------------------------------------------ */

static struct Library *vk_hidd     = NULL;   /* vulkan.hidd, its class */
static OOP_Object   *vk_obj       = NULL;   /* HiddVulkan instance  */
static OOP_MethodID  vk_mbase     = 0;      /* IID_Hidd_Vulkan base */
static OOP_AttrBase  vk_abase     = 0;      /* IID_Hidd_Vulkan attributes */
static struct SignalSemaphore vk_init_lock;
static int           vk_lock_state = 0;     /* 0 none, 1 initialising, 2 ready */

/* ---- Loader-owned command trampolines ---------------------------- */

void vkCmdBindPipeline(void *commandBuffer, uint32_t pipelineBindPoint,
                       void *pipeline);
void vkCmdBindDescriptorSets(void *commandBuffer, uint32_t pipelineBindPoint,
                             void *layout, uint32_t firstSet,
                             uint32_t descriptorSetCount,
                             const void *const *pDescriptorSets,
                             uint32_t dynamicOffsetCount,
                             const uint32_t *pDynamicOffsets);
void vkCmdPushConstants(void *commandBuffer, void *layout,
                        uint32_t stageFlags, uint32_t offset,
                        uint32_t size, const void *pValues);
void vkCmdDispatch(void *commandBuffer, uint32_t groupCountX,
                   uint32_t groupCountY, uint32_t groupCountZ);
void vkCmdCopyBuffer(void *commandBuffer, void *srcBuffer, void *dstBuffer,
                     uint32_t regionCount, const void *pRegions);
void vkCmdFillBuffer(void *commandBuffer, void *dstBuffer,
                     uint64_t dstOffset, uint64_t size, uint32_t data);
void vkCmdPipelineBarrier(void *commandBuffer, uint32_t srcStageMask,
                          uint32_t dstStageMask, uint32_t dependencyFlags,
                          uint32_t memoryBarrierCount,
                          const void *pMemoryBarriers,
                          uint32_t bufferMemoryBarrierCount,
                          const void *pBufferMemoryBarriers,
                          uint32_t imageMemoryBarrierCount,
                          const void *pImageMemoryBarriers);
void vkCmdSetEvent(void *commandBuffer, void *event, uint32_t stageMask);
void vkCmdResetEvent(void *commandBuffer, void *event, uint32_t stageMask);
void vkCmdWaitEvents(void *commandBuffer, uint32_t eventCount,
                     const void *const *pEvents, uint32_t srcStageMask,
                     uint32_t dstStageMask, uint32_t memoryBarrierCount,
                     const void *pMemoryBarriers,
                     uint32_t bufferMemoryBarrierCount,
                     const void *pBufferMemoryBarriers,
                     uint32_t imageMemoryBarrierCount,
                     const void *pImageMemoryBarriers);
void vkCmdWriteTimestamp(void *commandBuffer, uint32_t pipelineStage,
                         void *queryPool, uint32_t query);
void vkCmdResetQueryPool(void *commandBuffer, void *queryPool,
                         uint32_t firstQuery, uint32_t queryCount);
int32_t vkEnumerateInstanceVersion(uint32_t *pApiVersion);
int32_t vkEnumerateInstanceExtensionProperties(const char *pLayerName,
                                               uint32_t *pPropertyCount,
                                               void *pProperties);
int32_t vkEnumerateInstanceLayerProperties(uint32_t *pPropertyCount,
                                           void *pProperties);
int32_t vkCreateInstance(const void *pCreateInfo, const void *pAllocator,
                         void **pInstance);
void vkDestroyInstance(void *instance, const void *pAllocator);
int32_t vkEnumeratePhysicalDevices(void *instance,
                                   uint32_t *pPhysicalDeviceCount,
                                   void **pPhysicalDevices);
void vkGetPhysicalDeviceProperties(void *physicalDevice, void *pProperties);
void vkGetPhysicalDeviceProperties2(void *physicalDevice, void *pProperties);
void vkGetPhysicalDeviceFeatures(void *physicalDevice, void *pFeatures);
void vkGetPhysicalDeviceFeatures2(void *physicalDevice, void *pFeatures);
void vkGetPhysicalDeviceMemoryProperties(void *physicalDevice,
                                         void *pMemoryProperties);
void vkGetPhysicalDeviceMemoryProperties2(void *physicalDevice,
                                          void *pMemoryProperties);
void vkGetPhysicalDeviceMemoryProperties2KHR(void *physicalDevice,
                                             void *pMemoryProperties);
void vkGetPhysicalDeviceQueueFamilyProperties(void *physicalDevice,
                                              uint32_t *pQueueFamilyPropertyCount,
                                              void *pQueueFamilyProperties);
int32_t vkEnumerateDeviceExtensionProperties(void *physicalDevice,
                                             const char *pLayerName,
                                             uint32_t *pPropertyCount,
                                             void *pProperties);
int32_t vkCreateDevice(void *physicalDevice, const void *pCreateInfo,
                       const void *pAllocator, void **pDevice);
void vkDestroyDevice(void *device, const void *pAllocator);
void vkGetDeviceQueue(void *device, uint32_t queueFamilyIndex,
                      uint32_t queueIndex, void **pQueue);
int32_t vkDeviceWaitIdle(void *device);
void *vkGetDeviceProcAddr(void *device, const char *pName);
void *vkGetInstanceProcAddr(void *instance, const char *pName);

static void *vk_loader_local_proc(const char *pName)
{
    if (!pName)
        return NULL;

#define VK_LOADER_ENTRY(name) \
    if (strcmp(pName, #name) == 0) return (void *)name

    VK_LOADER_ENTRY(vkGetInstanceProcAddr);
    VK_LOADER_ENTRY(vkGetDeviceProcAddr);
    VK_LOADER_ENTRY(vkEnumerateInstanceVersion);
    VK_LOADER_ENTRY(vkEnumerateInstanceExtensionProperties);
    VK_LOADER_ENTRY(vkEnumerateInstanceLayerProperties);
    VK_LOADER_ENTRY(vkCreateInstance);
    VK_LOADER_ENTRY(vkDestroyInstance);
    VK_LOADER_ENTRY(vkEnumeratePhysicalDevices);
    VK_LOADER_ENTRY(vkGetPhysicalDeviceProperties);
    VK_LOADER_ENTRY(vkGetPhysicalDeviceProperties2);
    VK_LOADER_ENTRY(vkGetPhysicalDeviceFeatures);
    VK_LOADER_ENTRY(vkGetPhysicalDeviceFeatures2);
    VK_LOADER_ENTRY(vkGetPhysicalDeviceMemoryProperties);
    VK_LOADER_ENTRY(vkGetPhysicalDeviceMemoryProperties2);
    VK_LOADER_ENTRY(vkGetPhysicalDeviceMemoryProperties2KHR);
    VK_LOADER_ENTRY(vkGetPhysicalDeviceQueueFamilyProperties);
    VK_LOADER_ENTRY(vkEnumerateDeviceExtensionProperties);
    VK_LOADER_ENTRY(vkCreateDevice);
    VK_LOADER_ENTRY(vkDestroyDevice);
    VK_LOADER_ENTRY(vkGetDeviceQueue);
    VK_LOADER_ENTRY(vkDeviceWaitIdle);

#undef VK_LOADER_ENTRY

    return NULL;
}

/* ---- One-time initialisation -------------------------------------- */

/*
 * The first call from any task creates the Vulkan object; an application's
 * threads may race to it (Dawn does). The object is published last, after
 * the IDs it is used with, so a task that sees it sees them. A failure is
 * not remembered: the next call tries again (a driver may appear later).
 */
/* Every use reads the object with acquire: a task whose own initialisation
   failed may see another task's later publication, and must see the IDs
   published before it (vk_loader_init). */
#define VK_OBJ() __atomic_load_n(&vk_obj, __ATOMIC_ACQUIRE)

static void vk_loader_init(void)
{
    OOP_AttrBase attrBase;
    OOP_Object  *obj;
    int          expected = 0;

    if (__atomic_load_n(&vk_obj, __ATOMIC_ACQUIRE))
        return;

    /* The lock itself, once: Forbid keeps this CPU's initialiser from being
       preempted while another CPU waits for it */
    Forbid();
    if (__atomic_compare_exchange_n(&vk_lock_state, &expected, 1, 0,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
        InitSemaphore(&vk_init_lock);
        __atomic_store_n(&vk_lock_state, 2, __ATOMIC_RELEASE);
    }
    Permit();
    while (__atomic_load_n(&vk_lock_state, __ATOMIC_ACQUIRE) != 2)
        ;

    ObtainSemaphore(&vk_init_lock);
    /* The class exists only while vulkan.hidd is open: an application that
       loads the loader by itself (Dawn, wgpu) is the only one to open it */
    if (!vk_hidd)
        vk_hidd = OpenLibrary("vulkan.hidd", 0);
    if (vk_hidd && !__atomic_load_n(&vk_obj, __ATOMIC_ACQUIRE) &&
        (attrBase = OOP_ObtainAttrBase(IID_Hidd_Vulkan)) != 0)
    {
        struct TagItem tags[] = {
            { attrBase + aoHidd_Vulkan_InterfaceVersion, VULKAN_INTERFACE_VERSION },
            { TAG_DONE, 0 }
        };

        obj = OOP_NewObject(NULL, CLID_Hidd_Vulkan, tags);
        if (obj)
        {
            vk_abase = attrBase;
            vk_mbase = OOP_GetMethodID(IID_Hidd_Vulkan, 0);
            __atomic_store_n(&vk_obj, obj, __ATOMIC_RELEASE);
        }
        else
            OOP_ReleaseAttrBase(IID_Hidd_Vulkan);
    }
    ReleaseSemaphore(&vk_init_lock);
}

/* When the module is unloaded, or the program that links the loader ends */
__attribute__((destructor)) static void vk_loader_exit(void)
{
    if (vk_obj)
    {
        OOP_DisposeObject(vk_obj);
        vk_obj = NULL;
        OOP_ReleaseAttrBase(IID_Hidd_Vulkan);
    }
    if (vk_hidd)
    {
        CloseLibrary(vk_hidd);
        vk_hidd = NULL;
    }
}

/* ---- Root Vulkan entry points ------------------------------------- */

/*
 * vkGetInstanceProcAddr — THE root Vulkan entry point.
 * ggml-vulkan (and Vulkan-Hpp DispatchLoaderDynamic) bootstraps all
 * other function pointers through this call.
 */
/*
 * Global functions (no instance yet) are the loader's own: they find the
 * drivers and pass the application's create info. Once an instance or a
 * device exists, its functions are the driver's own, as an ICD loader
 * hands them out: an application that drives Vulkan fully (wgpu, Dawn,
 * ANGLE) needs every instance, physical-device and device function, with
 * the exact pNext semantics. The loader's wrappers answer only what the
 * driver does not.
 */
void *vkGetInstanceProcAddr(void *instance, const char *pName)
{
    void *proc;

    if (!instance)
    {
        proc = vk_loader_local_proc(pName);
        if (proc)
            return proc;
    }

    vk_loader_init();
    if (!VK_OBJ())
        return instance ? vk_loader_local_proc(pName) : NULL;

    struct pHidd_Vulkan_GetInstanceProcAddr msg = {
        .mID      = vk_mbase + moHidd_Vulkan_GetInstanceProcAddr,
        .instance = instance,
        .pName    = pName,
    };
    proc = (void *)OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
    return proc ? proc : vk_loader_local_proc(pName);
}

void *vkGetDeviceProcAddr(void *device, const char *pName)
{
    void *proc;

    vk_loader_init();
    if (!VK_OBJ())
        return vk_loader_local_proc(pName);

    struct pHidd_Vulkan_GetDeviceProcAddr msg = {
        .mID    = vk_mbase + moHidd_Vulkan_GetDeviceProcAddr,
        .device = device,
        .pName  = pName,
    };
    proc = (void *)OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
    return proc ? proc : vk_loader_local_proc(pName);
}

/* ---- Instance/device bootstrap entry points ---------------------- */

int32_t vkEnumerateInstanceVersion(uint32_t *pApiVersion)
{
    ULONG apiVersion;
    int32_t result;

    vk_loader_init();
    if (!VK_OBJ()) {
        if (pApiVersion)
            *pApiVersion = VULKAN_LOADER_API_VERSION;
        return 0;
    }

    apiVersion = pApiVersion ? *pApiVersion : 0;
    struct pHidd_Vulkan_EnumerateInstanceVersion msg = {
        .mID         = vk_mbase + moHidd_Vulkan_EnumerateInstanceVersion,
        .pApiVersion = &apiVersion,
    };
    result = (int32_t)OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
    if (pApiVersion)
        *pApiVersion = (uint32_t)apiVersion;
    return result;
}

int32_t vkEnumerateInstanceExtensionProperties(const char *pLayerName,
                                               uint32_t *pPropertyCount,
                                               void *pProperties)
{
    ULONG propertyCount;
    int32_t result;

    vk_loader_init();
    if (!VK_OBJ()) {
        if (pPropertyCount)
            *pPropertyCount = 0;
        return 0;
    }

    propertyCount = pPropertyCount ? *pPropertyCount : 0;
    struct pHidd_Vulkan_EnumerateInstanceExtensionProperties msg = {
        .mID            = vk_mbase + moHidd_Vulkan_EnumerateInstanceExtensionProperties,
        .pLayerName     = pLayerName,
        .pPropertyCount = &propertyCount,
        .pProperties    = pProperties,
    };
    result = (int32_t)OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
    if (pPropertyCount)
        *pPropertyCount = (uint32_t)propertyCount;
    return result;
}

int32_t vkEnumerateInstanceLayerProperties(uint32_t *pPropertyCount,
                                           void *pProperties)
{
    (void)pProperties;
    if (pPropertyCount)
        *pPropertyCount = 0;
    return 0;
}

int32_t vkCreateInstance(const void *pCreateInfo, const void *pAllocator,
                         void **pInstance)
{
    void *instance;

    (void)pAllocator;

    vk_loader_init();
    if (!VK_OBJ())
        return -3; /* VK_ERROR_INITIALIZATION_FAILED */

    /* The driver gets the application's API version and extensions */
    struct pHidd_Vulkan_CreateInstance msg = {
        .mID        = vk_mbase + moHidd_Vulkan_CreateInstance,
        .createInfo = pCreateInfo,
    };
    instance = (void *)OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
    if (pInstance)
        *pInstance = instance;
    if (!instance)
    {
        IPTR gpus = 0;

        /* No driver at all is VK_ERROR_INCOMPATIBLE_DRIVER, as an ICD
           loader reports it; a driver that fails is a failed initialisation */
        OOP_GetAttr(VK_OBJ(), vk_abase + aoHidd_Vulkan_GPUCount, &gpus);
        return gpus ? -3 : -9;
    }
    return 0;
}

void vkDestroyInstance(void *instance, const void *pAllocator)
{
    (void)pAllocator;

    vk_loader_init();
    if (!VK_OBJ())
        return;

    struct pHidd_Vulkan_DestroyInstance msg = {
        .mID      = vk_mbase + moHidd_Vulkan_DestroyInstance,
        .instance = instance,
    };
    OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
}

int32_t vkEnumeratePhysicalDevices(void *instance,
                                   uint32_t *pPhysicalDeviceCount,
                                   void **pPhysicalDevices)
{
    ULONG physicalDeviceCount;
    int32_t result;

    vk_loader_init();
    if (!VK_OBJ()) {
        if (pPhysicalDeviceCount)
            *pPhysicalDeviceCount = 0;
        return -3;
    }

    physicalDeviceCount = pPhysicalDeviceCount ? *pPhysicalDeviceCount : 0;
    struct pHidd_Vulkan_EnumeratePhysicalDevices msg = {
        .mID                  = vk_mbase + moHidd_Vulkan_EnumeratePhysicalDevices,
        .instance             = instance,
        .pPhysicalDeviceCount = &physicalDeviceCount,
        .pPhysicalDevices     = (APTR *)pPhysicalDevices,
    };
    result = (int32_t)OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
    if (pPhysicalDeviceCount)
        *pPhysicalDeviceCount = (uint32_t)physicalDeviceCount;
    return result;
}

void vkGetPhysicalDeviceProperties(void *physicalDevice, void *pProperties)
{
    vk_loader_init();
    if (!VK_OBJ())
        return;

    struct pHidd_Vulkan_GetPhysicalDeviceProperties msg = {
        .mID            = vk_mbase + moHidd_Vulkan_GetPhysicalDeviceProperties,
        .physicalDevice = physicalDevice,
        .pProperties    = pProperties,
    };
    OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
}

void vkGetPhysicalDeviceProperties2(void *physicalDevice, void *pProperties)
{
    vk_loader_init();
    if (!VK_OBJ())
        return;

    struct pHidd_Vulkan_GetPhysicalDeviceProperties2 msg = {
        .mID            = vk_mbase + moHidd_Vulkan_GetPhysicalDeviceProperties2,
        .physicalDevice = physicalDevice,
        .pProperties    = pProperties,
    };
    OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
}

void vkGetPhysicalDeviceFeatures(void *physicalDevice, void *pFeatures)
{
    vk_loader_init();
    if (!VK_OBJ())
        return;

    struct pHidd_Vulkan_GetPhysicalDeviceFeatures msg = {
        .mID            = vk_mbase + moHidd_Vulkan_GetPhysicalDeviceFeatures,
        .physicalDevice = physicalDevice,
        .pFeatures      = pFeatures,
    };
    OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
}

void vkGetPhysicalDeviceFeatures2(void *physicalDevice, void *pFeatures)
{
    vk_loader_init();
    if (!VK_OBJ())
        return;

    struct pHidd_Vulkan_GetPhysicalDeviceFeatures2 msg = {
        .mID            = vk_mbase + moHidd_Vulkan_GetPhysicalDeviceFeatures2,
        .physicalDevice = physicalDevice,
        .pFeatures      = pFeatures,
    };
    OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
}

void vkGetPhysicalDeviceMemoryProperties(void *physicalDevice,
                                         void *pMemoryProperties)
{
    vk_loader_init();
    if (!VK_OBJ())
        return;

    struct pHidd_Vulkan_GetPhysicalDeviceMemoryProperties msg = {
        .mID               = vk_mbase + moHidd_Vulkan_GetPhysicalDeviceMemoryProperties,
        .physicalDevice    = physicalDevice,
        .pMemoryProperties = pMemoryProperties,
    };
    OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
}

void vkGetPhysicalDeviceMemoryProperties2(void *physicalDevice,
                                          void *pMemoryProperties)
{
    struct VkLoaderPhysicalDeviceMemoryProperties2 *props2;
    struct VkLoaderBaseOutStructure *next;

    if (!pMemoryProperties)
        return;

    props2 = (struct VkLoaderPhysicalDeviceMemoryProperties2 *)pMemoryProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &props2->memoryProperties);

    next = (struct VkLoaderBaseOutStructure *)props2->pNext;
    while (next) {
        if (next->sType == VK_LOADER_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT) {
            struct VkLoaderPhysicalDeviceMemoryBudgetPropertiesEXT *budget =
                (struct VkLoaderPhysicalDeviceMemoryBudgetPropertiesEXT *)next;
            uint32_t i;

            for (i = 0; i < VK_LOADER_MAX_MEMORY_HEAPS; ++i) {
                VkLoaderDeviceSize heap_size = 0;
                if (i < props2->memoryProperties.memoryHeapCount)
                    heap_size = props2->memoryProperties.memoryHeaps[i].size;
                budget->heapBudget[i] = heap_size;
                budget->heapUsage[i] = 0;
            }
        }
        next = next->pNext;
    }
}

void vkGetPhysicalDeviceMemoryProperties2KHR(void *physicalDevice,
                                             void *pMemoryProperties)
{
    vkGetPhysicalDeviceMemoryProperties2(physicalDevice, pMemoryProperties);
}

void vkGetPhysicalDeviceQueueFamilyProperties(void *physicalDevice,
                                              uint32_t *pQueueFamilyPropertyCount,
                                              void *pQueueFamilyProperties)
{
    ULONG queueFamilyPropertyCount;

    vk_loader_init();
    if (!VK_OBJ()) {
        if (pQueueFamilyPropertyCount)
            *pQueueFamilyPropertyCount = 0;
        return;
    }

    queueFamilyPropertyCount = pQueueFamilyPropertyCount ? *pQueueFamilyPropertyCount : 0;
    struct pHidd_Vulkan_GetPhysicalDeviceQueueFamilyProperties msg = {
        .mID                       = vk_mbase + moHidd_Vulkan_GetPhysicalDeviceQueueFamilyProperties,
        .physicalDevice            = physicalDevice,
        .pQueueFamilyPropertyCount = &queueFamilyPropertyCount,
        .pQueueFamilyProperties    = pQueueFamilyProperties,
    };
    OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
    if (pQueueFamilyPropertyCount)
        *pQueueFamilyPropertyCount = (uint32_t)queueFamilyPropertyCount;
}

int32_t vkEnumerateDeviceExtensionProperties(void *physicalDevice,
                                             const char *pLayerName,
                                             uint32_t *pPropertyCount,
                                             void *pProperties)
{
    ULONG propertyCount;
    int32_t result;

    vk_loader_init();
    if (!VK_OBJ()) {
        if (pPropertyCount)
            *pPropertyCount = 0;
        return -3;
    }

    propertyCount = pPropertyCount ? *pPropertyCount : 0;
    struct pHidd_Vulkan_EnumerateDeviceExtensionProperties msg = {
        .mID            = vk_mbase + moHidd_Vulkan_EnumerateDeviceExtensionProperties,
        .physicalDevice = physicalDevice,
        .pLayerName     = pLayerName,
        .pPropertyCount = &propertyCount,
        .pProperties    = pProperties,
    };
    result = (int32_t)OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
    if (pPropertyCount)
        *pPropertyCount = (uint32_t)propertyCount;
    return result;
}

int32_t vkCreateDevice(void *physicalDevice, const void *pCreateInfo,
                       const void *pAllocator, void **pDevice)
{
    void *device;

    (void)pAllocator;

    vk_loader_init();
    if (!VK_OBJ())
        return -3;

    struct pHidd_Vulkan_CreateDevice msg = {
        .mID            = vk_mbase + moHidd_Vulkan_CreateDevice,
        .physicalDevice = physicalDevice,
        .pCreateInfo    = (APTR)pCreateInfo,
    };
    device = (void *)OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
    if (pDevice)
        *pDevice = device;
    return device ? 0 : -3;
}

void vkDestroyDevice(void *device, const void *pAllocator)
{
    (void)pAllocator;

    vk_loader_init();
    if (!VK_OBJ())
        return;

    struct pHidd_Vulkan_DestroyDevice msg = {
        .mID    = vk_mbase + moHidd_Vulkan_DestroyDevice,
        .device = device,
    };
    OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
}

void vkGetDeviceQueue(void *device, uint32_t queueFamilyIndex,
                      uint32_t queueIndex, void **pQueue)
{
    vk_loader_init();
    if (!VK_OBJ()) {
        if (pQueue)
            *pQueue = NULL;
        return;
    }

    struct pHidd_Vulkan_GetDeviceQueue msg = {
        .mID              = vk_mbase + moHidd_Vulkan_GetDeviceQueue,
        .device           = device,
        .queueFamilyIndex = queueFamilyIndex,
        .queueIndex       = queueIndex,
        .pQueue           = (APTR *)pQueue,
    };
    OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
}

int32_t vkDeviceWaitIdle(void *device)
{
    vk_loader_init();
    if (!VK_OBJ())
        return -3;

    struct pHidd_Vulkan_DeviceWaitIdle msg = {
        .mID    = vk_mbase + moHidd_Vulkan_DeviceWaitIdle,
        .device = device,
    };
    return (int32_t)OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
}

/* ---- Direct entry points referenced by ggml-vulkan.cpp ----------- */

void vkCmdBindPipeline(void *commandBuffer, uint32_t pipelineBindPoint,
                       void *pipeline)
{
    vk_loader_init();
    if (!VK_OBJ())
        return;

    struct pHidd_Vulkan_CmdBindPipeline msg = {
        .mID               = vk_mbase + moHidd_Vulkan_CmdBindPipeline,
        .commandBuffer     = commandBuffer,
        .pipelineBindPoint = pipelineBindPoint,
        .pipeline          = pipeline,
    };
    OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
}

void vkCmdBindDescriptorSets(void *commandBuffer, uint32_t pipelineBindPoint,
                             void *layout, uint32_t firstSet,
                             uint32_t descriptorSetCount,
                             const void *const *pDescriptorSets,
                             uint32_t dynamicOffsetCount,
                             const uint32_t *pDynamicOffsets)
{
    vk_loader_init();
    if (!VK_OBJ())
        return;

    struct pHidd_Vulkan_CmdBindDescriptorSets msg = {
        .mID                = vk_mbase + moHidd_Vulkan_CmdBindDescriptorSets,
        .commandBuffer      = commandBuffer,
        .pipelineBindPoint  = pipelineBindPoint,
        .layout             = layout,
        .firstSet           = firstSet,
        .descriptorSetCount = descriptorSetCount,
        .pDescriptorSets    = (const APTR *)pDescriptorSets,
        .dynamicOffsetCount = dynamicOffsetCount,
        .pDynamicOffsets    = (const ULONG *)pDynamicOffsets,
    };
    OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
}

void vkCmdPushConstants(void *commandBuffer, void *layout,
                        uint32_t stageFlags, uint32_t offset,
                        uint32_t size, const void *pValues)
{
    vk_loader_init();
    if (!VK_OBJ())
        return;

    struct pHidd_Vulkan_CmdPushConstants msg = {
        .mID           = vk_mbase + moHidd_Vulkan_CmdPushConstants,
        .commandBuffer = commandBuffer,
        .layout        = layout,
        .stageFlags    = stageFlags,
        .offset        = offset,
        .size          = size,
        .pValues       = (APTR)pValues,
    };
    OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
}

void vkCmdDispatch(void *commandBuffer, uint32_t groupCountX,
                   uint32_t groupCountY, uint32_t groupCountZ)
{
    vk_loader_init();
    if (!VK_OBJ())
        return;

    struct pHidd_Vulkan_CmdDispatch msg = {
        .mID           = vk_mbase + moHidd_Vulkan_CmdDispatch,
        .commandBuffer = commandBuffer,
        .groupCountX   = groupCountX,
        .groupCountY   = groupCountY,
        .groupCountZ   = groupCountZ,
    };
    OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
}

/*
 * vkCmdCopyBuffer — called directly in ggml_vk_buffer_copy_async().
 */
void vkCmdCopyBuffer(void *commandBuffer, void *srcBuffer, void *dstBuffer,
                     uint32_t regionCount, const void *pRegions)
{
    vk_loader_init();
    if (!VK_OBJ())
        return;

    struct pHidd_Vulkan_CmdCopyBuffer msg = {
        .mID           = vk_mbase + moHidd_Vulkan_CmdCopyBuffer,
        .commandBuffer = commandBuffer,
        .srcBuffer     = srcBuffer,
        .dstBuffer     = dstBuffer,
        .regionCount   = regionCount,
        .pRegions      = (APTR)pRegions,
    };
    OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
}

void vkCmdFillBuffer(void *commandBuffer, void *dstBuffer,
                     uint64_t dstOffset, uint64_t size, uint32_t data)
{
    vk_loader_init();
    if (!VK_OBJ())
        return;

    struct pHidd_Vulkan_CmdFillBuffer msg = {
        .mID           = vk_mbase + moHidd_Vulkan_CmdFillBuffer,
        .commandBuffer = commandBuffer,
        .dstBuffer     = dstBuffer,
        .dstOffset     = dstOffset,
        .size          = size,
        .data          = data,
    };
    OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
}

void vkCmdPipelineBarrier(void *commandBuffer, uint32_t srcStageMask,
                          uint32_t dstStageMask, uint32_t dependencyFlags,
                          uint32_t memoryBarrierCount,
                          const void *pMemoryBarriers,
                          uint32_t bufferMemoryBarrierCount,
                          const void *pBufferMemoryBarriers,
                          uint32_t imageMemoryBarrierCount,
                          const void *pImageMemoryBarriers)
{
    vk_loader_init();
    if (!VK_OBJ())
        return;

    struct pHidd_Vulkan_CmdPipelineBarrier msg = {
        .mID                      = vk_mbase + moHidd_Vulkan_CmdPipelineBarrier,
        .commandBuffer            = commandBuffer,
        .srcStageMask             = srcStageMask,
        .dstStageMask             = dstStageMask,
        .dependencyFlags          = dependencyFlags,
        .memoryBarrierCount       = memoryBarrierCount,
        .pMemoryBarriers          = (APTR)pMemoryBarriers,
        .bufferMemoryBarrierCount = bufferMemoryBarrierCount,
        .pBufferMemoryBarriers    = (APTR)pBufferMemoryBarriers,
        .imageMemoryBarrierCount  = imageMemoryBarrierCount,
        .pImageMemoryBarriers     = (APTR)pImageMemoryBarriers,
    };
    OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
}

void vkCmdSetEvent(void *commandBuffer, void *event, uint32_t stageMask)
{
    vk_loader_init();
    if (!VK_OBJ())
        return;

    struct pHidd_Vulkan_CmdSetEvent msg = {
        .mID           = vk_mbase + moHidd_Vulkan_CmdSetEvent,
        .commandBuffer = commandBuffer,
        .event         = event,
        .stageMask     = stageMask,
    };
    OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
}

void vkCmdResetEvent(void *commandBuffer, void *event, uint32_t stageMask)
{
    vk_loader_init();
    if (!VK_OBJ())
        return;

    struct pHidd_Vulkan_CmdResetEvent msg = {
        .mID           = vk_mbase + moHidd_Vulkan_CmdResetEvent,
        .commandBuffer = commandBuffer,
        .event         = event,
        .stageMask     = stageMask,
    };
    OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
}

void vkCmdWaitEvents(void *commandBuffer, uint32_t eventCount,
                     const void *const *pEvents, uint32_t srcStageMask,
                     uint32_t dstStageMask, uint32_t memoryBarrierCount,
                     const void *pMemoryBarriers,
                     uint32_t bufferMemoryBarrierCount,
                     const void *pBufferMemoryBarriers,
                     uint32_t imageMemoryBarrierCount,
                     const void *pImageMemoryBarriers)
{
    vk_loader_init();
    if (!VK_OBJ())
        return;

    struct pHidd_Vulkan_CmdWaitEvents msg = {
        .mID                      = vk_mbase + moHidd_Vulkan_CmdWaitEvents,
        .commandBuffer            = commandBuffer,
        .eventCount               = eventCount,
        .pEvents                  = (const APTR *)pEvents,
        .srcStageMask             = srcStageMask,
        .dstStageMask             = dstStageMask,
        .memoryBarrierCount       = memoryBarrierCount,
        .pMemoryBarriers          = (APTR)pMemoryBarriers,
        .bufferMemoryBarrierCount = bufferMemoryBarrierCount,
        .pBufferMemoryBarriers    = (APTR)pBufferMemoryBarriers,
        .imageMemoryBarrierCount  = imageMemoryBarrierCount,
        .pImageMemoryBarriers     = (APTR)pImageMemoryBarriers,
    };
    OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
}

void vkCmdWriteTimestamp(void *commandBuffer, uint32_t pipelineStage,
                         void *queryPool, uint32_t query)
{
    vk_loader_init();
    if (!VK_OBJ())
        return;

    struct pHidd_Vulkan_CmdWriteTimestamp msg = {
        .mID           = vk_mbase + moHidd_Vulkan_CmdWriteTimestamp,
        .commandBuffer = commandBuffer,
        .pipelineStage = pipelineStage,
        .queryPool     = queryPool,
        .query         = query,
    };
    OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
}

void vkCmdResetQueryPool(void *commandBuffer, void *queryPool,
                         uint32_t firstQuery, uint32_t queryCount)
{
    vk_loader_init();
    if (!VK_OBJ())
        return;

    struct pHidd_Vulkan_CmdResetQueryPool msg = {
        .mID           = vk_mbase + moHidd_Vulkan_CmdResetQueryPool,
        .commandBuffer = commandBuffer,
        .queryPool     = queryPool,
        .firstQuery    = firstQuery,
        .queryCount    = queryCount,
    };
    OOP_DoMethod(VK_OBJ(), (OOP_Msg)&msg);
}
