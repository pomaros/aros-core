/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Freestanding entry wrapper for the Apple second-stage loader.
 */

#include "stage2_exception.h"

#include "stage2_loader.h"
#include "stage2_prepare.h"
#include "stage2_probe.h"
#include "stage2_platform.h"
#define STAGE2_EYES(point) \
    do { if (STAGE2_HAS_PLATFORM) \
        STAGE2_PROVIDER(checkpoint)(boot_info, (point)); } while (0)


#include <aros/apple/startup.h>
#include <aros/apple/runtime.h>

_Static_assert(__builtin_offsetof(struct AROSAppleKernelStartupState, phase)==STAGE2_KERNEL_PHASE, "kernel phase diagnostic offset");
_Static_assert(__builtin_offsetof(struct AROSAppleKernelStartupState, status)==STAGE2_KERNEL_STATUS, "kernel status diagnostic offset");
_Static_assert(__builtin_offsetof(struct AROSAppleKernelStartupState, detail)==STAGE2_KERNEL_DETAIL, "kernel detail diagnostic offset");

extern char __stage2_user_stack_start[];
extern char __stage2_user_stack_end[];
extern char __stage2_image_start[];
extern char __stage2_writable_start[];

struct AROSAppleStage2Context AROSAppleStage2LoaderContext;
struct AROSAppleStage2Result AROSAppleStage2LastResult;
struct AROSAppleStage2MemoryProof AROSAppleStage2LastMemoryProof;
_Alignas(16) struct AROSAppleKernelStartupState AROSAppleStage2KernelState;
_Alignas(16) struct AROSAppleRuntimeProof AROSAppleStage2RuntimeProof;
_Alignas(16) struct AROSAppleDispatchProof AROSAppleStage2DispatchProof;
_Alignas(16) struct AROSAppleColdStartProof AROSAppleStage2ColdStartProof;
volatile uint64_t AROSAppleStage2Phase;
uintptr_t AROSAppleStage2KernelBase;

static int dispatch_gprs_valid(const uint64_t *observed, uint64_t base)
{
    uint32_t index;

    for (index = 0; index < AROS_APPLE_DISPATCH_GPR_COUNT; ++index) {
        if (observed[index] != base + index)
            return 0;
    }
    return 1;
}

static int payload_digest_equal(const uint8_t *left, const uint8_t *right)
{
    uint8_t difference = 0;
    uint32_t index;

    for (index = 0; index < 32; ++index)
        difference |= left[index] ^ right[index];
    return difference == 0;
}

/* Called after loader validation: module table and name ranges are owned. */
static int boot_has_afs_handler(const struct AROSBootInfo *boot_info)
{
    static const char expected[] = "afsplus-handler";
    uint32_t index;

    for (index = 0; index < boot_info->module_count; ++index) {
        const struct AROSBootModule *module = (const void *)(uintptr_t)
            (boot_info->modules_physical +
             (uint64_t)index * boot_info->module_entry_size);
        const char *name = (const char *)(uintptr_t)module->name_physical;
        uint32_t byte;

        if (module->name_size != sizeof(expected) - 1U)
            continue;
        for (byte = 0; byte < sizeof(expected) - 1U; ++byte) {
            if (name[byte] != expected[byte])
                break;
        }
        if (byte == sizeof(expected) - 1U)
            return 1;
    }
    return 0;
}

static int coldstart_resident_order_valid(
    const struct AROSAppleColdStartProof *proof, int expect_afs)
{
    if (proof->kernel_index != 0 || proof->exec_index != 1 ||
        proof->expansion_index != 2)
        return 0;
    if (STAGE2_HAS_PLATFORM) {
        if (!STAGE2_PROVIDER(resident_order_valid)(proof)) return 0;
        if (proof->resident_count < 54) return 1;
    } else {
        if (proof->pmgr_index != UINT32_MAX || proof->rtkit_index != UINT32_MAX)
            return 0;
        if (proof->resident_count < 54) {
            if (expect_afs) return 0;
    return (proof->resident_count == 20 &&
            proof->kernel_index == 0 &&
            proof->exec_index == 1 &&
            proof->expansion_index == 2 &&
            proof->utility_index == 3 &&
            proof->aros_index == 5 &&
            proof->bootloader_index == 6 &&
            proof->task_index == 7 &&
            proof->filesystem_index == 8 &&
            proof->timer_index == 9 &&
            proof->partition_index == 10 &&
            proof->locale_index == 11 &&
            proof->stdc_index == 12 &&
            proof->stdcio_index == 13 &&
            proof->posixc_index == 14 &&
            proof->fat_index == 15 &&
            proof->appleboot_index == 16 &&
            proof->dos_index == 17 &&
            proof->iffparse_index == 18);
        }
    }
    /*
     * The desktop family (54 residents and up): the kernel proof has already
     * checked every position, name, priority and flag against its table, so
     * this independent side checks that the recorded indices lie inside the
     * list and keep the canonical order, whatever else the bundle carries.
     */
    if (proof->resident_count >= 54) {
        return proof->utility_index < proof->aros_index &&
            proof->aros_index < proof->bootloader_index &&
            proof->bootloader_index < proof->task_index &&
            proof->task_index < proof->filesystem_index &&
            proof->filesystem_index < proof->timer_index &&
            proof->timer_index < proof->partition_index &&
            proof->partition_index < proof->locale_index &&
            proof->stdc_index == proof->locale_index + 1U &&
            proof->stdcio_index == proof->stdc_index + 1U &&
            proof->posixc_index == proof->stdcio_index + 1U &&
            proof->posixc_index < proof->fat_index &&
            proof->appleboot_index == proof->fat_index + 1U +
                (expect_afs ? 1U : 0U) &&
            proof->appleboot_index < proof->dos_index &&
            proof->dos_index < proof->iffparse_index &&
            proof->iffparse_index < proof->resident_count;
    }
    return 0;
}

