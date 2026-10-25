#include <aros/apple/vmconsole.h>
/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Shared kernel diagnostics, retained FDT and VM console output.
 */

#include <stdint.h>

#include <aros/apple/runtime.h>
#include <aros/apple/startup.h>
#include <aros/apple/klog.h>
#include <aros/aarch64/hwcap.h>
#include <aros/kernel.h>

#include "kernel_intern.h"
#include "platform_debug.h"

struct KernelBase;

#ifndef AROS_APPLE_DEBUG_UART_POLL_LIMIT
#define AROS_APPLE_DEBUG_UART_POLL_LIMIT UINT32_C(1000000)
#endif

#define DEBUG_KIND_NONE 0
#define DEBUG_KIND_PL011 3
#define DEBUG_KIND_VIRTIO 4
static struct VMConsole *vm_console;

static uintptr_t debug_uart;
static uint32_t debug_uart_failed;
static uint32_t debug_kind;

void AROSAppleKernelDebugInitialize(uintptr_t base, uint64_t size)
{
    AROSPlatformDebugInitialize(base, size);
    debug_uart = 0;
    debug_uart_failed = 0;
    debug_kind = DEBUG_KIND_NONE;
    if (base != 0 && size >= UINT64_C(0x30) &&
        size <= UINTPTR_MAX - base) {
        debug_uart = base;
    }
}

#define FDT_MAGIC UINT32_C(0xd00dfeed)
#define FDT_BEGIN_NODE UINT32_C(1)
#define FDT_END_NODE UINT32_C(2)
#define FDT_PROP UINT32_C(3)
#define FDT_NOP UINT32_C(4)
#define FDT_END UINT32_C(9)

static uint32_t read_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
        ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

/*
 * Compare up to limit bytes; a match ends at the wanted string's terminator
 * or at the limit, so the same routine checks a whole name and a prefix.
 */
static int text_equal(const uint8_t *text, uint32_t limit, const char *want)
{
    uint32_t index;

    for (index = 0; index < limit; ++index) {
        if (want[index] == 0)
            return text[index] == 0;
        if (text[index] != (uint8_t)want[index])
            return 0;
    }
    return want[index] == 0 || want[index] == ':';
}

/*
 * Look one property up under /chosen. The scan is bounded by the tree's own
 * header and never follows a pointer out of it; a malformed tree reads as
 * "not found".
 */
static int find_property(
    const void *fdt, uint64_t size, const char *node, const char *name,
    const uint8_t **value, uint32_t *length);

int AROSAppleKernelChosenProperty(
    const void *fdt, uint64_t size, const char *name,
    const uint8_t **value, uint32_t *length)
{
    return find_property(fdt, size, "chosen", name, value, length);
}

