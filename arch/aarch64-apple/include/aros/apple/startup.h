/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 */

#ifndef AROS_APPLE_STARTUP_H
#define AROS_APPLE_STARTUP_H

#include <stdint.h>

#define AROS_APPLE_BOOT_MAGIC UINT32_C(0x41524f53)
#define AROS_APPLE_TAG_DONE UINT32_C(0)
#define AROS_APPLE_KRN_DUMMY UINT32_C(0x83d00000)
#define AROS_APPLE_KRN_KERNEL_BASE (AROS_APPLE_KRN_DUMMY + 1U)
#define AROS_APPLE_KRN_KERNEL_LOWEST (AROS_APPLE_KRN_DUMMY + 2U)
#define AROS_APPLE_KRN_KERNEL_HIGHEST (AROS_APPLE_KRN_DUMMY + 3U)
#define AROS_APPLE_KRN_KERNEL_BSS (AROS_APPLE_KRN_DUMMY + 4U)
#define AROS_APPLE_KRN_PROT_AREA_START (AROS_APPLE_KRN_DUMMY + 13U)
#define AROS_APPLE_KRN_PROT_AREA_END (AROS_APPLE_KRN_DUMMY + 14U)
#define AROS_APPLE_KRN_BOOT_LOADER (AROS_APPLE_KRN_DUMMY + 22U)
#define AROS_APPLE_KRN_MEM_LOWER (AROS_APPLE_KRN_DUMMY + 17U)
#define AROS_APPLE_KRN_MEM_UPPER (AROS_APPLE_KRN_DUMMY + 18U)
#define AROS_APPLE_KRN_FLATTENED_DEVICE_TREE \
    (AROS_APPLE_KRN_DUMMY + 32U)
#define AROS_APPLE_KRN_FB_ADDR (AROS_APPLE_KRN_DUMMY + 33U)
#define AROS_APPLE_KRN_FRAMEBUFFER_WIDTH (AROS_APPLE_KRN_DUMMY + 34U)
#define AROS_APPLE_KRN_FRAMEBUFFER_HEIGHT (AROS_APPLE_KRN_DUMMY + 35U)
#define AROS_APPLE_KRN_FRAMEBUFFER_DEPTH (AROS_APPLE_KRN_DUMMY + 36U)
#define AROS_APPLE_KRN_FRAMEBUFFER_PITCH (AROS_APPLE_KRN_DUMMY + 37U)
#define AROS_APPLE_KRN_FRAMEBUFFER_FORMAT (AROS_APPLE_KRN_DUMMY + 60U)
#define AROS_APPLE_KRN_FRAMEBUFFER_SIZE (AROS_APPLE_KRN_DUMMY + 61U)
#define AROS_APPLE_KRN_BOOT_IMAGE_START (AROS_APPLE_KRN_DUMMY + 40U)
#define AROS_APPLE_KRN_BOOT_IMAGE_SIZE (AROS_APPLE_KRN_DUMMY + 41U)
#define AROS_APPLE_KRN_PAGE_ARENA_START (AROS_APPLE_KRN_DUMMY + 42U)
#define AROS_APPLE_KRN_PAGE_ARENA_SIZE (AROS_APPLE_KRN_DUMMY + 43U)
#define AROS_APPLE_KRN_PAYLOAD_GENERATION (AROS_APPLE_KRN_DUMMY + 44U)
#define AROS_APPLE_KRN_PAYLOAD_SIZE (AROS_APPLE_KRN_DUMMY + 45U)
#define AROS_APPLE_KRN_PAYLOAD_SHA256_0 (AROS_APPLE_KRN_DUMMY + 46U)
#define AROS_APPLE_KRN_PAYLOAD_SHA256_1 (AROS_APPLE_KRN_DUMMY + 47U)
#define AROS_APPLE_KRN_PAYLOAD_SHA256_2 (AROS_APPLE_KRN_DUMMY + 48U)
#define AROS_APPLE_KRN_PAYLOAD_SHA256_3 (AROS_APPLE_KRN_DUMMY + 49U)
#define AROS_APPLE_KRN_PAYLOAD_SLOT (AROS_APPLE_KRN_DUMMY + 50U)
#define AROS_APPLE_KRN_PAYLOAD_FLAGS (AROS_APPLE_KRN_DUMMY + 51U)
#define AROS_APPLE_KRN_PAYLOAD_PRODUCER (AROS_APPLE_KRN_DUMMY + 52U)
#define AROS_APPLE_KRN_FDT_SIZE (AROS_APPLE_KRN_DUMMY + 53U)
#define AROS_APPLE_KRN_DEBUG_UART_START (AROS_APPLE_KRN_DUMMY + 54U)
#define AROS_APPLE_KRN_DEBUG_UART_END (AROS_APPLE_KRN_DUMMY + 55U)
/* Exact GIC windows mapped by stage two; all four tags or none. */
#define AROS_APPLE_KRN_GIC_DIST_START (AROS_APPLE_KRN_DUMMY + 56U)
#define AROS_APPLE_KRN_GIC_DIST_END (AROS_APPLE_KRN_DUMMY + 57U)
#define AROS_APPLE_KRN_GIC_CPU_START (AROS_APPLE_KRN_DUMMY + 58U)
#define AROS_APPLE_KRN_GIC_CPU_END (AROS_APPLE_KRN_DUMMY + 59U)
#define AROS_APPLE_KATTR_BOOT_IMAGE_START UINT32_C(0x83f10000)
#define AROS_APPLE_KATTR_BOOT_IMAGE_SIZE UINT32_C(0x83f10001)
#define AROS_APPLE_KATTR_PAYLOAD_IDENTITY UINT32_C(0x83f10002)
/*
 * CPU and board identity, recorded once at EL1 during kernel start so a task
 * at EL0, which cannot read the system registers itself, can ask for them.
 * The two strings are copies bounded by AROS_APPLE_IDENTITY_TEXT_BYTES.
 */
