/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    VulkanTriangle - renders one triangle through the AROS Vulkan loader,
    checks every pixel against the exact coverage, shows the image in an
    Intuition window and reads the window back.

    The whole cycle runs three times: the first two fill the driver's and
    the pixel transfer's caches, the third must return every byte it takes.
    The result is one line:

        [W-GFX] native device=<name> api=<v> pixels=4096 mismatches=0
                window=1 sync=1 leak=0 PASS
*/

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>
#include <cybergraphx/cybergraphics.h>
#include <intuition/intuition.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <stdio.h>
#include <string.h>

#include "VulkanTriangle_spv.h"

/* The loader's root entry point; everything else is the driver's own */
void *vkGetInstanceProcAddr(void *instance, const char *pName);

#define INSTANCE_FUNCS(X) \
    X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) \
    X(vkGetPhysicalDeviceProperties) X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetPhysicalDeviceMemoryProperties) X(vkCreateDevice) X(vkGetDeviceProcAddr)

#define DEVICE_FUNCS(X) \
    X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkDeviceWaitIdle) \
    X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) \
    X(vkAllocateMemory) X(vkFreeMemory) X(vkBindImageMemory) X(vkMapMemory) X(vkUnmapMemory) \
    X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) \
    X(vkCreateImageView) X(vkDestroyImageView) \
    X(vkCreateShaderModule) X(vkDestroyShaderModule) \
    X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) \
    X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) \
    X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) \
    X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkCmdPipelineBarrier2) \
    X(vkCmdBeginRendering) X(vkCmdEndRendering) X(vkCmdBindPipeline) X(vkCmdDraw) \
    X(vkCmdCopyImageToBuffer) \
    X(vkCreateFence) X(vkDestroyFence) X(vkWaitForFences) X(vkGetFenceStatus) \
    X(vkCreateSemaphore) X(vkDestroySemaphore) X(vkWaitSemaphores) \
    X(vkGetSemaphoreCounterValue) X(vkQueueSubmit2)

#define DECLARE(f) static PFN_##f f;
static PFN_vkCreateInstance vkCreateInstance;
INSTANCE_FUNCS(DECLARE)
DEVICE_FUNCS(DECLARE)

static BOOL load_instance(VkInstance instance)
{
    BOOL ok = TRUE;
#define LOAD(f) ok &= (f = (PFN_##f)vkGetInstanceProcAddr(instance, #f)) != NULL;
    INSTANCE_FUNCS(LOAD)
#undef LOAD
    return ok;
}

static BOOL load_device(VkDevice device)
{
    BOOL ok = TRUE;
#define LOAD(f) ok &= (f = (PFN_##f)vkGetDeviceProcAddr(device, #f)) != NULL;
    DEVICE_FUNCS(LOAD)
#undef LOAD
    return ok;
}

/* LLVM compiles the pipeline on this task's stack */
ULONG __stack = 1024 * 1024;

#define SIZE   64
#define PIXELS (SIZE * SIZE)

struct cycle
{
    char        device[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    uint32_t    api;
    ULONG       mismatches;
    ULONG       window_mismatches;
    BOOL        window;
    BOOL        sync;
};

static ULONG expected(int x, int y)
{
    /* Pixel centre inside the triangle (8.25,8.25) (56.25,8.25) (8.25,56.25) */
    if (x >= 8 && y >= 8 && x + y <= 63)
        return 0xff0000ffUL;    /* R=255 G=0 B=0 A=255, little-endian RGBA */
    return 0xffff0000UL;        /* the clear colour R=0 G=0 B=255 A=255 */
}

static int32_t memory_type(VkPhysicalDevice pd, uint32_t bits, VkMemoryPropertyFlags want)
{
    VkPhysicalDeviceMemoryProperties props;
    uint32_t i;

    vkGetPhysicalDeviceMemoryProperties(pd, &props);
    for (i = 0; i < props.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & want) == want)
            return i;
    return -1;
}