/* A node name is looked up under the root; NULL names the root itself. */
static int find_property(
    const void *fdt, uint64_t size, const char *node, const char *name,
    const uint8_t **value, uint32_t *length)
{
    const uint8_t *data = fdt;
    uint32_t total;
    uint32_t structure_offset;
    uint32_t strings_offset;
    uint32_t structure_size;
    uint32_t strings_size;
    uint32_t offset;
    uint32_t depth = 0;
    int in_chosen = 0;
    uint32_t steps;

    if (data == NULL || name == NULL || size < 40U || size > UINT32_MAX)
        return 0;
    if (read_be32(data) != FDT_MAGIC)
        return 0;
    total = read_be32(data + 4);
    structure_offset = read_be32(data + 8);
    strings_offset = read_be32(data + 12);
    structure_size = read_be32(data + 36);
    strings_size = read_be32(data + 32);
    if (total > size || structure_offset > total ||
        structure_size > total - structure_offset ||
        strings_offset > total || strings_size > total - strings_offset)
        return 0;

    offset = 0;
    for (steps = 0; steps < UINT32_C(200000); ++steps) {
        uint32_t token;

        if (offset + 4U > structure_size)
            return 0;
        token = read_be32(data + structure_offset + offset);
        offset += 4U;
        if (token == FDT_BEGIN_NODE) {
            const uint8_t *node_name = data + structure_offset + offset;
            uint32_t node_length = 0;

            while (offset + node_length < structure_size &&
                    node_name[node_length] != 0)
                ++node_length;
            if (offset + node_length >= structure_size)
                return 0;
            ++depth;
            if (node == NULL)
                in_chosen = depth == 1U;
            else
                in_chosen = depth == 2U &&
                    text_equal(node_name, node_length + 1U, node);
            offset += (node_length + 1U + 3U) & ~UINT32_C(3);
        } else if (token == FDT_END_NODE) {
            if (depth == 0)
                return 0;
            --depth;
            in_chosen = node == NULL && depth == 1U;
        } else if (token == FDT_PROP) {
            uint32_t property_length;
            uint32_t name_offset;

            if (offset + 8U > structure_size)
                return 0;
            property_length = read_be32(data + structure_offset + offset);
            name_offset = read_be32(data + structure_offset + offset + 4U);
            offset += 8U;
            if (property_length > structure_size - offset)
                return 0;
            if (in_chosen && name_offset < strings_size &&
                text_equal(data + strings_offset + name_offset,
                    strings_size - name_offset, name)) {
                *value = data + structure_offset + offset;
                *length = property_length;
                return 1;
            }
            offset += (property_length + 3U) & ~UINT32_C(3);
        } else if (token == FDT_NOP) {
            continue;
        } else {
            return 0;
        }
    }
    return 0;
}

static const void *retained_fdt;
static uint64_t retained_fdt_size;

/*
 * The CPU nodes of the retained tree: children of /cpus named "cpu@...".
 * Returns their count and, through boot_index, the position of the node
 * whose reg equals boot_affinity (0 when none matches). m1n1 removes the
 * nodes of cores it did not start, so the count is what firmware handed
 * over, not what the chip has.
 */
uint32_t AROSAppleKernelCPUNodes(uint64_t boot_affinity, uint32_t *boot_index)
{
    const uint8_t *data = retained_fdt;
    uint32_t total, structure_offset, strings_offset, structure_size, strings_size;
    uint32_t offset = 0, depth = 0, steps, count = 0;
    int in_cpus = 0, in_cpu = 0;

    if (boot_index != NULL)
        *boot_index = 0;
    if (data == NULL || retained_fdt_size < 40U || retained_fdt_size > UINT32_MAX ||
            read_be32(data) != FDT_MAGIC)
        return 0;
    total = read_be32(data + 4);
    structure_offset = read_be32(data + 8);
    strings_offset = read_be32(data + 12);
    structure_size = read_be32(data + 36);
    strings_size = read_be32(data + 32);
    if (total > retained_fdt_size || structure_offset > total ||
            structure_size > total - structure_offset ||
            strings_offset > total || strings_size > total - strings_offset)
        return 0;
    for (steps = 0; steps < UINT32_C(200000); ++steps) {
        uint32_t token;

        if (offset + 4U > structure_size)
            return count;
        token = read_be32(data + structure_offset + offset);
        offset += 4U;
        if (token == FDT_BEGIN_NODE) {
            const uint8_t *name = data + structure_offset + offset;
            uint32_t length = 0;

            while (offset + length < structure_size && name[length] != 0)
                ++length;
            if (offset + length >= structure_size)
                return count;
            ++depth;
            if (depth == 2U)
                in_cpus = text_equal(name, length + 1U, "cpus");
            in_cpu = depth == 3U && in_cpus && length > 4U &&
                name[0] == 'c' && name[1] == 'p' && name[2] == 'u' && name[3] == '@';
            if (in_cpu)
                ++count;
            offset += (length + 1U + 3U) & ~UINT32_C(3);
        } else if (token == FDT_END_NODE) {
            if (depth == 0)
                return count;
            if (depth == 2U)
                in_cpus = 0;
            in_cpu = 0;
            --depth;
        } else if (token == FDT_PROP) {
            uint32_t property_length, name_offset;

            if (offset + 8U > structure_size)
                return count;
            property_length = read_be32(data + structure_offset + offset);
            name_offset = read_be32(data + structure_offset + offset + 4U);
            offset += 8U;
            if (property_length > structure_size - offset)
                return count;
            if (in_cpu && depth == 3U && boot_index != NULL &&
                    name_offset < strings_size && property_length >= 4U &&
                    text_equal(data + strings_offset + name_offset,
                        strings_size - name_offset, "reg")) {
                const uint8_t *value = data + structure_offset + offset;
                uint64_t reg = read_be32(value + property_length - 4U);

                if (property_length >= 8U)
                    reg |= (uint64_t)read_be32(value + property_length - 8U) << 32;
                if ((reg & UINT64_C(0xff00ffffff)) == boot_affinity)
                    *boot_index = count - 1U;
            }
            offset += (property_length + 3U) & ~UINT32_C(3);
        } else if (token == FDT_END) {
            return count;
        } else if (token != FDT_NOP) {
            return count;
        }
    }
    return count;
}

