/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Prepare the first Apple memory header, resident list and ExecBase.
 */

#include <aros/apple/startup.h>

#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/nodes.h>
#include <exec/resident.h>
#include <proto/exec.h>
#include <utility/tagitem.h>

#include "kernel_base.h"
#include "kernel_globals.h"
#include "kernel_intern.h"
#include "kernel_romtags.h"
#include "page_allocator.h"

extern struct ExecBase *SysBase;

static const char boot_memory_name[] = "Apple boot memory";
#include "cpu_monitor.h"

static uintptr_t retained_boot_image_start;
static uint64_t retained_boot_image_size;
static struct AROSApplePayloadIdentity retained_payload_identity;
static struct AROSAppleFramebufferInfo retained_framebuffer;

int AROSAppleKernelRetainedBootImage(
    uintptr_t *image_start, uint64_t *image_size)
{
    if (image_start == NULL || image_size == NULL ||
        retained_boot_image_start == 0 || retained_boot_image_size == 0)
        return 0;
    *image_start = retained_boot_image_start;
    *image_size = retained_boot_image_size;
    return 1;
}

int AROSAppleKernelRetainedPayloadIdentity(
    struct AROSApplePayloadIdentity *identity)
{
    if (identity == NULL || retained_payload_identity.size == 0)
        return 0;
    *identity = retained_payload_identity;
    return 1;
}

int AROSAppleKernelRetainedFramebuffer(
    struct AROSAppleFramebufferInfo *framebuffer)
{
    if (framebuffer == NULL || retained_framebuffer.size == 0)
        return 0;
    *framebuffer = retained_framebuffer;
    return 1;
}

static void initialize_state(struct AROSAppleKernelStartupState *state)
{
    uint32_t index;

    state->version = AROS_APPLE_STARTUP_STATE_VERSION;
    state->size = sizeof(*state);
    state->phase = AROS_APPLE_KERNEL_PHASE_NONE;
    state->status = AROS_APPLE_KERNEL_STARTUP_OK;
    state->detail = 0;
    state->resident_count = 0;
    state->reserved0 = 0;
    state->reserved1 = 0;
    state->memory_lower = 0;
    state->memory_upper = 0;
    state->memory_header = 0;
    state->memory_free = 0;
    state->resident_list = 0;
    state->exec_resident = 0;
    state->sys_base = 0;
    state->kernel_base = 0;
    state->memory_plan = 0;
    state->boot_image_start = 0;
    state->boot_image_size = 0;
    state->page_arena_start = 0;
    state->page_arena_size = 0;
    state->conventional_bytes = 0;
    state->reclaimed_bytes = 0;
    state->total_free_bytes = 0;
    state->memory_header_count = 0;
    state->conventional_range_count = 0;
    state->release_range_count = 0;
    state->retained_range_count = 0;
    state->payload_identity.generation_sequence = 0;
    state->payload_identity.size = 0;
    for (index = 0; index < 32; ++index)
        state->payload_identity.sha256[index] = 0;
    state->payload_identity.slot = 0;
    state->payload_identity.flags = 0;
    state->payload_identity.producer = 0;
    state->payload_identity.reserved = 0;
    state->debug_uart_start = 0;
    state->debug_uart_end = 0;
}

static void store_u64_le(uint8_t *bytes, uint64_t value)
{
    uint32_t index;

    for (index = 0; index < 8; ++index)
        bytes[index] = (uint8_t)(value >> (index * 8U));
}

static enum AROSAppleKernelStartupStatus fail(
    struct AROSAppleKernelStartupState *state,
    enum AROSAppleKernelStartupStatus status,
    uint32_t detail)
{
    state->status = status;
    state->detail = detail;
    return status;
}