#define CHECK(call) do { VkResult r_ = (call); if (r_ != VK_SUCCESS) { \
    printf("[W-GFX] %s failed: %d\n", #call, (int)r_); goto out; } } while (0)

static BOOL show_in_window(const ULONG *pixels, struct cycle *c)
{
    static ULONG back[PIXELS];
    struct Window *win;
    ULONG i;

    win = OpenWindowTags(NULL,
                         WA_Title, (IPTR)"VulkanTriangle",
                         WA_InnerWidth, SIZE, WA_InnerHeight, SIZE,
                         WA_Activate, FALSE,
                         WA_DragBar, TRUE,
                         WA_GimmeZeroZero, TRUE,
                         TAG_DONE);
    if (!win)
        return FALSE;

    WritePixelArray((APTR)pixels, 0, 0, SIZE * 4, win->RPort, 0, 0, SIZE, SIZE, RECTFMT_RGBA);
    memset(back, 0, sizeof(back));
    ReadPixelArray(back, 0, 0, SIZE * 4, win->RPort, 0, 0, SIZE, SIZE, RECTFMT_RGBA);
    c->window_mismatches = 0;
    /* The screen keeps colour only: compare red, green and blue */
    for (i = 0; i < PIXELS; i++)
        if ((back[i] ^ pixels[i]) & 0x00ffffffUL)
            c->window_mismatches++;
    CloseWindow(win);
    return TRUE;
}

static BOOL run_cycle(struct cycle *c)
{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice pd = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory image_mem = VK_NULL_HANDLE, buffer_mem = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore timeline = VK_NULL_HANDLE;
    VkMemoryRequirements req;
    VkPhysicalDeviceProperties props;
    uint32_t count = 1, family = 0, families = 8, i;
    VkQueueFamilyProperties qf[8];
    const ULONG *pixels;
    uint64_t value = 0;
    void *map = NULL;
    BOOL ok = FALSE, device_loaded = FALSE, instance_loaded = FALSE;
    int x, y;

    {
        VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                  .pApplicationName = "VulkanTriangle",
                                  .apiVersion = VK_API_VERSION_1_3 };
        VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                     .pApplicationInfo = &app };
        vkCreateInstance = (PFN_vkCreateInstance)vkGetInstanceProcAddr(NULL, "vkCreateInstance");
        if (!vkCreateInstance)
            goto out;
        CHECK(vkCreateInstance(&ici, NULL, &instance));
        instance_loaded = load_instance(instance);
        if (!instance_loaded)
        {
            printf("[W-GFX] instance functions missing\n");
            goto out;
        }
    }
    CHECK(vkEnumeratePhysicalDevices(instance, &count, &pd));
    if (!count)
    {
        printf("[W-GFX] no physical device\n");
        goto out;
    }
    vkGetPhysicalDeviceProperties(pd, &props);
    strncpy(c->device, props.deviceName, sizeof(c->device) - 1);
    c->api = props.apiVersion;

    vkGetPhysicalDeviceQueueFamilyProperties(pd, &families, qf);
    for (i = 0; i < families; i++)
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            break;
    if (i == families)
    {
        printf("[W-GFX] no graphics queue\n");
        goto out;
    }
    family = i;

    {
        float priority = 1.0f;
        VkPhysicalDeviceVulkan12Features f12 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
            .timelineSemaphore = VK_TRUE };
        VkPhysicalDeviceVulkan13Features f13 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
            .pNext = &f12, .dynamicRendering = VK_TRUE, .synchronization2 = VK_TRUE };
        VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                        .queueFamilyIndex = family, .queueCount = 1,
                                        .pQueuePriorities = &priority };
        VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                   .pNext = &f13, .queueCreateInfoCount = 1,
                                   .pQueueCreateInfos = &qci };
        CHECK(vkCreateDevice(pd, &dci, NULL, &device));
        device_loaded = load_device(device);
        if (!device_loaded)
        {
            printf("[W-GFX] device functions missing\n");
            goto out;
        }
    }
    vkGetDeviceQueue(device, family, 0, &queue);

    /* The colour attachment and the host-visible readback buffer */
    {
        VkImageCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
            .extent = { SIZE, SIZE, 1 }, .mipLevels = 1, .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
        VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        int32_t type;

        CHECK(vkCreateImage(device, &ii, NULL, &image));
        vkGetImageMemoryRequirements(device, image, &req);
        type = memory_type(pd, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (type < 0)
            goto out;
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = type;
        CHECK(vkAllocateMemory(device, &mai, NULL, &image_mem));
        CHECK(vkBindImageMemory(device, image, image_mem, 0));
    }
    {
        VkBufferCreateInfo bi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = PIXELS * 4, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT };
        VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        int32_t type;

        CHECK(vkCreateBuffer(device, &bi, NULL, &buffer));
        vkGetBufferMemoryRequirements(device, buffer, &req);
        type = memory_type(pd, req.memoryTypeBits,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (type < 0)
            goto out;
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = type;
        CHECK(vkAllocateMemory(device, &mai, NULL, &buffer_mem));
        CHECK(vkBindBufferMemory(device, buffer, buffer_mem, 0));
    }
    {
        VkImageViewCreateInfo vi = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image, .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
        CHECK(vkCreateImageView(device, &vi, NULL, &view));
    }

    /* The pipeline: dynamic rendering, no vertex input */
    {
        VkShaderModuleCreateInfo smi = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        VkPipelineLayoutCreateInfo pli = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
        VkPipelineRenderingCreateInfo pri = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1, .pColorAttachmentFormats = &format };
        VkPipelineShaderStageCreateInfo stages[2] = {
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_VERTEX_BIT, .pName = "main" },
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .pName = "main" } };
        VkPipelineVertexInputStateCreateInfo vin = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        VkPipelineInputAssemblyStateCreateInfo ia = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
        VkViewport vp = { 0, 0, SIZE, SIZE, 0, 1 };
        VkRect2D sc = { { 0, 0 }, { SIZE, SIZE } };
        VkPipelineViewportStateCreateInfo vps = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1, .pViewports = &vp, .scissorCount = 1, .pScissors = &sc };
        VkPipelineRasterizationStateCreateInfo rs = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_CLOCKWISE, .lineWidth = 1.0f };
        VkPipelineMultisampleStateCreateInfo ms = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
        VkPipelineColorBlendAttachmentState cba = { .colorWriteMask = 0xf };
        VkPipelineColorBlendStateCreateInfo cb = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1, .pAttachments = &cba };
        VkGraphicsPipelineCreateInfo gpi = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &pri,
            .stageCount = 2, .pStages = stages, .pVertexInputState = &vin,
            .pInputAssemblyState = &ia, .pViewportState = &vps, .pRasterizationState = &rs,
            .pMultisampleState = &ms, .pColorBlendState = &cb };

        smi.codeSize = sizeof(vulkan_triangle_vert);
        smi.pCode = vulkan_triangle_vert;
        CHECK(vkCreateShaderModule(device, &smi, NULL, &vert));
        smi.codeSize = sizeof(vulkan_triangle_frag);
        smi.pCode = vulkan_triangle_frag;
        CHECK(vkCreateShaderModule(device, &smi, NULL, &frag));
        stages[0].module = vert;
        stages[1].module = frag;
        CHECK(vkCreatePipelineLayout(device, &pli, NULL, &layout));
        gpi.layout = layout;
        CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpi, NULL, &pipeline));
    }

    /* Record: clear, draw, copy to the readback buffer */
    {
        VkCommandPoolCreateInfo cpi = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                        .queueFamilyIndex = family };
        VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                            .commandBufferCount = 1 };
        VkCommandBufferBeginInfo cbi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                         .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
        VkImageMemoryBarrier2 to_attachment = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .image = image, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
        VkImageMemoryBarrier2 to_transfer = to_attachment;
        VkDependencyInfo dep = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                 .imageMemoryBarrierCount = 1 };
        VkRenderingAttachmentInfo att = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = view, .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue.color.float32 = { 0.0f, 0.0f, 1.0f, 1.0f } };
        VkRenderingInfo ri = { .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = { { 0, 0 }, { SIZE, SIZE } }, .layerCount = 1,
            .colorAttachmentCount = 1, .pColorAttachments = &att };
        VkBufferImageCopy region = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                                     .imageExtent = { SIZE, SIZE, 1 } };

        CHECK(vkCreateCommandPool(device, &cpi, NULL, &pool));
        cai.commandPool = pool;
        CHECK(vkAllocateCommandBuffers(device, &cai, &cmd));
        CHECK(vkBeginCommandBuffer(cmd, &cbi));
        dep.pImageMemoryBarriers = &to_attachment;
        vkCmdPipelineBarrier2(cmd, &dep);
        vkCmdBeginRendering(cmd, &ri);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRendering(cmd);
        to_transfer.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        to_transfer.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        to_transfer.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        to_transfer.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        to_transfer.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        dep.pImageMemoryBarriers = &to_transfer;
        vkCmdPipelineBarrier2(cmd, &dep);
        vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &region);
        CHECK(vkEndCommandBuffer(cmd));
    }

    /* Submit with a fence and a timeline signal; the host waits for both */
    {
        VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        VkSemaphoreTypeCreateInfo sti = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
                                          .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE };
        VkSemaphoreCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                                      .pNext = &sti };
        VkCommandBufferSubmitInfo cbsi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                                           .commandBuffer = cmd };
        VkSemaphoreSubmitInfo signal = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                         .value = 7,
                                         .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT };
        VkSubmitInfo2 si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                             .commandBufferInfoCount = 1, .pCommandBufferInfos = &cbsi,
                             .signalSemaphoreInfoCount = 1, .pSignalSemaphoreInfos = &signal };
        VkSemaphoreWaitInfo wi = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
                                   .semaphoreCount = 1, .pSemaphores = &timeline };
        uint64_t seven = 7;

        CHECK(vkCreateFence(device, &fci, NULL, &fence));
        CHECK(vkCreateSemaphore(device, &sci, NULL, &timeline));
        signal.semaphore = timeline;
        wi.pValues = &seven;
        CHECK(vkQueueSubmit2(queue, 1, &si, fence));
        CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, 10ULL * 1000 * 1000 * 1000));
        CHECK(vkWaitSemaphores(device, &wi, 10ULL * 1000 * 1000 * 1000));
        CHECK(vkGetSemaphoreCounterValue(device, timeline, &value));
        c->sync = (value == 7 && vkGetFenceStatus(device, fence) == VK_SUCCESS);
    }

    CHECK(vkMapMemory(device, buffer_mem, 0, PIXELS * 4, 0, &map));
    pixels = map;
    c->mismatches = 0;
    for (y = 0; y < SIZE; y++)
        for (x = 0; x < SIZE; x++)
            if (pixels[y * SIZE + x] != expected(x, y))
            {
                if (c->mismatches < 4)
                    printf("[W-GFX] pixel %d,%d = %08lx, expected %08lx\n", x, y,
                           (unsigned long)pixels[y * SIZE + x], (unsigned long)expected(x, y));
                c->mismatches++;
            }
    c->window = show_in_window(pixels, c);
    vkUnmapMemory(device, buffer_mem);
    ok = TRUE;