#define AROS_APPLE_KATTR_CPU_MIDR UINT32_C(0x83f10003)
#define AROS_APPLE_KATTR_CURRENT_EL UINT32_C(0x83f10004)
#define AROS_APPLE_KATTR_COUNTER_FREQUENCY UINT32_C(0x83f10005)
#define AROS_APPLE_KATTR_BOARD_MODEL UINT32_C(0x83f10006)
#define AROS_APPLE_KATTR_BOARD_COMPATIBLE UINT32_C(0x83f10007)
/* The boot loader's random seed (/chosen/rng-seed), taken once at EL1 and
 * wiped from the tree. The attribute returns a struct AROSAppleBootSeed while
 * length is non-zero; its one consumer, entropy.resource, folds the bytes
 * into its generator and clears the struct, after which it returns -1. */
#define AROS_APPLE_KATTR_BOOT_SEED UINT32_C(0x83f1000b)
#define AROS_APPLE_BOOT_SEED_BYTES 128U
struct AROSAppleBootSeed {
    uint32_t length;
    uint8_t bytes[AROS_APPLE_BOOT_SEED_BYTES];
};
/*
 * What set the system clock at boot. The desktop hold publishes it after
 * its firmware or platform clock attempt: a pointer to the struct once
 * published, -1 before. unix_seconds is 0 when the hold set nothing;
 * rtc_status is the provider-specific status of the attempt, or -1 when no
 * usable clock was available. Network time is not
 * here: SNTPSync writes ENV:SNTPSync/LastSync ("<unix seconds> <server>").
 */
#define AROS_APPLE_KATTR_SYSTEM_TIME UINT32_C(0x83f1000d)
#define AROS_APPLE_SYSTEM_TIME_VERSION UINT32_C(1)
#define AROS_APPLE_SYSTEM_TIME_RTC_SET UINT32_C(0x1)   /* the clock was set from a boot-time source */
struct AROSAppleSystemTimeInfo {
    uint32_t version;
    uint32_t size;
    uint32_t flags;             /* AROS_APPLE_SYSTEM_TIME_* */
    int32_t rtc_status;         /* provider-specific status, -1 when unavailable */
    uint64_t unix_seconds;      /* what was set, 0 when nothing was */
    uint64_t counter_at_set;    /* CNTVCT_EL0 then, 0 when nothing was */
};
#define AROS_APPLE_IDENTITY_TEXT_BYTES 64U