static int collect_required_values(
    const struct AROSAppleTagItem *tags,
    uint64_t proven_lower,
    uint64_t proven_upper,
    uint64_t *kernel_lower,
    uint64_t *kernel_upper,
    uint64_t *boot_image_start,
    uint64_t *boot_image_size,
    uint64_t *page_arena_start,
    uint64_t *page_arena_size,
    uint64_t *debug_uart_start,
    uint64_t *debug_uart_end,
    struct AROSAppleFramebufferInfo *framebuffer,
    struct AROSAppleKernelStartupState *state)
{
    uint64_t open_lower = 0;
    uint32_t index;
    int have_lower = 0;
    int proven_pair = 0;

    *kernel_lower = 0;
    *kernel_upper = 0;
    *boot_image_start = 0;
    *boot_image_size = 0;
    *page_arena_start = 0;
    *page_arena_size = 0;
    *debug_uart_start = 0;
    *debug_uart_end = 0;
    framebuffer->physical_address = 0;
    framebuffer->size = 0;
    framebuffer->pitch = 0;
    framebuffer->width = 0;
    framebuffer->height = 0;
    framebuffer->depth = 0;
    framebuffer->format = 0;
    for (index = 0; index < AROS_APPLE_MAX_BOOT_TAGS; ++index) {
        switch (tags[index].tag) {
        case AROS_APPLE_TAG_DONE:
            return proven_pair && *kernel_lower != 0 &&
                *kernel_upper > *kernel_lower;
        case AROS_APPLE_KRN_KERNEL_LOWEST:
            *kernel_lower = tags[index].data;
            break;
        case AROS_APPLE_KRN_KERNEL_HIGHEST:
            *kernel_upper = tags[index].data;
            break;
        case AROS_APPLE_KRN_BOOT_IMAGE_START:
            *boot_image_start = tags[index].data;
            break;
        case AROS_APPLE_KRN_BOOT_IMAGE_SIZE:
            *boot_image_size = tags[index].data;
            break;
        case AROS_APPLE_KRN_PAGE_ARENA_START:
            *page_arena_start = tags[index].data;
            break;
        case AROS_APPLE_KRN_PAGE_ARENA_SIZE:
            *page_arena_size = tags[index].data;
            break;
        case AROS_APPLE_KRN_DEBUG_UART_START:
            *debug_uart_start = tags[index].data;
            break;
        case AROS_APPLE_KRN_DEBUG_UART_END:
            *debug_uart_end = tags[index].data;
            break;
        case AROS_APPLE_KRN_FB_ADDR:
            framebuffer->physical_address = tags[index].data;
            break;
        case AROS_APPLE_KRN_FRAMEBUFFER_WIDTH:
            framebuffer->width = (uint32_t)tags[index].data;
            break;
        case AROS_APPLE_KRN_FRAMEBUFFER_HEIGHT:
            framebuffer->height = (uint32_t)tags[index].data;
            break;
        case AROS_APPLE_KRN_FRAMEBUFFER_DEPTH:
            framebuffer->depth = (uint32_t)tags[index].data;
            break;
        case AROS_APPLE_KRN_FRAMEBUFFER_PITCH:
            framebuffer->pitch = tags[index].data;
            break;
        case AROS_APPLE_KRN_FRAMEBUFFER_FORMAT:
            framebuffer->format = (uint32_t)tags[index].data;
            break;
        case AROS_APPLE_KRN_FRAMEBUFFER_SIZE:
            framebuffer->size = tags[index].data;
            break;
        case AROS_APPLE_KRN_PAYLOAD_GENERATION:
            state->payload_identity.generation_sequence = tags[index].data;
            break;
        case AROS_APPLE_KRN_PAYLOAD_SIZE:
            state->payload_identity.size = tags[index].data;
            break;
        case AROS_APPLE_KRN_PAYLOAD_SHA256_0:
            store_u64_le(&state->payload_identity.sha256[0], tags[index].data);
            break;
        case AROS_APPLE_KRN_PAYLOAD_SHA256_1:
            store_u64_le(&state->payload_identity.sha256[8], tags[index].data);
            break;
        case AROS_APPLE_KRN_PAYLOAD_SHA256_2:
            store_u64_le(&state->payload_identity.sha256[16], tags[index].data);
            break;
        case AROS_APPLE_KRN_PAYLOAD_SHA256_3:
            store_u64_le(&state->payload_identity.sha256[24], tags[index].data);
            break;
        case AROS_APPLE_KRN_PAYLOAD_SLOT:
            state->payload_identity.slot = (uint32_t)tags[index].data;
            break;
        case AROS_APPLE_KRN_PAYLOAD_FLAGS:
            state->payload_identity.flags = (uint32_t)tags[index].data;
            break;
        case AROS_APPLE_KRN_PAYLOAD_PRODUCER:
            state->payload_identity.producer = (uint32_t)tags[index].data;
            break;
        case AROS_APPLE_KRN_MEM_LOWER:
            if (have_lower)
                return 0;
            open_lower = tags[index].data;
            have_lower = 1;
            break;
        case AROS_APPLE_KRN_MEM_UPPER:
            if (!have_lower)
                return 0;
            if (open_lower == proven_lower &&
                tags[index].data == proven_upper)
                proven_pair = 1;
            have_lower = 0;
            break;
        default:
            break;
        }
    }
    return 0;
}