void AROSAppleKernelRetainDeviceTree(const void *fdt, uint64_t size)
{
    retained_fdt = fdt;
    retained_fdt_size = size;
}

int AROSAppleKernelRetainedDeviceTree(const void **fdt, uint64_t *size)
{
    if (retained_fdt == NULL || retained_fdt_size == 0)
        return 0;
    *fdt = retained_fdt;
    *size = retained_fdt_size;
    return 1;
}

/*
 * A decimal text property under /chosen of the retained tree, for the few
 * run-shaping knobs the guest composer sets. Absent or malformed reads as
 * zero, which every caller treats as "off".
 */
uint64_t AROSAppleKernelChosenDecimal(const char *name)
{
    const uint8_t *value;
    uint32_t length;
    uint64_t result = 0;
    uint32_t index;

    if (retained_fdt == NULL ||
        !AROSAppleKernelChosenProperty(retained_fdt, retained_fdt_size,
            name, &value, &length))
        return 0;
    if (length == 0 || length > 21U || value[length - 1U] != 0)
        return 0;
    for (index = 0; index + 1U < length; ++index) {
        if (value[index] < '0' || value[index] > '9')
            return 0;
        result = result * 10U + (uint64_t)(value[index] - '0');
    }
    return result;
}

/*
 * CPU and board identity for KrnGetSystemAttr. A task runs at EL0 and cannot
 * read MIDR_EL1, CurrentEL or CNTFRQ_EL0 itself, so the kernel records them
 * once while it still runs at EL1, together with bounded copies of the root
 * node's model and compatible strings.
 */
static uint64_t identity_midr;
static uint64_t identity_current_el;
static uint64_t identity_counter_frequency;
static uint64_t identity_hwcap;
static uint64_t identity_hwcap2;
static char identity_model[AROS_APPLE_IDENTITY_TEXT_BYTES];
static char identity_compatible[AROS_APPLE_IDENTITY_TEXT_BYTES];
static int identity_recorded;

static void copy_root_text(const char *name, char *out)
{
    const uint8_t *value;
    uint32_t length;
    uint32_t index;

    out[0] = 0;
    if (retained_fdt == NULL ||
        !find_property(retained_fdt, retained_fdt_size, NULL, name,
            &value, &length))
        return;
    for (index = 0; index < length && index + 1U < AROS_APPLE_IDENTITY_TEXT_BYTES;
            ++index) {
        /* A compatible list is NUL-separated: show it as one line. */
        out[index] = value[index] == 0 && index + 1U < length ?
            ' ' : (char)value[index];
    }
    out[index] = 0;
}

static struct AROSAppleBootSeed boot_seed;
static struct AROSAppleSystemTimeInfo system_time_info;

void AROSAppleKernelPublishSystemTime(const struct AROSAppleSystemTimeInfo *info)
{
    const uint8_t *source = (const uint8_t *)info;
    volatile uint8_t *target = (volatile uint8_t *)&system_time_info;
    uint32_t index;

    for (index = 0; index < sizeof(system_time_info); ++index)
        target[index] = source[index];
}

