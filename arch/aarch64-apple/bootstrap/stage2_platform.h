/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Platform boundary for second-stage validation and diagnostics.
 */

#ifndef STAGE2_PLATFORM_H
#define STAGE2_PLATFORM_H
#include "stage2_loader.h"
#include <aros/apple/runtime.h>

/* Product observers are optional; a missing callback means no observation,
 * never a successful hardware proof. The native provider itself is required. */
struct FDTView;
struct FDTPlatform;
/* Raw stage2 images have no self-relocations. Provider calls are statically
 * bound PC-relative branches, never initialized absolute function pointers. */
#if (!defined(AROS_VM_GUEST) || !AROS_VM_GUEST) && !defined(AROS_STAGE2_PLATFORM)
#error "Native product must select its stage2 platform provider"
#endif
#define STAGE2_JOIN_(a, b) a ## b
#define STAGE2_JOIN(a, b) STAGE2_JOIN_(a, b)
#ifdef AROS_STAGE2_PLATFORM
#define STAGE2_HAS_PLATFORM 1
#define STAGE2_PROVIDER(name) STAGE2_JOIN(AROS_STAGE2_PLATFORM, _ ## name)
#else
#define STAGE2_HAS_PLATFORM 0
#define STAGE2_PROVIDER(name) STAGE2_JOIN(UnavailableStage2Platform, _ ## name)
#endif
int STAGE2_PROVIDER(classify_compatible)(const uint8_t *, uint32_t);
int STAGE2_PROVIDER(chosen_property)(const uint8_t *, uint32_t,
    const uint8_t *, uint32_t, struct FDTPlatform *);
int STAGE2_PROVIDER(build_device_map)(const struct FDTView *, const struct FDTPlatform *,
    const struct AROSAppleMemoryPlan *, struct AROSAppleDeviceMap *);
int STAGE2_PROVIDER(resident_order_valid)(const struct AROSAppleColdStartProof *);
int STAGE2_PROVIDER(console_valid)(const struct AROSAppleStage2Result *,
    const struct AROSAppleKernelStartupState *, const struct AROSAppleDeviceRole *);
void STAGE2_PROVIDER(checkpoint)(const struct AROSBootInfo *, uint32_t);
int STAGE2_PROVIDER(has_import_observer)(void);
int STAGE2_PROVIDER(before_import)(const struct AROSBootInfo *,
    const struct AROSAppleStage2Result *, const struct AROSAppleMemoryPlan *);
int STAGE2_PROVIDER(after_import)(const struct AROSAppleStage2Result *,
    const struct AROSAppleMemoryPlan *, const struct AROSAppleKernelStartupState *);
#endif
