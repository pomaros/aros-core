/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 */

#ifndef AROS_AARCH64_APPLE_APPLEBOOT_DEVICE_H
#define AROS_AARCH64_APPLE_APPLEBOOT_DEVICE_H

#include <exec/devices.h>
#include <exec/lists.h>
#include <exec/types.h>

#include <stdint.h>

struct AppleBootUnit {
    struct Unit unit;
    struct List change_requests;
};

struct AppleBootBase {
    struct Device device;
    struct AppleBootUnit unit;
    APTR kernel_base;
    uintptr_t image_start;
    uint64_t image_size;
    ULONG sector_count;
    UWORD sectors_per_track;
    UWORD heads;
    BOOL ready;
};

#endif /* AROS_AARCH64_APPLE_APPLEBOOT_DEVICE_H */
