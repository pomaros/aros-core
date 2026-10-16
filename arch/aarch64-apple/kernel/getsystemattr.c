/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Apple kernel system attributes.
 */

#include <aros/config.h>
#include <aros/kernel.h>
#include <aros/libcall.h>
#include <aros/apple/platformtimer.h>
#include <aros/apple/startup.h>


#include <kernel_base.h>
#include "kernel_intern.h"
#include "page_allocator.h"

#include "cpu_monitor.h"
#include <proto/kernel.h>

extern struct KrnPlatformTimer AROSApplePlatformTimer;

AROS_LH1(intptr_t, KrnGetSystemAttr,
    AROS_LHA(uint32_t, id, D0),
    struct KernelBase *, KernelBase, 29, Kernel)
{
    AROS_LIBFUNC_INIT

    switch (id)
    {
    case KATTR_PageProtect:
        return (intptr_t)(KRN_PageProtect)AROSApplePageProtect;
    case AROS_APPLE_KATTR_CPU_STATS:
    {
        const struct AROSAppleCPUStats *stats = AROSAppleCPUMonitorStats();
        return stats ? (intptr_t)stats : -1;
    }

    case KATTR_Architecture:
        return (intptr_t)AROS_ARCHITECTURE;

    case KATTR_ClockSource:
        if (KernelBase->kb_ClockSource)
            return (intptr_t)KernelBase->kb_ClockSource;
        return -1;

    case KATTR_PlatformTimer:
        if ((AROSApplePlatformTimer.kpt_Flags & KPTF_COUNTER_READY) != 0)
            return (intptr_t)&AROSApplePlatformTimer;
        return -1;

    case KATTR_FrameBuffer:
    case KATTR_FrameBufferWidth:
    case KATTR_FrameBufferHeight:
    case KATTR_FrameBufferDepth:
    case KATTR_FrameBufferPitch:
    case KATTR_FrameBufferFormat:
    case KATTR_FrameBufferSize:
    {
        struct AROSAppleFramebufferInfo framebuffer;

        if (!AROSAppleKernelRetainedFramebuffer(&framebuffer))
            return -1;
        switch (id)
        {
        case KATTR_FrameBuffer:
            return (intptr_t)framebuffer.physical_address;
        case KATTR_FrameBufferWidth:
            return (intptr_t)framebuffer.width;
        case KATTR_FrameBufferHeight:
            return (intptr_t)framebuffer.height;
        case KATTR_FrameBufferDepth:
            return (intptr_t)framebuffer.depth;
        case KATTR_FrameBufferPitch:
            return (intptr_t)framebuffer.pitch;
        case KATTR_FrameBufferFormat:
            return (intptr_t)framebuffer.format;
        default:
            return (intptr_t)framebuffer.size;
        }
    }

    case AROS_APPLE_KATTR_CPU_MIDR:
    case AROS_APPLE_KATTR_CURRENT_EL:
    case AROS_APPLE_KATTR_COUNTER_FREQUENCY:
    case AROS_APPLE_KATTR_BOARD_MODEL:
    case AROS_APPLE_KATTR_BOARD_COMPATIBLE:
    case AROS_APPLE_KATTR_KEYBOARD_ISO:
    case AROS_APPLE_KATTR_TRACKPAD_HID:
    case AROS_APPLE_KATTR_TRACKPAD_STATE:
    case AROS_APPLE_KATTR_SPI_STATS:
    case AROS_APPLE_KATTR_DESKTOP_AIC:
    case AROS_APPLE_KATTR_DESKTOP_NVME:
    case AROS_APPLE_KATTR_BOOT_SEED:
    case AROS_APPLE_KATTR_BATTERY:
    case AROS_APPLE_KATTR_SYSTEM_TIME:
    {
        intptr_t value;

        if (!AROSAppleKernelIdentity(id, &value))
            return -1;
        return value;
    }

    case AROS_APPLE_KATTR_BOOT_IMAGE_START:
    case AROS_APPLE_KATTR_BOOT_IMAGE_SIZE:
    {
        uintptr_t image_start;
        uint64_t image_size;

        if (!AROSAppleKernelRetainedBootImage(&image_start, &image_size))
            return -1;
        return id == AROS_APPLE_KATTR_BOOT_IMAGE_START ?
            (intptr_t)image_start : (intptr_t)image_size;
    }

    case AROS_APPLE_KATTR_PAYLOAD_IDENTITY:
    {
        static struct AROSApplePayloadIdentity identity;

        if (!AROSAppleKernelRetainedPayloadIdentity(&identity))
            return -1;
        return (intptr_t)&identity;
    }
    }

    return -1;

    AROS_LIBFUNC_EXIT
}