/* Eight memory extents, FDT extent, framebuffer, arena and identity need 44. */
#define AROS_APPLE_MAX_BOOT_TAGS 64U
#define AROS_APPLE_MAX_MEMORY_RANGES 8U
#define AROS_APPLE_FDT_MAX_SIZE (UINT64_C(2) * 1024U * 1024U)
#define AROS_APPLE_STARTUP_STATE_VERSION UINT32_C(7)
#define AROS_APPLE_STARTUP_MINIMUM_MEMORY (UINT64_C(1) << 20)
#define AROS_APPLE_STARTUP_MAX_RESIDENTS UINT32_C(256)
/* SYS0, ESP, WORK, AFSSYS and nonbootable DATA. */
#define AROS_APPLE_DESKTOP_STORAGE_NODE_MAX 5U
#define AROS_APPLE_MEMORY_PLAN_VERSION UINT32_C(1)
#define AROS_APPLE_MEMORY_PLAN_MAX_RANGES UINT32_C(2592)
#define AROS_APPLE_MEMORY_PLAN_PAGE_SIZE UINT64_C(4096)
#define AROS_APPLE_COLDSTART_PROOF_VERSION UINT32_C(17)
#define AROS_APPLE_COLDSTART_STATUS_PASS UINT64_C(0xc01d57a27c01d57a)

#define AROS_APPLE_MEMORY_RANGE_CONVENTIONAL UINT32_C(1)
#define AROS_APPLE_MEMORY_RANGE_RELEASE UINT32_C(2)
#define AROS_APPLE_MEMORY_RANGE_RETAIN UINT32_C(3)

struct AROSAppleTagItem {
    uint32_t tag;
    uint32_t reserved;
    uint64_t data;
};

enum AROSAppleStartupContractStatus {
    AROS_APPLE_STARTUP_CONTRACT_OK = 0,
    AROS_APPLE_STARTUP_CONTRACT_ARGUMENT,
    AROS_APPLE_STARTUP_CONTRACT_MAGIC,
    AROS_APPLE_STARTUP_CONTRACT_TERMINATION,
    AROS_APPLE_STARTUP_CONTRACT_RESERVED,
    AROS_APPLE_STARTUP_CONTRACT_DUPLICATE,
    AROS_APPLE_STARTUP_CONTRACT_REQUIRED,
    AROS_APPLE_STARTUP_CONTRACT_LAYOUT,
    AROS_APPLE_STARTUP_CONTRACT_MEMORY,
    AROS_APPLE_STARTUP_CONTRACT_FRAMEBUFFER,
    AROS_APPLE_STARTUP_CONTRACT_BOOT_IMAGE,
    AROS_APPLE_STARTUP_CONTRACT_PAGE_ARENA,
    AROS_APPLE_STARTUP_CONTRACT_PAYLOAD_IDENTITY,
    AROS_APPLE_STARTUP_CONTRACT_DEBUG_UART
};

enum AROSAppleKernelStartupPhase {
    AROS_APPLE_KERNEL_PHASE_NONE = 0,
    AROS_APPLE_KERNEL_PHASE_CONTRACT,
    AROS_APPLE_KERNEL_PHASE_MEMORY_HEADER,
    AROS_APPLE_KERNEL_PHASE_RESIDENT_SCAN,
    AROS_APPLE_KERNEL_PHASE_RESIDENT_READY,
    AROS_APPLE_KERNEL_PHASE_EXEC_INIT,
    AROS_APPLE_KERNEL_PHASE_EXEC_READY,
    AROS_APPLE_KERNEL_PHASE_SINGLE_TASK_INIT,
    AROS_APPLE_KERNEL_PHASE_SINGLE_TASK_READY,
    AROS_APPLE_KERNEL_PHASE_MEMORY_IMPORT,
    AROS_APPLE_KERNEL_PHASE_MEMORY_READY
};