enum AROSAppleKernelStartupStatus AROSAppleKernelPrepareMemory(
    const struct AROSAppleTagItem *tags,
    uint32_t magic,
    uint64_t proven_lower,
    uint64_t proven_upper,
    struct AROSAppleKernelStartupState *state)
{
    struct MemHeader *memory_header;
    struct Resident **resident_list;
    struct Resident *exec_resident;
    UWORD *ranges[3];
    uint64_t kernel_lower;
    uint64_t kernel_upper;
    uint64_t boot_image_start;
    uint64_t boot_image_size;
    uint64_t page_arena_start;
    uint64_t page_arena_size;
    uint64_t debug_uart_start;
    uint64_t debug_uart_end;
    struct AROSAppleFramebufferInfo framebuffer;
    uint64_t memory_size;
    uint32_t resident_count;
    ULONG attributes = MEMF_CHIP | MEMF_FAST | MEMF_PUBLIC | MEMF_KICK |
        MEMF_LOCAL;
    enum AROSAppleStartupContractStatus contract_status;

    /* Firmware does not own our CPU anchor. Clear it before any possible
     * Exec access; the monitor binds a validated record before activation. */
    __asm__ volatile("msr tpidrro_el0,xzr\n\tisb" ::: "memory");

    if (tags == NULL || state == NULL ||
        ((uintptr_t)state & 15U) != 0)
        return AROS_APPLE_KERNEL_STARTUP_ARGUMENT;
    retained_boot_image_start = 0;
    retained_boot_image_size = 0;
    retained_payload_identity.size = 0;
    retained_framebuffer.size = 0;
    initialize_state(state);
    state->phase = AROS_APPLE_KERNEL_PHASE_CONTRACT;
    contract_status = AROSAppleKernelStartupContract(tags, magic);
    if (contract_status != AROS_APPLE_STARTUP_CONTRACT_OK)
        return fail(state, AROS_APPLE_KERNEL_STARTUP_CONTRACT,
            (uint32_t)contract_status);
    if (proven_lower > UINTPTR_MAX || proven_upper > UINTPTR_MAX ||
        proven_upper <= proven_lower ||
        proven_upper - proven_lower < AROS_APPLE_STARTUP_MINIMUM_MEMORY ||
        !collect_required_values(tags, proven_lower, proven_upper,
            &kernel_lower, &kernel_upper,
            &boot_image_start, &boot_image_size,
            &page_arena_start, &page_arena_size,
            &debug_uart_start, &debug_uart_end, &framebuffer, state))
        return fail(state, AROS_APPLE_KERNEL_STARTUP_MEMORY_RANGE, 0);