/* Copy a /chosen seed property into the kernel's one-shot buffer, then wipe
 * it in the retained tree so nothing that reads the tree later sees it, as
 * Linux does with the same properties. */
static void take_chosen_seed(const char *name, uint32_t limit)
{
    const uint8_t *value;
    uint32_t length;
    uint32_t index;

    if (retained_fdt == NULL ||
        !AROSAppleKernelChosenProperty(retained_fdt, retained_fdt_size,
            name, &value, &length))
        return;
    for (index = 0; index < length; ++index) {
        if (index < limit &&
            boot_seed.length < AROS_APPLE_BOOT_SEED_BYTES)
            boot_seed.bytes[boot_seed.length++] = value[index];
        ((volatile uint8_t *)value)[index] = 0;
    }
}

/* The boot CPU's features stand for all of them: every core of one Apple SoC
 * implements the same ISA, and a VM guest is given one CPU model. */
static void record_hwcap(void)
{
    uint64_t isar0, isar1, pfr0, zfr0 = 0;

    __asm__ volatile("mrs %0, id_aa64isar0_el1" : "=r"(isar0));
    __asm__ volatile("mrs %0, id_aa64isar1_el1" : "=r"(isar1));
    __asm__ volatile("mrs %0, id_aa64pfr0_el1" : "=r"(pfr0));
    if (AROSAArch64IDField(pfr0, 32) >= 1)
        __asm__ volatile("mrs %0, s3_0_c0_c4_4" : "=r"(zfr0)); /* ZFR0 */
    AROSAArch64DecodeHWCap(isar0, isar1, pfr0, zfr0,
        &identity_hwcap, &identity_hwcap2);
}

void AROSAppleKernelRecordIdentity(void)
{
    uint64_t value;

    __asm__ volatile("mrs %0, midr_el1" : "=r"(value));
    identity_midr = value;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(value));
    identity_current_el = (value >> 2) & 3U;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(value));
    identity_counter_frequency = value & UINT32_MAX;
    record_hwcap();
    copy_root_text("model", identity_model);
    copy_root_text("compatible", identity_compatible);
    boot_seed.length = 0;
    take_chosen_seed("rng-seed", AROS_APPLE_BOOT_SEED_BYTES);
    /* The KASLR seed is wiped too; AROS does not randomise its layout. */
    take_chosen_seed("kaslr-seed", 0);
    identity_recorded = 1;
}

int AROSAppleKernelIdentity(uint32_t id, intptr_t *value)
{
    if (!identity_recorded)
        return 0;
    switch (id) {
    case AROS_APPLE_KATTR_CPU_MIDR:
        *value = (intptr_t)identity_midr;
        return 1;
    case AROS_APPLE_KATTR_CURRENT_EL:
        *value = (intptr_t)identity_current_el;
        return 1;
    case AROS_APPLE_KATTR_COUNTER_FREQUENCY:
        *value = (intptr_t)identity_counter_frequency;
        return 1;
    case KATTR_HWCap:
        *value = (intptr_t)identity_hwcap;
        return 1;
    case KATTR_HWCap2:
        *value = (intptr_t)identity_hwcap2;
        return 1;
    case AROS_APPLE_KATTR_BOARD_MODEL:
        *value = (intptr_t)identity_model;
        return identity_model[0] != 0;
    case AROS_APPLE_KATTR_BOARD_COMPATIBLE:
        *value = (intptr_t)identity_compatible;
        return identity_compatible[0] != 0;
    case AROS_APPLE_KATTR_BOOT_SEED:
        *value = (intptr_t)&boot_seed;
        return boot_seed.length != 0;
    case AROS_APPLE_KATTR_SYSTEM_TIME:
        *value = (intptr_t)&system_time_info;
        return system_time_info.version == AROS_APPLE_SYSTEM_TIME_VERSION;
    default:
        return AROSPlatformIdentity(id, value);
    }
}