enum AROSAppleKernelStartupStatus {
    AROS_APPLE_KERNEL_STARTUP_OK = 0,
    AROS_APPLE_KERNEL_STARTUP_ARGUMENT,
    AROS_APPLE_KERNEL_STARTUP_CONTRACT,
    AROS_APPLE_KERNEL_STARTUP_MEMORY_RANGE,
    AROS_APPLE_KERNEL_STARTUP_MEMORY_HEADER,
    AROS_APPLE_KERNEL_STARTUP_RESIDENT_LIST,
    AROS_APPLE_KERNEL_STARTUP_EXEC_MISSING,
    AROS_APPLE_KERNEL_STARTUP_EXEC_INIT,
    AROS_APPLE_KERNEL_STARTUP_SYSBASE,
    AROS_APPLE_KERNEL_STARTUP_SINGLE_TASK_INIT,
    AROS_APPLE_KERNEL_STARTUP_KERNEL_BASE,
    AROS_APPLE_KERNEL_STARTUP_MEMORY_PLAN,
    AROS_APPLE_KERNEL_STARTUP_MEMORY_IMPORT,
    AROS_APPLE_KERNEL_STARTUP_CPU_BIND
};

struct __attribute__((aligned(16))) AROSAppleMemoryPlanRange {
    uint64_t start;
    uint64_t end;
    uint32_t kind;
    uint32_t source_index;
    uint64_t reserved;
};

struct __attribute__((aligned(16))) AROSAppleMemoryPlan {
    uint32_t version;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_count;
    uint32_t conventional_count;
    uint32_t release_count;
    uint32_t retained_count;
    uint32_t reserved;
    uint64_t conventional_bytes;
    uint64_t release_bytes;
    uint64_t retained_bytes;
    uint64_t reserved64[3];
};

_Static_assert(sizeof(struct AROSAppleMemoryPlanRange) == 32,
    "Apple memory-plan range size");
_Static_assert(sizeof(struct AROSAppleMemoryPlan) == 80,
    "Apple memory-plan header size");

struct AROSApplePayloadIdentity {
    uint64_t generation_sequence;
    uint64_t size;
    uint8_t sha256[32];
    uint32_t slot;
    uint32_t flags;
    uint32_t producer;
    uint32_t reserved;
};

_Static_assert(sizeof(struct AROSApplePayloadIdentity) == 64,
    "Apple payload identity size");

struct AROSAppleFramebufferInfo {
    uint64_t physical_address;
    uint64_t size;
    uint64_t pitch;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t format;
};

_Static_assert(sizeof(struct AROSAppleFramebufferInfo) == 40,
    "Apple framebuffer info size");

struct AROSAppleKernelStartupState {
    uint32_t version;
    uint32_t size;
    uint32_t phase;
    uint32_t status;
    uint32_t detail;
    uint32_t resident_count;
    uint32_t reserved0;
    uint32_t reserved1;
    uint64_t memory_lower;
    uint64_t memory_upper;
    uint64_t memory_header;
    uint64_t memory_free;
    uint64_t resident_list;
    uint64_t exec_resident;
    uint64_t sys_base;
    uint64_t kernel_base;
    uint64_t memory_plan;
    uint64_t boot_image_start;
    uint64_t boot_image_size;
    uint64_t page_arena_start;
    uint64_t page_arena_size;
    uint64_t conventional_bytes;
    uint64_t reclaimed_bytes;
    uint64_t total_free_bytes;
    uint32_t memory_header_count;
    uint32_t conventional_range_count;
    uint32_t release_range_count;
    uint32_t retained_range_count;
    struct AROSApplePayloadIdentity payload_identity;
    uint64_t debug_uart_start;
    uint64_t debug_uart_end;
};

