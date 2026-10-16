/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: One shared loader, FDT-device-map and MMU preparation path for the
 * production stage two and explicit physical observers.
 */

#include "stage2_prepare.h"

#include "stage2_fdt_map.h"
#include "stage2_mmu.h"
#include "stage2_tags.h"

enum AROSAppleStage2Status AROSAppleStage2PrepareMemory(
    const struct AROSBootInfo *boot_info,
    uintptr_t stage2_image_start,
    uintptr_t stage2_image_end,
    uintptr_t stage2_writable_start,
    struct AROSAppleStage2Context *context,
    struct AROSAppleStage2Result *result,
    AROSAppleStage2PrepareObserver observer,
    void *observer_context)
{
    const struct AROSAppleMemoryPlan *memory_plan;
    enum AROSAppleStage2Status status;

    status = AROSAppleStage2Load(boot_info, stage2_image_start,
        stage2_image_end, context, result);
    if (status != AROS_APPLE_STAGE2_OK)
        return status;
    memory_plan = (const struct AROSAppleMemoryPlan *)result->memory_plan;
    if (!AROSAppleStage2BuildDeviceMap(boot_info, memory_plan,
            &result->device_map))
        return AROS_APPLE_STAGE2_INVALID_DEVICE_MAP;
    if (!AROSAppleStage2AppendDeviceTags(result))
        return AROS_APPLE_STAGE2_INVALID_DEVICE_MAP;
    if (observer != NULL && !observer(AROS_APPLE_STAGE2_OBSERVE_BEFORE_MMU,
            boot_info, result, observer_context))
        return AROS_APPLE_STAGE2_OBSERVER;
    status = AROSAppleStage2EnableIdentityMMU(result, stage2_image_start,
        stage2_writable_start);
    if (status != AROS_APPLE_STAGE2_OK || result->translation_table == 0 ||
        result->translation_table_count == 0 ||
        result->translation_mapped_bytes == 0 ||
        result->translation_normal_bytes == 0 ||
        result->translation_device_bytes != result->device_map.mapped_bytes ||
        result->translation_normal_bytes >
            result->translation_mapped_bytes ||
        result->translation_device_bytes >
            result->translation_mapped_bytes -
                result->translation_normal_bytes ||
        result->translation_framebuffer_bytes !=
            result->translation_mapped_bytes -
                result->translation_normal_bytes -
                result->translation_device_bytes)
        return AROS_APPLE_STAGE2_MMU;
    if (observer != NULL && !observer(AROS_APPLE_STAGE2_OBSERVE_AFTER_MMU,
            boot_info, result, observer_context))
        return AROS_APPLE_STAGE2_OBSERVER;
    return AROS_APPLE_STAGE2_OK;
}
