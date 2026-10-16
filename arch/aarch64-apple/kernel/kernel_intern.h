/*
    Copyright (C) 2026, John KNIPPER. All rights reserved.

    Desc: Private kernel.resource declarations for the Apple AArch64 target.
*/

#ifndef AROS_AARCH64_APPLE_KERNEL_INTERN_H
#define AROS_AARCH64_APPLE_KERNEL_INTERN_H

#include <stdint.h>

/* Set by the coldstart proof once the dispatch-mask proof ran: the bounded
 * [KT] idle and timer traces count from there, not from the first boot idle. */
extern volatile uint32_t AROSAppleKernelIdleTraceEnable;

struct AROSAppleBatteryInfo;
struct AROSAppleSystemTimeInfo;

int AROSAppleKernelDebugPutDirect(int chr);
void AROSAppleKernelDebugInitialize(uintptr_t base, uint64_t size);
void AROSAppleKernelDebugSelectVirtConsole(const void *fdt, uint64_t size);

int AROSAppleKernelChosenProperty(
    const void *fdt, uint64_t size, const char *name,
    const uint8_t **value, uint32_t *length);
void AROSAppleKernelRetainDeviceTree(const void *fdt, uint64_t size);
int AROSAppleKernelRetainedDeviceTree(const void **fdt, uint64_t *size);
uint64_t AROSAppleKernelChosenDecimal(const char *name);
/* The desktop hold publishes each SMC battery sample here; readers get it
 * through AROS_APPLE_KATTR_BATTERY. */
void AROSAppleKernelPublishBattery(const struct AROSAppleBatteryInfo *sample);
/* The hold publishes what set the clock; readers use AROS_APPLE_KATTR_SYSTEM_TIME. */
void AROSAppleKernelPublishSystemTime(const struct AROSAppleSystemTimeInfo *info);
/* Record CPU and board identity at EL1; answer the AROS_APPLE_KATTR_* ids. */
void AROSAppleKernelRecordIdentity(void);
int AROSAppleKernelIdentity(uint32_t id, intptr_t *value);
/* The boot log on the panel until the desktop claims the screen. */
void AROSAppleKernelScreenLogPut(int chr);
void AROSAppleKernelScreenLogStop(void);


#endif /* AROS_AARCH64_APPLE_KERNEL_INTERN_H */
