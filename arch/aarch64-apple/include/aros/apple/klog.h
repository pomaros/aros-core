/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: The kernel log ring: every character of the kernel's debug output
          (KrnBug, bug(), the [KI] lines) is also kept in memory, so a
          native boot without a debug cable can still read it (C:KLog).

    KrnGetSystemAttr(AROS_APPLE_KATTR_KLOG) answers the address of this
    record, or -1. head counts every character ever written; the last
    min(head, capacity) of them are in ring, the newest at
    ring[(head - 1) % capacity]. Writers on several cores may interleave.
*/
#ifndef AROS_APPLE_KLOG_H
#define AROS_APPLE_KLOG_H

#include <stdint.h>

#define AROS_APPLE_KATTR_KLOG UINT32_C(0x83f10016)
#define AROS_APPLE_KLOG_VERSION UINT32_C(1)

struct AROSAppleKernelLog {
    uint32_t version;
    uint32_t size;
    const volatile char *ring;
    uint64_t capacity;
    const volatile uint64_t *head;
};

#endif