_Static_assert(sizeof(struct AROSAppleKernelStartupState) == 256,
    "Apple kernel startup state size");

struct __attribute__((aligned(16))) AROSAppleColdStartProof {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t detail;
    uint32_t resident_count;
    uint32_t kernel_index;
    uint32_t exec_index;
    uint32_t expansion_index;
    uint32_t pmgr_index;
    uint32_t rtkit_index;
    uint32_t utility_index;
    uint32_t bootloader_index;
    uint32_t task_index;
    uint32_t timer_index;
    uint32_t partition_index;
    uint32_t filesystem_index;
    uint32_t fat_index;
    uint32_t dos_index;
    uint32_t ready_count;
    uint32_t wait_count;
    uint32_t appleboot_index;
    uint32_t mount_count;
    uint64_t free_before;
    uint64_t free_after;
    uint64_t bootstrap_task;
    uint64_t bootstrap_context;
    uint64_t kernel_base;
    uint64_t utility_base;
    uint64_t task_base;
    uint64_t expansion_base;
    uint64_t bootloader_base;
    uint64_t partition_base;
    uint64_t housekeeper_task;
    uint64_t guru_task;
    uint64_t dispatch_count_before;
    uint64_t dispatch_count_after;
    uint64_t el0_is_super;
    uint64_t return_status;
    uint64_t timer_base;
    uint64_t timer_frequency;
    uint64_t eclock_before;
    uint64_t eclock_after;
    uint64_t time_before_usec;
    uint64_t time_after_usec;
    uint64_t filesystem_base;
    uint64_t fat_seglist;
    uint64_t dos_resident;
    uint64_t filesystem_entry_count;
    uint64_t appleboot_base;
    uint64_t boot_node;
    uint64_t boot_image_start;
    uint64_t boot_image_size;
    uint64_t boot_read_actual;
    uint64_t boot_bounds_error;
    uint64_t boot_write_error;
    uint64_t dos_base;
    uint64_t dos_boot_process;
    uint64_t sys_lock;
    uint64_t boot_volume_task;
    uint64_t standard_assign_count;
    uint64_t sentinel_read_actual;
    uint64_t sentinel_eof_actual;
    uint64_t boot_signature_read_actual;
    uint64_t abi_probe_seglist;
    uint64_t abi_probe_entry;
    uint64_t abi_probe_return;
    uint64_t abi_pages_before;
    uint64_t abi_pages_loaded;
    uint64_t abi_pages_unloaded;
    uint64_t abi_load_allocations;
    uint64_t abi_unload_frees;
    uint64_t abi_load_rx_protections;
    uint64_t abi_unload_rw_protections;
    uint64_t abi_probe_bad_return;
    uint64_t abi_probe_argument_size;
    uint64_t workbench_probe_requested;
    uint64_t workbench_probe_seglist;
    uint64_t workbench_probe_process;
    uint64_t workbench_probe_reply;
    uint64_t workbench_probe_return;
    uint64_t workbench_probe_numargs;
    uint32_t aros_index;
    uint32_t locale_index;
    uint32_t stdc_index;
    uint32_t stdcio_index;
    uint32_t posixc_index;
    uint32_t iffparse_index;
    uint64_t aros_base;
    uint64_t iffparse_base;
    uint64_t locale_base;
    uint64_t stdc_base;
    uint64_t stdcio_base;
    uint64_t posixc_base;
    uint32_t aros_version;
    uint32_t iffparse_version;
    uint32_t locale_version;
    uint32_t stdc_version;
    uint32_t stdcio_version;
    uint32_t posixc_version;
    uint64_t tls_probe_requested;
    uint64_t tls_probe_seglist;
    uint64_t tls_probe_return;
    uint64_t tls_probe_argument_size;
    uint64_t tls_pages_before;
    uint64_t tls_pages_loaded;
    uint64_t tls_pages_unloaded;
    uint64_t tls_load_allocations;
    uint64_t tls_unload_frees;
    uint64_t tls_load_rx_protections;
    uint64_t tls_unload_rw_protections;
    uint64_t wanderer_requested;
    uint64_t wanderer_seglist;
    uint64_t wanderer_process;
    uint64_t wanderer_screen;
    uint64_t wanderer_window;
    uint64_t wanderer_pixel;
    uint64_t wanderer_initial_cli_process;
    uint64_t wanderer_initial_cli_seen;
    uint64_t wanderer_initial_cli_closed;
    uint64_t runtime_exception_vector;
    uint64_t runtime_exception_esr;
    uint64_t runtime_exception_far;
    uint64_t runtime_exception_elr;
    uint64_t runtime_exception_spsr;
    uint64_t runtime_exception_lr;
    uint64_t runtime_exception_sp;
    uint64_t runtime_exception_task;
    char runtime_exception_task_name[32];
    /* Small code model command (SMLTEST.REQ): page-relative relocations
     * through the native loader. Appended so earlier offsets stay fixed. */
    uint64_t small_probe_requested;
    uint64_t small_probe_seglist;
    uint64_t small_probe_return;
    uint64_t small_probe_bad_return;
    uint64_t small_pages_before;
    uint64_t small_pages_loaded;
    uint64_t small_pages_unloaded;
    uint64_t small_load_allocations;
    uint64_t small_unload_frees;
    uint64_t small_load_rx_protections;
    uint64_t small_unload_rw_protections;
    /* settings.library round trip (SETTEST.REQ): the probe mounts RAM:,
     * moves ENV: there and initialises the library from LIBS: itself; all
     * three stay, so the step checks the load and the return, no exact
     * page recovery. */
    uint64_t settings_probe_requested;
    uint64_t settings_probe_seglist;
    uint64_t settings_probe_return;
    uint64_t settings_pages_before;
    uint64_t settings_pages_loaded;
    uint64_t settings_pages_unloaded;
    uint64_t settings_load_allocations;
    uint64_t settings_unload_frees;
    uint64_t settings_load_rx_protections;
    uint64_t settings_unload_rw_protections;
};