static int coldstart_wanderer_valid(
    const struct AROSAppleColdStartProof *proof)
{
    /* The desktop family: 54 residents and up. */
    if (proof->resident_count >= 54)
        return proof->wanderer_requested == 1 &&
            proof->wanderer_seglist != 0 &&
            proof->wanderer_process != 0 && proof->wanderer_screen != 0 &&
            proof->wanderer_window != 0 && proof->wanderer_pixel != 0 &&
            proof->wanderer_initial_cli_process != 0 &&
            proof->wanderer_initial_cli_process != proof->wanderer_process &&
            proof->wanderer_initial_cli_seen == 1 &&
            proof->wanderer_initial_cli_closed == 1;
    return proof->wanderer_requested == 0 && proof->wanderer_seglist == 0 &&
        proof->wanderer_process == 0 && proof->wanderer_screen == 0 &&
        proof->wanderer_window == 0 && proof->wanderer_pixel == 0 &&
        proof->wanderer_initial_cli_process == 0 &&
        proof->wanderer_initial_cli_seen == 0 &&
        proof->wanderer_initial_cli_closed == 0;
}

static int coldstart_runtime_exception_empty(
    const struct AROSAppleColdStartProof *proof)
{
    uint32_t index;

    if (proof->runtime_exception_vector != 0 ||
        proof->runtime_exception_esr != 0 ||
        proof->runtime_exception_far != 0 ||
        proof->runtime_exception_elr != 0 ||
        proof->runtime_exception_spsr != 0 ||
        proof->runtime_exception_lr != 0 ||
        proof->runtime_exception_sp != 0 ||
        proof->runtime_exception_task != 0)
        return 0;
    for (index = 0; index < sizeof(proof->runtime_exception_task_name);
        ++index) {
        if (proof->runtime_exception_task_name[index] != '\0')
            return 0;
    }
    return 1;
}