    state->boot_image_start = boot_image_start;
    state->boot_image_size = boot_image_size;
    state->page_arena_start = page_arena_start;
    state->page_arena_size = page_arena_size;
    state->debug_uart_start = debug_uart_start;
    state->debug_uart_end = debug_uart_end;
    AROSAppleKernelDebugInitialize((uintptr_t)debug_uart_start,
        debug_uart_end - debug_uart_start);
    {
        uint64_t fdt_address = 0;
        uint64_t fdt_size = 0;
        uint32_t tag_index;

        for (tag_index = 0; tag_index < AROS_APPLE_MAX_BOOT_TAGS; ++tag_index) {
            if (tags[tag_index].tag == AROS_APPLE_TAG_DONE)
                break;
            if (tags[tag_index].tag == AROS_APPLE_KRN_FLATTENED_DEVICE_TREE)
                fdt_address = tags[tag_index].data;
            else if (tags[tag_index].tag == AROS_APPLE_KRN_FDT_SIZE)
                fdt_size = tags[tag_index].data;
        }
        if (fdt_address != 0 && fdt_address <= UINTPTR_MAX &&
            fdt_size <= AROS_APPLE_FDT_MAX_SIZE) {
            AROSAppleKernelRetainDeviceTree(
                (const void *)(uintptr_t)fdt_address, fdt_size);
            AROSAppleKernelRecordIdentity();
            AROSCPUInitMonitor((const void *)(uintptr_t)fdt_address, fdt_size);
            AROSAppleKernelDebugSelectVirtConsole(
                (const void *)(uintptr_t)fdt_address, fdt_size);
        } else {
            /* Even absent firmware needs an observed boot CPU and EL0 anchor. */
            AROSCPUInitMonitor(NULL, 0);
        }
    }
    /* Exec SMP must never reach a raw TLS/table access without an anchor.
     * Preserve a typed boot failure instead of trapping during allocation. */
    if (AROSCPUCurrentIndex() == AROS_CPU_MAX)
        return fail(state, AROS_APPLE_KERNEL_STARTUP_CPU_BIND, 0);
    retained_boot_image_start = (uintptr_t)boot_image_start;
    retained_boot_image_size = boot_image_size;
    retained_payload_identity = state->payload_identity;
    retained_framebuffer = framebuffer;
    if (!AROSApplePageAllocatorInitialize(
            (uintptr_t)page_arena_start, page_arena_size))
        return fail(state, AROS_APPLE_KERNEL_STARTUP_MEMORY_RANGE, 1);

    memory_size = proven_upper - proven_lower;
    state->memory_lower = proven_lower;
    state->memory_upper = proven_upper;
    state->phase = AROS_APPLE_KERNEL_PHASE_MEMORY_HEADER;
    memory_header = (struct MemHeader *)(uintptr_t)proven_lower;
    krnCreateMemHeader(boot_memory_name, 0, memory_header,
        (IPTR)memory_size, attributes);
    if (memory_header->mh_Node.ln_Type != NT_MEMORY ||
        memory_header->mh_Lower != (APTR)(uintptr_t)proven_lower ||
        memory_header->mh_Upper != (APTR)(uintptr_t)proven_upper ||
        memory_header->mh_First !=
            (struct MemChunk *)(uintptr_t)(proven_lower + MEMHEADER_TOTAL) ||
        memory_header->mh_First->mc_Next != NULL ||
        memory_header->mh_First->mc_Bytes !=
            (IPTR)(memory_size - MEMHEADER_TOTAL) ||
        memory_header->mh_Free != (IPTR)(memory_size - MEMHEADER_TOTAL) ||
        (memory_header->mh_Attributes & attributes) != attributes)
        return fail(state, AROS_APPLE_KERNEL_STARTUP_MEMORY_HEADER, 0);