out:
    if (device && device_loaded)
    {
        vkDeviceWaitIdle(device);
        vkDestroySemaphore(device, timeline, NULL);
        vkDestroyFence(device, fence, NULL);
        vkDestroyCommandPool(device, pool, NULL);
        vkDestroyPipeline(device, pipeline, NULL);
        vkDestroyPipelineLayout(device, layout, NULL);
        vkDestroyShaderModule(device, frag, NULL);
        vkDestroyShaderModule(device, vert, NULL);
        vkDestroyImageView(device, view, NULL);
        vkDestroyBuffer(device, buffer, NULL);
        vkFreeMemory(device, buffer_mem, NULL);
        vkDestroyImage(device, image, NULL);
        vkFreeMemory(device, image_mem, NULL);
        vkDestroyDevice(device, NULL);
    }
    if (instance && instance_loaded)
        vkDestroyInstance(instance, NULL);
    return ok;
}

int main(void)
{
    struct cycle cycles[3] = { { 0 } };
    ULONG before = 0, after;
    ULONG mismatches = 0, window_mismatches = 0;
    BOOL window = TRUE, sync = TRUE, pass;
    LONG leak;
    int i;

    /* Two cycles fill the driver's and the pixel transfer's caches; the
       third must give back everything it takes */
    for (i = 0; i < 3; i++)
    {
        if (i == 2)
            before = AvailMem(MEMF_ANY);
        if (!run_cycle(&cycles[i]))
        {
            printf("[W-GFX] native cycle %d FAIL\n", i + 1);
            return RETURN_FAIL;
        }
        mismatches += cycles[i].mismatches;
        window_mismatches += cycles[i].window_mismatches;
        window = window && cycles[i].window;
        sync = sync && cycles[i].sync;
    }
    after = AvailMem(MEMF_ANY);
    leak = (LONG)(before - after);

    pass = mismatches == 0 && window && window_mismatches == 0 && sync && leak <= 0;
    printf("[W-GFX] native device=%s api=%lu.%lu.%lu pixels=%d mismatches=%lu window=%d "
           "window_mismatches=%lu sync=%d leak=%ld %s\n",
           cycles[2].device,
           (unsigned long)VK_API_VERSION_MAJOR(cycles[2].api),
           (unsigned long)VK_API_VERSION_MINOR(cycles[2].api),
           (unsigned long)VK_API_VERSION_PATCH(cycles[2].api),
           PIXELS, (unsigned long)mismatches, window,
           (unsigned long)window_mismatches, sync, (long)leak, pass ? "PASS" : "FAIL");
    return pass ? RETURN_OK : RETURN_FAIL;
}