int AROSAppleStage2Main(
    const struct AROSBootInfo *boot_info,
    uintptr_t image_start,
    uintptr_t image_end)
{
    uintptr_t user_stack;
    uint64_t user_stack_size;
    enum AROSAppleStage2Status status;
    enum AROSAppleKernelStartupStatus kernel_status;
    const struct AROSAppleMemoryPlan *memory_plan;
    const struct AROSAppleDeviceRole *console;
    int has_afs;

    AROSAppleStage2Phase = STAGE2_PHASE_LOAD;
    STAGE2_EYES(4);
    status = AROSAppleStage2PrepareMemory(
        boot_info, image_start, image_end,
        image_start + ((uintptr_t)__stage2_writable_start -
            (uintptr_t)__stage2_image_start),
        &AROSAppleStage2LoaderContext,
        &AROSAppleStage2LastResult, NULL, NULL);
    if (status != AROS_APPLE_STAGE2_OK)
        return (int)status;
    /* Capture before Exec imports and may recycle the producer module table. */
    has_afs = boot_has_afs_handler(boot_info);
    STAGE2_EYES(5);
    memory_plan = (const struct AROSAppleMemoryPlan *)
        AROSAppleStage2LastResult.memory_plan;
    AROSAppleStage2KernelBase = AROSAppleStage2LastResult.readonly_start;
    AROSAppleStage2Phase = STAGE2_PHASE_CONTRACT;
    STAGE2_EYES(6);
    if (((enum AROSAppleStartupContractStatus (*)(
            const struct AROSAppleTagItem *, uint32_t))
            AROSAppleStage2LastResult.kernel_contract)(
                (const struct AROSAppleTagItem *)
                    AROSAppleStage2LastResult.boot_tags,
                AROS_APPLE_BOOT_MAGIC) != AROS_APPLE_STARTUP_CONTRACT_OK)
        return (int)AROS_APPLE_STAGE2_KERNEL_CONTRACT;
    STAGE2_EYES(7);
    AROSAppleStage2Phase = STAGE2_PHASE_MEMORY_PROBE;
    STAGE2_EYES(8);
    if (!AROSAppleStage2ProbeMemory(
            (const struct AROSAppleTagItem *)
                AROSAppleStage2LastResult.boot_tags,
            &AROSAppleStage2LastMemoryProof))
        return (int)AROS_APPLE_STAGE2_MEMORY_PROBE;
    STAGE2_EYES(9);

    AROSAppleStage2Phase = STAGE2_PHASE_KERNEL_MEMORY;
    STAGE2_EYES(10);
    kernel_status = ((enum AROSAppleKernelStartupStatus (*)(
        const struct AROSAppleTagItem *, uint32_t, uint64_t, uint64_t,
        struct AROSAppleKernelStartupState *))
        AROSAppleStage2LastResult.kernel_prepare_memory)(
            (const struct AROSAppleTagItem *)
                AROSAppleStage2LastResult.boot_tags,
            AROS_APPLE_BOOT_MAGIC,
            AROSAppleStage2LastMemoryProof.range_start,
            AROSAppleStage2LastMemoryProof.range_end,
            &AROSAppleStage2KernelState);
    if (kernel_status != AROS_APPLE_KERNEL_STARTUP_OK)
        return (int)AROS_APPLE_STAGE2_KERNEL_MEMORY;
    console = AROSAppleStage2FindDeviceRole(
        &AROSAppleStage2LastResult.device_map,
        AROS_APPLE_DEVICE_ROLE_CONSOLE, 0);
    if (AROSAppleStage2KernelState.page_arena_start !=
            AROSAppleStage2LastResult.page_arena_start ||
        AROSAppleStage2KernelState.page_arena_size !=
            AROSAppleStage2LastResult.page_arena_size ||
        AROSAppleStage2KernelState.payload_identity.generation_sequence !=
            boot_info->payload_generation_sequence ||
        AROSAppleStage2KernelState.payload_identity.size !=
            boot_info->payload_size ||
        !payload_digest_equal(AROSAppleStage2KernelState.payload_identity.sha256,
            boot_info->payload_sha256) ||
        AROSAppleStage2KernelState.payload_identity.slot !=
            boot_info->payload_slot ||
        AROSAppleStage2KernelState.payload_identity.flags !=
            boot_info->payload_flags ||
        AROSAppleStage2KernelState.payload_identity.producer !=
            boot_info->producer ||
        AROSAppleStage2KernelState.payload_identity.reserved != 0 ||
        (AROSAppleStage2LastResult.device_map.profile ==
            AROS_APPLE_DEVICE_MAP_PROFILE_NONE &&
            (AROSAppleStage2KernelState.debug_uart_start != 0 ||
             AROSAppleStage2KernelState.debug_uart_end != 0)) ||
        (STAGE2_HAS_PLATFORM && !STAGE2_PROVIDER(console_valid)(
            &AROSAppleStage2LastResult, &AROSAppleStage2KernelState, console)))
        return (int)AROS_APPLE_STAGE2_KERNEL_MEMORY;
    STAGE2_EYES(11);

    AROSAppleStage2Phase = STAGE2_PHASE_EXEC_INIT;
    STAGE2_EYES(12);
    kernel_status = ((enum AROSAppleKernelStartupStatus (*)(
        const struct AROSAppleTagItem *,
        struct AROSAppleKernelStartupState *))
        AROSAppleStage2LastResult.kernel_initialize_exec)(
            (const struct AROSAppleTagItem *)
                AROSAppleStage2LastResult.boot_tags,
            &AROSAppleStage2KernelState);
    if (kernel_status != AROS_APPLE_KERNEL_STARTUP_OK)
        return (int)AROS_APPLE_STAGE2_EXEC_INIT;
    if (AROSAppleStage2KernelState.phase !=
            AROS_APPLE_KERNEL_PHASE_EXEC_READY ||
        AROSAppleStage2KernelState.exec_resident !=
            AROSAppleStage2LastResult.exec_resident ||
        AROSAppleStage2KernelState.sys_base == 0 ||
        AROSAppleStage2KernelState.sys_base !=
            *(const uintptr_t *)AROSAppleStage2LastResult.sysbase_storage)
        return (int)AROS_APPLE_STAGE2_SYSBASE;
    STAGE2_EYES(13);

    AROSAppleStage2Phase = STAGE2_PHASE_SINGLE_TASK_INIT;
    STAGE2_EYES(14);
    kernel_status = ((enum AROSAppleKernelStartupStatus (*)(
        const struct AROSAppleTagItem *,
        struct AROSAppleKernelStartupState *))
        AROSAppleStage2LastResult.kernel_initialize_single_task)(
            (const struct AROSAppleTagItem *)
                AROSAppleStage2LastResult.boot_tags,
            &AROSAppleStage2KernelState);
    if (kernel_status != AROS_APPLE_KERNEL_STARTUP_OK)
        return (int)AROS_APPLE_STAGE2_SINGLE_TASK_INIT;
    if (AROSAppleStage2KernelState.phase !=
            AROS_APPLE_KERNEL_PHASE_SINGLE_TASK_READY ||
        AROSAppleStage2KernelState.kernel_base == 0)
        return (int)AROS_APPLE_STAGE2_KERNEL_BASE;
    STAGE2_EYES(15);
    /*
     * Read-only, and deliberately before the import phase: the device tree is
     * loader-owned memory and is reclaimed once physical memory is imported,
     * so a later look finds a stale pointer rather than a tree.
     */

    AROSAppleStage2Phase = STAGE2_PHASE_MEMORY_IMPORT;
    if (STAGE2_HAS_PLATFORM && STAGE2_PROVIDER(has_import_observer)()) {
        STAGE2_EYES(16);
        if (!STAGE2_PROVIDER(before_import)(
                boot_info, &AROSAppleStage2LastResult, memory_plan))
            return (int)AROS_APPLE_STAGE2_OBSERVER;
        STAGE2_EYES(17);
    }
    STAGE2_EYES(18);
    kernel_status = ((enum AROSAppleKernelStartupStatus (*)(
        const struct AROSAppleTagItem *,
        const struct AROSAppleMemoryPlan *,
        struct AROSAppleKernelStartupState *))
        AROSAppleStage2LastResult.kernel_import_memory)(
            (const struct AROSAppleTagItem *)
                AROSAppleStage2LastResult.boot_tags,
            (const struct AROSAppleMemoryPlan *)
                AROSAppleStage2LastResult.memory_plan,
            &AROSAppleStage2KernelState);
    if (kernel_status != AROS_APPLE_KERNEL_STARTUP_OK ||
        AROSAppleStage2KernelState.phase !=
            AROS_APPLE_KERNEL_PHASE_MEMORY_READY ||
        AROSAppleStage2KernelState.memory_plan !=
            AROSAppleStage2LastResult.memory_plan ||
        AROSAppleStage2KernelState.reclaimed_bytes !=
            AROSAppleStage2LastResult.ownership.release_after_stage2_bytes ||
        AROSAppleStage2KernelState.release_range_count !=
            AROSAppleStage2LastResult.ownership.release_after_stage2_count ||
        AROSAppleStage2KernelState.retained_range_count !=
            AROSAppleStage2LastResult.ownership.retained_count ||
        AROSAppleStage2KernelState.conventional_range_count !=
            memory_plan->conventional_count ||
        AROSAppleStage2KernelState.conventional_bytes !=
            memory_plan->conventional_bytes ||
        AROSAppleStage2KernelState.memory_header_count !=
            memory_plan->conventional_count + memory_plan->release_count ||
        AROSAppleStage2KernelState.total_free_bytes == 0)
        return (int)AROS_APPLE_STAGE2_MEMORY_IMPORT;
    STAGE2_EYES(19);
    if (STAGE2_HAS_PLATFORM && STAGE2_PROVIDER(has_import_observer)()) {
        STAGE2_EYES(20);
        if (!STAGE2_PROVIDER(after_import)(
                &AROSAppleStage2LastResult, memory_plan,
                &AROSAppleStage2KernelState))
            return (int)AROS_APPLE_STAGE2_OBSERVER;
        STAGE2_EYES(21);
    }

    user_stack = image_start +
        ((uintptr_t)__stage2_user_stack_start -
            (uintptr_t)__stage2_image_start);
    user_stack_size = (uint64_t)((uintptr_t)__stage2_user_stack_end -
        (uintptr_t)__stage2_user_stack_start);
    AROSAppleStage2Phase = STAGE2_PHASE_RUNTIME;
    STAGE2_EYES(22);
    if (!((int (*)(uintptr_t, uint64_t, struct AROSAppleRuntimeProof *))
            AROSAppleStage2LastResult.kernel_prove_runtime)(
                user_stack, user_stack_size,
                &AROSAppleStage2RuntimeProof) ||
        AROSAppleStage2RuntimeProof.version !=
            AROS_APPLE_RUNTIME_PROOF_VERSION ||
        AROSAppleStage2RuntimeProof.size !=
            sizeof(AROSAppleStage2RuntimeProof) ||
        AROSAppleStage2RuntimeProof.frame_size !=
            sizeof(struct AROSAppleRuntimeFrame) ||
        AROSAppleStage2RuntimeProof.vector_table_size !=
            AROS_APPLE_RUNTIME_VECTOR_TABLE_SIZE ||
        AROSAppleStage2RuntimeProof.el1_is_super != 1 ||
        AROSAppleStage2RuntimeProof.el0_is_super != 0 ||
        AROSAppleStage2RuntimeProof.el1_service_count != AROS_APPLE_RUNTIME_PROOF_EL1_CALLS ||
        AROSAppleStage2RuntimeProof.el0_service_count != AROS_APPLE_RUNTIME_PROOF_EL0_CALLS ||
        AROSAppleStage2RuntimeProof.supervisor_count != AROS_APPLE_RUNTIME_PROOF_SUPERVISORS ||
        AROSAppleStage2RuntimeProof.rejected_count != AROS_APPLE_RUNTIME_PROOF_REJECTIONS ||
        AROSAppleStage2RuntimeProof.el1_invalid_result !=
            AROS_APPLE_RUNTIME_SVC_ERROR ||
        AROSAppleStage2RuntimeProof.el1_kernel_result !=
            AROS_APPLE_RUNTIME_SVC_ERROR ||
        AROSAppleStage2RuntimeProof.el0_invalid_result !=
            AROS_APPLE_RUNTIME_SVC_ERROR ||
        AROSAppleStage2RuntimeProof.el0_stack_observed < user_stack ||
        AROSAppleStage2RuntimeProof.el0_stack_observed >=
            user_stack + user_stack_size)
        return (int)AROS_APPLE_STAGE2_RUNTIME;

    AROSAppleStage2Phase = STAGE2_PHASE_DISPATCH;
    STAGE2_EYES(23);
    if (!((int (*)(uintptr_t, uint64_t, struct AROSAppleDispatchProof *))
            AROSAppleStage2LastResult.kernel_prove_dispatch)(
                user_stack, user_stack_size,
                &AROSAppleStage2DispatchProof) ||
        AROSAppleStage2DispatchProof.version !=
            AROS_APPLE_DISPATCH_PROOF_VERSION ||
        AROSAppleStage2DispatchProof.size !=
            sizeof(AROSAppleStage2DispatchProof) ||
        AROSAppleStage2DispatchProof.context_size !=
            AROS_APPLE_DISPATCH_CONTEXT_SIZE ||
        AROSAppleStage2DispatchProof.frame_size !=
            AROS_APPLE_RUNTIME_FRAME_SIZE ||
        AROSAppleStage2DispatchProof.boot_task == 0 ||
        AROSAppleStage2DispatchProof.task_a == 0 ||
        AROSAppleStage2DispatchProof.task_b == 0 ||
        AROSAppleStage2DispatchProof.boot_task ==
            AROSAppleStage2DispatchProof.task_a ||
        AROSAppleStage2DispatchProof.boot_task ==
            AROSAppleStage2DispatchProof.task_b ||
        AROSAppleStage2DispatchProof.task_a ==
            AROSAppleStage2DispatchProof.task_b ||
        AROSAppleStage2DispatchProof.context_a == 0 ||
        AROSAppleStage2DispatchProof.context_b == 0 ||
        AROSAppleStage2DispatchProof.context_a ==
            AROSAppleStage2DispatchProof.context_b ||
        AROSAppleStage2DispatchProof.sequence_count !=
            AROS_APPLE_DISPATCH_SEQUENCE_COUNT ||
        AROSAppleStage2DispatchProof.boot_switches != 2 ||
        AROSAppleStage2DispatchProof.task_a_switches != 2 ||
        AROSAppleStage2DispatchProof.task_b_switches != 2 ||
        AROSAppleStage2DispatchProof.cleanup_count != 2 ||
        AROSAppleStage2DispatchProof.dispatch_count_after !=
            AROSAppleStage2DispatchProof.dispatch_count_before + 6 ||
        !dispatch_gprs_valid(AROSAppleStage2DispatchProof.boot_gpr_after,
            AROS_APPLE_DISPATCH_BOOT_GPR_BASE) ||
        !dispatch_gprs_valid(AROSAppleStage2DispatchProof.task_a_gpr_after,
            AROS_APPLE_DISPATCH_TASK_A_GPR_BASE) ||
        !dispatch_gprs_valid(AROSAppleStage2DispatchProof.task_b_gpr_after,
            AROS_APPLE_DISPATCH_TASK_B_GPR_BASE) ||
        AROSAppleStage2DispatchProof.boot_simd_after[0] !=
            AROS_APPLE_DISPATCH_BOOT_SIMD_LOW ||
        AROSAppleStage2DispatchProof.boot_simd_after[1] !=
            AROS_APPLE_DISPATCH_BOOT_SIMD_HIGH ||
        AROSAppleStage2DispatchProof.task_a_simd_after[0] !=
            AROS_APPLE_DISPATCH_TASK_A_SIMD_LOW ||
        AROSAppleStage2DispatchProof.task_a_simd_after[1] !=
            AROS_APPLE_DISPATCH_TASK_A_SIMD_HIGH ||
        AROSAppleStage2DispatchProof.task_b_simd_after[0] !=
            AROS_APPLE_DISPATCH_TASK_B_SIMD_LOW ||
        AROSAppleStage2DispatchProof.task_b_simd_after[1] !=
            AROS_APPLE_DISPATCH_TASK_B_SIMD_HIGH ||
        AROSAppleStage2DispatchProof.return_status !=
            AROS_APPLE_DISPATCH_STATUS_PASS ||
        AROSAppleStage2DispatchProof.status !=
            AROS_APPLE_DISPATCH_STATUS_PASS)
        return (int)AROS_APPLE_STAGE2_DISPATCH;

    AROSAppleStage2Phase = STAGE2_PHASE_COLDSTART;
    STAGE2_EYES(24);
    if (!((int (*)(uintptr_t, uint64_t,
            const struct AROSAppleKernelStartupState *,
            struct AROSAppleColdStartProof *))
            AROSAppleStage2LastResult.kernel_prove_core_coldstart)(
                user_stack, user_stack_size,
                &AROSAppleStage2KernelState,
                &AROSAppleStage2ColdStartProof) ||
        AROSAppleStage2ColdStartProof.version !=
            AROS_APPLE_COLDSTART_PROOF_VERSION ||
        AROSAppleStage2ColdStartProof.size !=
            sizeof(AROSAppleStage2ColdStartProof) ||
        AROSAppleStage2ColdStartProof.status != 1 ||
        AROSAppleStage2ColdStartProof.detail != 0 ||
        !coldstart_resident_order_valid(&AROSAppleStage2ColdStartProof,
            has_afs) ||
        !coldstart_wanderer_valid(&AROSAppleStage2ColdStartProof) ||
        !coldstart_runtime_exception_empty(&AROSAppleStage2ColdStartProof) ||
        AROSAppleStage2ColdStartProof.bootstrap_task == 0 ||
        AROSAppleStage2ColdStartProof.bootstrap_context == 0 ||
        AROSAppleStage2ColdStartProof.kernel_base !=
            AROSAppleStage2KernelState.kernel_base ||
        AROSAppleStage2ColdStartProof.utility_base == 0 ||
        AROSAppleStage2ColdStartProof.task_base == 0 ||
        AROSAppleStage2ColdStartProof.expansion_base == 0 ||
        AROSAppleStage2ColdStartProof.bootloader_base == 0 ||
        AROSAppleStage2ColdStartProof.partition_base == 0 ||
        AROSAppleStage2ColdStartProof.timer_base == 0 ||
        AROSAppleStage2ColdStartProof.filesystem_base == 0 ||
        AROSAppleStage2ColdStartProof.aros_base == 0 ||
        AROSAppleStage2ColdStartProof.iffparse_base == 0 ||
        AROSAppleStage2ColdStartProof.locale_base == 0 ||
        AROSAppleStage2ColdStartProof.stdc_base == 0 ||
        AROSAppleStage2ColdStartProof.stdcio_base == 0 ||
        AROSAppleStage2ColdStartProof.posixc_base == 0 ||
        AROSAppleStage2ColdStartProof.fat_seglist == 0 ||
        AROSAppleStage2ColdStartProof.dos_resident == 0 ||
        AROSAppleStage2ColdStartProof.appleboot_base == 0 ||
        AROSAppleStage2ColdStartProof.boot_node == 0 ||
        AROSAppleStage2ColdStartProof.boot_image_start !=
            AROSAppleStage2LastResult.ramdisk_address ||
        AROSAppleStage2ColdStartProof.boot_image_size !=
            AROSAppleStage2LastResult.ramdisk_size ||
        AROSAppleStage2ColdStartProof.boot_read_actual != 512 ||
        AROSAppleStage2ColdStartProof.boot_bounds_error !=
            UINT64_C(0xfffffffffffffffb) ||
        AROSAppleStage2ColdStartProof.boot_write_error != UINT64_C(28) ||
        AROSAppleStage2ColdStartProof.dos_base == 0 ||
        AROSAppleStage2ColdStartProof.dos_boot_process == 0 ||
        AROSAppleStage2ColdStartProof.sys_lock == 0 ||
        AROSAppleStage2ColdStartProof.boot_volume_task == 0 ||
        AROSAppleStage2ColdStartProof.standard_assign_count != 6 ||
        AROSAppleStage2ColdStartProof.sentinel_read_actual != 36 ||
        AROSAppleStage2ColdStartProof.sentinel_eof_actual != 0 ||
        AROSAppleStage2ColdStartProof.boot_signature_read_actual != 14 ||
        AROSAppleStage2ColdStartProof.abi_probe_seglist == 0 ||
        AROSAppleStage2ColdStartProof.abi_probe_entry == 0 ||
        AROSAppleStage2ColdStartProof.abi_probe_bad_return != 30 ||
        (AROSAppleStage2ColdStartProof.abi_probe_argument_size != 33 &&
         AROSAppleStage2ColdStartProof.abi_probe_argument_size != 34) ||
        AROSAppleStage2ColdStartProof.abi_probe_return != 73 ||
        AROSAppleStage2ColdStartProof.abi_pages_loaded <=
            AROSAppleStage2ColdStartProof.abi_pages_before ||
        AROSAppleStage2ColdStartProof.abi_pages_unloaded !=
            AROSAppleStage2ColdStartProof.abi_pages_before ||
        AROSAppleStage2ColdStartProof.abi_load_allocations == 0 ||
        AROSAppleStage2ColdStartProof.abi_unload_frees == 0 ||
        AROSAppleStage2ColdStartProof.abi_load_rx_protections == 0 ||
        AROSAppleStage2ColdStartProof.abi_unload_rw_protections == 0 ||
        !((AROSAppleStage2ColdStartProof.workbench_probe_requested == 0 &&
           AROSAppleStage2ColdStartProof.workbench_probe_seglist == 0 &&
           AROSAppleStage2ColdStartProof.workbench_probe_process == 0 &&
           AROSAppleStage2ColdStartProof.workbench_probe_reply == 0 &&
           AROSAppleStage2ColdStartProof.workbench_probe_return == 0 &&
           AROSAppleStage2ColdStartProof.workbench_probe_numargs == 0) ||
          (AROSAppleStage2ColdStartProof.workbench_probe_requested == 1 &&
           AROSAppleStage2ColdStartProof.workbench_probe_seglist != 0 &&
           AROSAppleStage2ColdStartProof.workbench_probe_process != 0 &&
           AROSAppleStage2ColdStartProof.workbench_probe_reply != 0 &&
           AROSAppleStage2ColdStartProof.workbench_probe_return == 73 &&
           AROSAppleStage2ColdStartProof.workbench_probe_numargs == 1)) ||
        !((AROSAppleStage2ColdStartProof.tls_probe_requested == 0 &&
           AROSAppleStage2ColdStartProof.tls_probe_seglist == 0 &&
           AROSAppleStage2ColdStartProof.tls_probe_return == 0 &&
           AROSAppleStage2ColdStartProof.tls_probe_argument_size == 0 &&
           AROSAppleStage2ColdStartProof.tls_pages_before == 0 &&
           AROSAppleStage2ColdStartProof.tls_pages_loaded == 0 &&
           AROSAppleStage2ColdStartProof.tls_pages_unloaded == 0 &&
           AROSAppleStage2ColdStartProof.tls_load_allocations == 0 &&
           AROSAppleStage2ColdStartProof.tls_unload_frees == 0 &&
           AROSAppleStage2ColdStartProof.tls_load_rx_protections == 0 &&
           AROSAppleStage2ColdStartProof.tls_unload_rw_protections == 0) ||
          (AROSAppleStage2ColdStartProof.tls_probe_requested == 1 &&
           AROSAppleStage2ColdStartProof.tls_probe_seglist != 0 &&
           AROSAppleStage2ColdStartProof.tls_probe_return == 74 &&
           AROSAppleStage2ColdStartProof.tls_probe_argument_size == 0 &&
           AROSAppleStage2ColdStartProof.tls_pages_loaded >
               AROSAppleStage2ColdStartProof.tls_pages_before &&
           AROSAppleStage2ColdStartProof.tls_pages_unloaded ==
               AROSAppleStage2ColdStartProof.tls_pages_before &&
           AROSAppleStage2ColdStartProof.tls_load_allocations != 0 &&
           AROSAppleStage2ColdStartProof.tls_unload_frees != 0 &&
           AROSAppleStage2ColdStartProof.tls_load_rx_protections != 0 &&
           AROSAppleStage2ColdStartProof.tls_unload_rw_protections != 0)) ||
        !((AROSAppleStage2ColdStartProof.small_probe_requested == 0 &&
           AROSAppleStage2ColdStartProof.small_probe_seglist == 0 &&
           AROSAppleStage2ColdStartProof.small_probe_return == 0 &&
           AROSAppleStage2ColdStartProof.small_probe_bad_return == 0 &&
           AROSAppleStage2ColdStartProof.small_pages_before == 0 &&
           AROSAppleStage2ColdStartProof.small_pages_loaded == 0 &&
           AROSAppleStage2ColdStartProof.small_pages_unloaded == 0 &&
           AROSAppleStage2ColdStartProof.small_load_allocations == 0 &&
           AROSAppleStage2ColdStartProof.small_unload_frees == 0 &&
           AROSAppleStage2ColdStartProof.small_load_rx_protections == 0 &&
           AROSAppleStage2ColdStartProof.small_unload_rw_protections == 0) ||
          (AROSAppleStage2ColdStartProof.small_probe_requested == 1 &&
           AROSAppleStage2ColdStartProof.small_probe_seglist != 0 &&
           AROSAppleStage2ColdStartProof.small_probe_return == 73 &&
           AROSAppleStage2ColdStartProof.small_probe_bad_return == 30 &&
           AROSAppleStage2ColdStartProof.small_pages_loaded >
               AROSAppleStage2ColdStartProof.small_pages_before &&
           AROSAppleStage2ColdStartProof.small_pages_unloaded ==
               AROSAppleStage2ColdStartProof.small_pages_before &&
           AROSAppleStage2ColdStartProof.small_load_allocations != 0 &&
           AROSAppleStage2ColdStartProof.small_unload_frees != 0 &&
           AROSAppleStage2ColdStartProof.small_load_rx_protections != 0 &&
           AROSAppleStage2ColdStartProof.small_unload_rw_protections != 0)) ||
        !((AROSAppleStage2ColdStartProof.settings_probe_requested == 0 &&
           AROSAppleStage2ColdStartProof.settings_probe_seglist == 0 &&
           AROSAppleStage2ColdStartProof.settings_probe_return == 0 &&
           AROSAppleStage2ColdStartProof.settings_pages_before == 0 &&
           AROSAppleStage2ColdStartProof.settings_pages_loaded == 0 &&
           AROSAppleStage2ColdStartProof.settings_pages_unloaded == 0 &&
           AROSAppleStage2ColdStartProof.settings_load_allocations == 0 &&
           AROSAppleStage2ColdStartProof.settings_unload_frees == 0 &&
           AROSAppleStage2ColdStartProof.settings_load_rx_protections == 0 &&
           AROSAppleStage2ColdStartProof.settings_unload_rw_protections == 0) ||
          (AROSAppleStage2ColdStartProof.settings_probe_requested == 1 &&
           AROSAppleStage2ColdStartProof.settings_probe_seglist != 0 &&
           AROSAppleStage2ColdStartProof.settings_probe_return == 75 &&
           AROSAppleStage2ColdStartProof.settings_pages_loaded >
               AROSAppleStage2ColdStartProof.settings_pages_before &&
           AROSAppleStage2ColdStartProof.settings_load_allocations != 0 &&
           AROSAppleStage2ColdStartProof.settings_unload_frees != 0 &&
           AROSAppleStage2ColdStartProof.settings_load_rx_protections != 0)) ||
        (AROSAppleStage2ColdStartProof.mount_count != 1 &&
            !(AROSAppleStage2ColdStartProof.resident_count >= 54 &&
              AROSAppleStage2ColdStartProof.mount_count >= 2 &&
              AROSAppleStage2ColdStartProof.mount_count <=
                  (has_afs ? AROS_APPLE_DESKTOP_STORAGE_NODE_MAX : 3U))) ||
        AROSAppleStage2ColdStartProof.filesystem_entry_count !=
            (has_afs ? 6U : 4U) ||
        AROSAppleStage2ColdStartProof.timer_frequency == 0 ||
        AROSAppleStage2ColdStartProof.eclock_after <=
            AROSAppleStage2ColdStartProof.eclock_before ||
        AROSAppleStage2ColdStartProof.time_after_usec <
            AROSAppleStage2ColdStartProof.time_before_usec ||
        AROSAppleStage2ColdStartProof.housekeeper_task == 0 ||
        AROSAppleStage2ColdStartProof.guru_task == 0 ||
        AROSAppleStage2ColdStartProof.free_after >=
            AROSAppleStage2ColdStartProof.free_before ||
        AROSAppleStage2ColdStartProof.el0_is_super != 0 ||
        AROSAppleStage2ColdStartProof.return_status !=
            AROS_APPLE_COLDSTART_STATUS_PASS)
        return (int)AROS_APPLE_STAGE2_COLDSTART;
    AROSAppleStage2Phase = STAGE2_PHASE_READY;
    STAGE2_EYES(25);
    /* The desktop is up by here, so a person has a cue to type on. */
    {
        uint64_t daif;

        __asm__ volatile("mrs %0, DAIF" : "=r"(daif));
        if ((daif & UINT64_C(0x80)) == 0)
            return (int)AROS_APPLE_STAGE2_RUNTIME;
    }
    return (int)AROS_APPLE_STAGE2_OK;
}