_Static_assert(sizeof(struct AROSAppleColdStartProof) == 1088,
    "Apple cold-start proof size");

struct AROSAppleRuntimeFrame;
void AROSAppleColdStartRecordRuntimeException(
    const struct AROSAppleRuntimeFrame *frame);

int AROSAppleMemoryPlanValidate(const struct AROSAppleMemoryPlan *plan);

int AROSAppleKernelRetainedBootImage(
    uintptr_t *image_start, uint64_t *image_size);

int AROSAppleKernelRetainedPayloadIdentity(
    struct AROSApplePayloadIdentity *identity);

int AROSAppleKernelRetainedFramebuffer(
    struct AROSAppleFramebufferInfo *framebuffer);

enum AROSAppleStartupContractStatus AROSAppleKernelStartupContract(
    const struct AROSAppleTagItem *tags, uint32_t magic);

enum AROSAppleKernelStartupStatus AROSAppleKernelPrepareMemory(
    const struct AROSAppleTagItem *tags,
    uint32_t magic,
    uint64_t proven_lower,
    uint64_t proven_upper,
    struct AROSAppleKernelStartupState *state);

enum AROSAppleKernelStartupStatus AROSAppleKernelInitializeExec(
    const struct AROSAppleTagItem *tags,
    struct AROSAppleKernelStartupState *state);

enum AROSAppleKernelStartupStatus AROSAppleKernelInitializeSingleTask(
    const struct AROSAppleTagItem *tags,
    struct AROSAppleKernelStartupState *state);

enum AROSAppleKernelStartupStatus AROSAppleKernelImportMemory(
    const struct AROSAppleTagItem *tags,
    const struct AROSAppleMemoryPlan *plan,
    struct AROSAppleKernelStartupState *state);

int AROSAppleKernelProveCoreColdStart(
    uintptr_t user_stack, uint64_t user_stack_size,
    const struct AROSAppleKernelStartupState *state,
    struct AROSAppleColdStartProof *proof);

#endif /* AROS_APPLE_STARTUP_H */