    state->memory_header = (uintptr_t)memory_header;
    state->memory_free = memory_header->mh_Free;
    state->phase = AROS_APPLE_KERNEL_PHASE_RESIDENT_SCAN;
    ranges[0] = (UWORD *)(uintptr_t)kernel_lower;
    ranges[1] = (UWORD *)(uintptr_t)kernel_upper;
    ranges[2] = (UWORD *)~(uintptr_t)0;
    resident_list = krnRomTagScanner(memory_header, ranges);
    if (resident_list == NULL)
        return fail(state, AROS_APPLE_KERNEL_STARTUP_RESIDENT_LIST, 0);
    for (resident_count = 0;
        resident_count < AROS_APPLE_STARTUP_MAX_RESIDENTS &&
            resident_list[resident_count] != NULL;
        ++resident_count) {
    }
    if (resident_count == 0 ||
        resident_count == AROS_APPLE_STARTUP_MAX_RESIDENTS)
        return fail(state, AROS_APPLE_KERNEL_STARTUP_RESIDENT_LIST,
            resident_count);
    exec_resident = krnFindResident(resident_list, "exec.library");
    if (exec_resident == NULL ||
        exec_resident->rt_MatchWord != RTC_MATCHWORD ||
        exec_resident->rt_MatchTag != exec_resident)
        return fail(state, AROS_APPLE_KERNEL_STARTUP_EXEC_MISSING, 0);

    BootMsg = (struct TagItem *)(uintptr_t)tags;
    state->resident_count = resident_count;
    state->resident_list = (uintptr_t)resident_list;
    state->exec_resident = (uintptr_t)exec_resident;
    state->memory_free = memory_header->mh_Free;
    state->phase = AROS_APPLE_KERNEL_PHASE_RESIDENT_READY;
    return AROS_APPLE_KERNEL_STARTUP_OK;
}

enum AROSAppleKernelStartupStatus AROSAppleKernelInitializeExec(
    const struct AROSAppleTagItem *tags,
    struct AROSAppleKernelStartupState *state)
{
    struct MemHeader *memory_header;
    struct Resident **resident_list;
    struct Resident *exec_resident;
    struct ExecBase *sys_base;

    if (tags == NULL || state == NULL ||
        ((uintptr_t)state & 15U) != 0 ||
        state->version != AROS_APPLE_STARTUP_STATE_VERSION ||
        state->size != sizeof(*state) ||
        state->phase != AROS_APPLE_KERNEL_PHASE_RESIDENT_READY ||
        state->status != AROS_APPLE_KERNEL_STARTUP_OK ||
        state->memory_header == 0 || state->resident_list == 0 ||
        state->exec_resident == 0)
        return AROS_APPLE_KERNEL_STARTUP_ARGUMENT;

    memory_header = (struct MemHeader *)(uintptr_t)state->memory_header;
    resident_list = (struct Resident **)(uintptr_t)state->resident_list;
    exec_resident = (struct Resident *)(uintptr_t)state->exec_resident;
    state->phase = AROS_APPLE_KERNEL_PHASE_EXEC_INIT;
    sys_base = krnInitExecBase(exec_resident, memory_header,
        (struct TagItem *)(uintptr_t)tags);
    if (sys_base == NULL)
        return fail(state, AROS_APPLE_KERNEL_STARTUP_EXEC_INIT, 0);
    if (SysBase != sys_base)
        return fail(state, AROS_APPLE_KERNEL_STARTUP_SYSBASE, 0);

    sys_base->ResModules = resident_list;
    if (memory_header->mh_Node.ln_Succ == NULL ||
        memory_header->mh_Node.ln_Pred == NULL ||
        sys_base->ResModules != resident_list)
        return fail(state, AROS_APPLE_KERNEL_STARTUP_SYSBASE, 1);

    state->memory_free = memory_header->mh_Free;
    state->sys_base = (uintptr_t)sys_base;
    state->phase = AROS_APPLE_KERNEL_PHASE_EXEC_READY;
    return AROS_APPLE_KERNEL_STARTUP_OK;
}

static int resource_list_contains_kernel(
    const struct List *resources,
    const struct KernelBase *kernel_base)
{
    const struct Node *node = resources->lh_Head;
    uint32_t count;

    for (count = 0; count < AROS_APPLE_STARTUP_MAX_RESIDENTS &&
            node != NULL && node->ln_Succ != NULL; ++count) {
        if (node == &kernel_base->kb_Node)
            return 1;
        node = node->ln_Succ;
    }
    return 0;
}