/* PL011 is selected only for the FDT-bound virt console. */
static void pl011_put(int chr)
{
    volatile uint32_t *data = (volatile uint32_t *)debug_uart;
    volatile uint32_t *flags = (volatile uint32_t *)(debug_uart + 0x18);
    for (uint32_t count=0; count<AROS_APPLE_DEBUG_UART_POLL_LIMIT; ++count) {
        if ((*flags & (1U << 5)) == 0) { *data = (uint8_t)chr; return; }
    }
    debug_uart_failed = 1;
}

void AROSAppleKernelDebugSelectVirtConsole(const void *fdt, uint64_t size)
{
    const uint8_t *value;
    uint32_t length;
    uint32_t at;

    if (!find_property(fdt, size, NULL, "compatible", &value, &length))
        return;
    /* A string list: match a whole entry, not a substring of one. */
    for (at = 0; at < length;) {
        uint32_t entry = 0;

        while (at + entry < length && value[at + entry] != 0)
            ++entry;
        if (at + entry >= length)
            return;
        if (text_equal(value + at, entry + 1U, "linux,dummy-virt")) {
            if (!debug_uart) return;
            debug_uart_failed = 0;
            debug_kind = DEBUG_KIND_PL011;
            return;
        }
        if (text_equal(value + at, entry + 1U, "aros,virtualization")) {
            const uint8_t *address; uint32_t bytes;
            if (!find_property(fdt,size,NULL,"aros,vm-console-state",&address,&bytes) || bytes!=8) return;
            uint64_t state=((uint64_t)read_be32(address)<<32)|read_be32(address+4);
            if (!state || (state&4095)) return;
            vm_console=(struct VMConsole *)(uintptr_t)state;
            if (vm_console->magic!=VM_CONSOLE_MAGIC || vm_console->region_start!=debug_uart) return;
            debug_kind=DEBUG_KIND_VIRTIO;debug_uart_failed=0;return;
        }
        at += entry + 1U;
    }
}

void AROSAppleKernelDebugSelectEvidenceChannel(const void *fdt, uint64_t size)
{
    if (AROSPlatformDebugSelectEvidenceChannel(fdt, size))
        debug_kind = DEBUG_KIND_NONE;
}

static int put_raw(int chr)
{
    if (debug_kind == DEBUG_KIND_NONE)
        return AROSPlatformDebugPutRaw(chr);
    if (debug_uart == 0 || debug_uart_failed != 0)
        return 0;
    if (debug_kind == DEBUG_KIND_VIRTIO) {
        char c=(char)chr;return vm_console_write(vm_console,&c,1);
    }
    if (debug_kind == DEBUG_KIND_PL011) {
        pl011_put(chr);
        return 1;
    }
    return 0;
}

void AROSAppleKernelDebugTouchSink(void)
{
    AROSPlatformDebugTouchSink();
}

/* The kernel log ring (aros/apple/klog.h). */
#define KLOG_CAPACITY (512U * 1024U)
static volatile char klog_ring[KLOG_CAPACITY];
static volatile uint64_t klog_head;

/* A plain increment, not an atomic one: this runs from the first stage-two
 * output on, before the MMU makes memory cacheable, where exclusive loads
 * may fault. Two cores writing at once may lose a character. */
static void klog_put(int chr)
{
    uint64_t at = klog_head;

    klog_head = at + 1;
    klog_ring[at % KLOG_CAPACITY] = (char)chr;
}

intptr_t AROSAppleKernelLogRecord(void)
{
    static struct AROSAppleKernelLog record;

    record.version = AROS_APPLE_KLOG_VERSION;
    record.size = sizeof(record);
    record.ring = klog_ring;
    record.capacity = KLOG_CAPACITY;
    record.head = &klog_head;
    return (intptr_t)&record;
}

int AROSAppleKernelDebugPutDirect(int chr)
{
    klog_put(chr);
    AROSAppleKernelScreenLogPut(chr);
    if (chr == '\n' && !put_raw('\r'))
        return 0;
    return put_raw(chr);
}

int krnPutC(int chr, struct KernelBase *KernelBase)
{
    (void)KernelBase;
    if (AROSAppleRuntimeLocal()->active != 0)
        return AROSAppleRuntimeDebugPutChar(chr);
    return AROSAppleKernelDebugPutDirect(chr);
}
