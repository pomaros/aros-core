/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1
 */
#ifndef COLDSTART_PLATFORM_H
#define COLDSTART_PLATFORM_H

#include "desktop_storage.h"
#include <stdint.h>
#include <stddef.h>
#include <exec/types.h>
struct AROSAppleKernelStartupState;
struct AROSAppleColdStartProof;
typedef int (*ColdStartResidentAt)(const struct AROSAppleKernelStartupState *,
    uint32_t, const char *, BYTE, ULONG);
typedef int (*ColdStartResidentIndex)(const struct AROSAppleKernelStartupState *,
    const char *);
struct DosLibrary;
struct timerequest;

struct ColdStartResident {
    const char *name;
    BYTE priority;
    ULONG flags;
};

/* Borrowed for the duration of the desktop hold; all calls are on its task.
 * Platform services own their polling cadence and private resource state. */
struct ColdStartServices {
    struct DosLibrary *dos;
    struct timerequest *timer;
    void (*puts)(const char *);
    void (*hex)(uint64_t);
    void (*decimal)(uint64_t);
    void (*zero)(void *, size_t);
    uint64_t (*counter)(void);
    uint64_t (*frequency)(void);
    uint64_t (*firmware_clock)(void);
    int (*rtc_disabled)(void);
    int (*assign_exists)(const char *);
    void (*publish_time)(uint64_t, int32_t);
};
struct ColdStartPlatform {
    void (*begin)(const struct ColdStartServices *);
    void (*start_polling)(uint64_t started, uint64_t frequency);
    void (*poll)(uint64_t now, uint64_t started, uint64_t frequency);
    void (*report_irqs)(int changed_only);
    void (*finish)(struct timerequest *, int timer_open, int probe_started);
    const struct ColdStartResident *residents;
    uint32_t resident_count;
    const char *input_resident;
    const char *storage_device;
    const char *optional_usb_resident;
    const char *usb_before_resident;
    const char *firmware_after_resident;
    int (*prepare_proof)(const struct AROSAppleKernelStartupState *,
        struct AROSAppleColdStartProof *, int desktop,
        ColdStartResidentAt, ColdStartResidentIndex);
    int (*resources_valid)(uint32_t resident_count);
};

/* The product build names its provider explicitly. An ordinary VM build has
 * no provider and does not link or compile any native hardware services. */
#if (!defined(AROS_VM_GUEST) || !AROS_VM_GUEST) && !defined(AROS_COLDSTART_PLATFORM)
#error "Native product must select its coldstart platform provider"
#endif
#ifdef AROS_COLDSTART_PLATFORM
extern const struct ColdStartPlatform AROS_COLDSTART_PLATFORM;
#define COLDSTART_PLATFORM (&AROS_COLDSTART_PLATFORM)
#else
#define COLDSTART_PLATFORM ((const struct ColdStartPlatform *)0)
#endif
#endif