static int startup_name_equal(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

enum AROSAppleKernelStartupStatus AROSAppleKernelInitializeSingleTask(
    const struct AROSAppleTagItem *tags,
    struct AROSAppleKernelStartupState *state)
{
    struct MemHeader *memory_header;
    IPTR free_before;

    if (tags == NULL || state == NULL ||
        ((uintptr_t)state & 15U) != 0 ||
        state->version != AROS_APPLE_STARTUP_STATE_VERSION ||
        state->size != sizeof(*state) ||
        state->phase != AROS_APPLE_KERNEL_PHASE_EXEC_READY ||
        state->status != AROS_APPLE_KERNEL_STARTUP_OK ||
        state->memory_header == 0 || state->sys_base == 0 ||
        SysBase != (struct ExecBase *)(uintptr_t)state->sys_base ||
        KernelBase != NULL)
        return AROS_APPLE_KERNEL_STARTUP_ARGUMENT;

    memory_header = (struct MemHeader *)(uintptr_t)state->memory_header;
    free_before = memory_header->mh_Free;
    state->phase = AROS_APPLE_KERNEL_PHASE_SINGLE_TASK_INIT;
    InitCode(RTF_SINGLETASK, 0);
    if (KernelBase == NULL)
        return fail(state, AROS_APPLE_KERNEL_STARTUP_SINGLE_TASK_INIT, 0);
    if (KernelBase->kb_Node.ln_Type != NT_RESOURCE ||
        KernelBase->kb_Node.ln_Name == NULL ||
        !startup_name_equal(KernelBase->kb_Node.ln_Name,
            "kernel.resource") ||
        !resource_list_contains_kernel(&SysBase->ResourceList, KernelBase) ||
        memory_header->mh_Free >= free_before)
        return fail(state, AROS_APPLE_KERNEL_STARTUP_KERNEL_BASE, 0);

    state->memory_free = memory_header->mh_Free;
    state->kernel_base = (uintptr_t)KernelBase;
    state->phase = AROS_APPLE_KERNEL_PHASE_SINGLE_TASK_READY;
    return AROS_APPLE_KERNEL_STARTUP_OK;
}

static const struct AROSAppleMemoryPlanRange *memory_plan_range_at(
    const struct AROSAppleMemoryPlan *plan, uint32_t index)
{
    return (const struct AROSAppleMemoryPlanRange *)(
        (const UBYTE *)plan + sizeof(*plan) +
        (uint64_t)index * plan->entry_size);
}

static int startup_range_contains(
    uint64_t outer_start, uint64_t outer_end,
    uint64_t inner_start, uint64_t inner_size)
{
    uint64_t inner_end;

    return inner_size != 0 && inner_start >= outer_start &&
        inner_start <= UINT64_MAX - inner_size &&
        (inner_end = inner_start + inner_size) <= outer_end;
}

static int startup_range_overlaps(
    uint64_t first_start, uint64_t first_end,
    uint64_t second_start, uint64_t second_end)
{
    return first_start < second_end && second_start < first_end;
}

static uint32_t memory_list_count_and_free(
    const struct List *list, uint64_t *free_bytes)
{
    const struct MemHeader *header =
        (const struct MemHeader *)list->lh_Head;
    uint32_t count = 0;

    *free_bytes = 0;
    while (header != NULL && header->mh_Node.ln_Succ != NULL) {
        if (header->mh_Node.ln_Type != NT_MEMORY || header->mh_Free < 0 ||
            UINT64_MAX - *free_bytes < (uint64_t)header->mh_Free)
            return UINT32_MAX;
        *free_bytes += (uint64_t)header->mh_Free;
        if (++count == UINT32_MAX)
            return UINT32_MAX;
        header = (const struct MemHeader *)header->mh_Node.ln_Succ;
    }
    return count;
}

enum AROSAppleKernelStartupStatus AROSAppleKernelImportMemory(
    const struct AROSAppleTagItem *tags,
    const struct AROSAppleMemoryPlan *plan,
    struct AROSAppleKernelStartupState *state)
{
    static const char conventional_name[] = "Apple conventional memory";
    static const char reclaimed_name[] = "Apple reclaimed loader memory";
    const struct AROSAppleMemoryPlanRange *range;
    const struct AROSAppleMemoryPlanRange *other;
    const struct MemHeader *existing;
    struct MemHeader *header;
    uint64_t plan_address = (uintptr_t)plan;
    uint64_t state_address = (uintptr_t)state;
    uint64_t total_free;
    uint64_t size;
    uint32_t existing_count;
    uint32_t expected_headers;
    uint32_t imported = 0;
    uint32_t containing_plan = 0;
    uint32_t containing_state = 0;
    uint32_t index;
    uint32_t other_index;
    int initial_found = 0;
    APTR probe;
    IPTR free_before;
    /* The software graphics path can use any imported normal RAM for legacy
     * MEMF_CHIP requests. Restricting those to the initial bootstrap range
     * starves sprite allocations as soon as that small range fragments. */
    ULONG attributes = MEMF_CHIP | MEMF_FAST | MEMF_PUBLIC | MEMF_KICK |
        MEMF_LOCAL;

    if (tags == NULL || plan == NULL || state == NULL ||
        ((uintptr_t)state & 15U) != 0 ||
        state->version != AROS_APPLE_STARTUP_STATE_VERSION ||
        state->size != sizeof(*state) ||
        state->phase != AROS_APPLE_KERNEL_PHASE_SINGLE_TASK_READY ||
        state->status != AROS_APPLE_KERNEL_STARTUP_OK ||
        state->memory_header == 0 || state->sys_base == 0 ||
        state->kernel_base == 0 || SysBase !=
            (struct ExecBase *)(uintptr_t)state->sys_base ||
        KernelBase != (struct KernelBase *)(uintptr_t)state->kernel_base ||
        AROSAppleKernelStartupContract(tags, AROS_APPLE_BOOT_MAGIC) !=
            AROS_APPLE_STARTUP_CONTRACT_OK ||
        !AROSAppleMemoryPlanValidate(plan))
        return AROS_APPLE_KERNEL_STARTUP_ARGUMENT;

    state->phase = AROS_APPLE_KERNEL_PHASE_MEMORY_IMPORT;
    expected_headers = plan->conventional_count + plan->release_count;
    if (expected_headers < plan->conventional_count)
        return fail(state, AROS_APPLE_KERNEL_STARTUP_MEMORY_PLAN, 0);
    existing_count = memory_list_count_and_free(&SysBase->MemList,
        &total_free);
    if (existing_count != 1 ||
        SysBase->MemList.lh_Head !=
            (struct Node *)(uintptr_t)state->memory_header)
        return fail(state, AROS_APPLE_KERNEL_STARTUP_MEMORY_IMPORT, 0);

    for (index = 0; index < plan->entry_count; ++index) {
        range = memory_plan_range_at(plan, index);
        if (range->kind == AROS_APPLE_MEMORY_RANGE_RETAIN) {
            containing_plan += startup_range_contains(range->start,
                range->end, plan_address, plan->size);
            containing_state += startup_range_contains(range->start,
                range->end, state_address, sizeof(*state));
            continue;
        }
        if (range->end - range->start < MEMHEADER_TOTAL + MEMCHUNK_TOTAL)
            return fail(state, AROS_APPLE_KERNEL_STARTUP_MEMORY_PLAN,
                index);
        if (range->kind == AROS_APPLE_MEMORY_RANGE_CONVENTIONAL &&
            range->start == state->memory_lower &&
            range->end == state->memory_upper) {
            if (initial_found)
                return fail(state, AROS_APPLE_KERNEL_STARTUP_MEMORY_PLAN,
                    index);
            initial_found = 1;
            ++imported;
            continue;
        }
        existing = (const struct MemHeader *)SysBase->MemList.lh_Head;
        while (existing != NULL && existing->mh_Node.ln_Succ != NULL) {
            if (startup_range_overlaps(range->start, range->end,
                    (uintptr_t)existing->mh_Lower,
                    (uintptr_t)existing->mh_Upper))
                return fail(state, AROS_APPLE_KERNEL_STARTUP_MEMORY_IMPORT,
                    index);
            existing = (const struct MemHeader *)existing->mh_Node.ln_Succ;
        }
        for (other_index = index + 1U;
                other_index < plan->entry_count; ++other_index) {
            other = memory_plan_range_at(plan, other_index);
            if (other->kind == AROS_APPLE_MEMORY_RANGE_RETAIN)
                continue;
            if (startup_range_overlaps(range->start, range->end,
                    other->start, other->end))
                return fail(state, AROS_APPLE_KERNEL_STARTUP_MEMORY_PLAN,
                    index);
        }
    }
    if (!initial_found || containing_plan != 1 || containing_state != 1)
        return fail(state, AROS_APPLE_KERNEL_STARTUP_MEMORY_PLAN, 0);

    for (index = 0; index < plan->entry_count; ++index) {
        range = memory_plan_range_at(plan, index);
        if (range->kind == AROS_APPLE_MEMORY_RANGE_RETAIN ||
            (range->kind == AROS_APPLE_MEMORY_RANGE_CONVENTIONAL &&
             range->start == state->memory_lower &&
             range->end == state->memory_upper))
            continue;
        size = range->end - range->start;
        AddMemList((IPTR)size, attributes, 0,
            (APTR)(uintptr_t)range->start,
            (STRPTR)(range->kind == AROS_APPLE_MEMORY_RANGE_RELEASE ?
                reclaimed_name : conventional_name));
        header = (struct MemHeader *)(uintptr_t)range->start;
        if (header->mh_Node.ln_Type != NT_MEMORY ||
            header->mh_First != (struct MemChunk *)(uintptr_t)(
                range->start + MEMHEADER_TOTAL) ||
            header->mh_Upper != (APTR)(uintptr_t)range->end ||
            header->mh_Free != (IPTR)(size - MEMHEADER_TOTAL) ||
            header->mh_First->mc_Next != NULL ||
            header->mh_First->mc_Bytes != (IPTR)(size - MEMHEADER_TOTAL))
            return fail(state, AROS_APPLE_KERNEL_STARTUP_MEMORY_IMPORT,
                index);
        if (range->kind == AROS_APPLE_MEMORY_RANGE_RELEASE) {
            free_before = header->mh_Free;
            probe = Allocate(header, MEMCHUNK_TOTAL);
            if (probe == NULL || (uintptr_t)probe < range->start ||
                (uintptr_t)probe > range->end - MEMCHUNK_TOTAL)
                return fail(state, AROS_APPLE_KERNEL_STARTUP_MEMORY_IMPORT,
                    index);
            Deallocate(header, probe, MEMCHUNK_TOTAL);
            if (header->mh_Free != free_before)
                return fail(state, AROS_APPLE_KERNEL_STARTUP_MEMORY_IMPORT,
                    index);
        }
        ++imported;
    }
    existing_count = memory_list_count_and_free(&SysBase->MemList,
        &total_free);
    if (imported != expected_headers || existing_count != expected_headers)
        return fail(state, AROS_APPLE_KERNEL_STARTUP_MEMORY_IMPORT,
            imported);

    state->memory_plan = plan_address;
    state->conventional_bytes = plan->conventional_bytes;
    state->reclaimed_bytes = plan->release_bytes;
    state->total_free_bytes = total_free;
    state->memory_header_count = existing_count;
    state->conventional_range_count = plan->conventional_count;
    state->release_range_count = plan->release_count;
    state->retained_range_count = plan->retained_count;
    state->phase = AROS_APPLE_KERNEL_PHASE_MEMORY_READY;
    return AROS_APPLE_KERNEL_STARTUP_OK;
}
