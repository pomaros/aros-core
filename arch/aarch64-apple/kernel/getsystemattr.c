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
#include <aros/apple/jit.h>
#include "jit_protect.h"

#include <kernel_base.h>
#include "kernel_intern.h"
#include "page_allocator.h"
#include "platform_irq.h"
#include "platform_debug.h"
#include "cpu_monitor.h"
#include <proto/kernel.h>

extern struct KrnPlatformTimer AROSApplePlatformTimer;

#include <aros/apple/idle.h>
#include <aros/apple/klog.h>
#include <aros/apple/crashlog.h>
extern intptr_t AROSAppleKernelLogRecord(void);
extern intptr_t AROSAppleKernelCrashLogRecord(void);
extern volatile uint32_t AROSAppleKernelIdleMode;
extern volatile uint64_t AROSAppleKernelIdleEntries;
extern volatile uint32_t AROSAppleKernelTicksHeld;

AROS_LH1(intptr_t, KrnGetSystemAttr,
    AROS_LHA(uint32_t, id, D0),
    struct KernelBase *, KernelBase, 29, Kernel)
{
    AROS_LIBFUNC_INIT

    switch (id)
    {
    case KATTR_PageProtect:
        return (intptr_t)(KRN_PageProtect)AROSApplePageProtect;
    case AROS_APPLE_KATTR_JIT_MODE:
        return AROSAppleJitMode();
    case AROS_APPLE_KATTR_CPU_STATS:
    {
        const struct AROSAppleCPUStats *stats = AROSAppleCPUMonitorStats();
        return stats ? (intptr_t)stats : -1;
    }
    case AROS_APPLE_KATTR_KLOG:
        return AROSAppleKernelLogRecord();
    case AROS_APPLE_KATTR_CRASH_LOG:
        return AROSAppleKernelCrashLogRecord();
    case AROS_APPLE_KATTR_IDLE_CONTROL:
    {
        static struct AROSAppleIdleControl control;

        control.version = AROS_APPLE_IDLE_CONTROL_VERSION;
        control.size = sizeof(control);
        control.mode = &AROSAppleKernelIdleMode;
        control.entries = &AROSAppleKernelIdleEntries;
        control.ticks_held = &AROSAppleKernelTicksHeld;
        return (intptr_t)&control;
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
    case KATTR_HWCap:
    case KATTR_HWCap2:
    case AROS_APPLE_KATTR_BOARD_MODEL:
    case AROS_APPLE_KATTR_BOARD_COMPATIBLE:
    case AROS_APPLE_KATTR_BOOT_SEED:
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

    /* Product identities remain opaque to the common attribute dispatcher. */
    {
        intptr_t value;
        if (AROSPlatformIdentity(id, &value))
            return value;
    }
    return AROSPlatformIRQAttribute(id);

    AROS_LIBFUNC_EXIT
}
