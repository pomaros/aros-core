/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Generic ECAM binding: PCI child address flags are not scalar FDT cells. */
#include "pci_ecam.h"
#include <libfdt.h>
#define ADDRESS_LIMIT (UINT64_C(1) << 48)
static uint32_t cell(const void *p)
{
    const uint8_t *b = p;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
        ((uint32_t)b[2] << 8) | b[3];
}
static uint64_t pair(const uint8_t *p)
{
    return ((uint64_t)cell(p) << 32) | cell(p + 4);
}
static int count_is(const struct AROSEarlyFDT *tree, int node,
    const char *name, uint32_t expected)
{
    int bytes;
    const void *p = AROSEarlyFDTProperty(tree, node, name, &bytes);
    return p && bytes == 4 && cell(p) == expected;
}
static int window_valid(uint64_t base, uint64_t bytes)
{
    return base && bytes && base < ADDRESS_LIMIT && bytes <= ADDRESS_LIMIT - base &&
        !(base & 4095) && !(bytes & 4095);
}
static int overlaps(uint64_t a, uint64_t an, uint64_t b, uint64_t bn)
{
    return a < b + bn && b < a + an;
}
int AROSFDTDiscoverPCI(const struct AROSEarlyFDT *tree, struct AROSFDTPCI *out)
{
    struct AROSFDTPCI result;
    const uint8_t *ranges, *buses;
    int node = -1, depth = -1, bridge = -1, bytes;
    while ((node = AROSEarlyFDTNext(tree, node, &depth)) >= 0 && depth >= 0) {
        if (!AROSEarlyFDTEnabled(tree, node) ||
            !AROSEarlyFDTCompatible(tree, node, "pci-host-ecam-generic")) continue;
        if (bridge >= 0 || depth != 1) return 0;
        bridge = node;
    }
    if (bridge < 0) return -1;
    if (!count_is(tree, 0, "#address-cells", 2) ||
        !count_is(tree, 0, "#size-cells", 2) ||
        !count_is(tree, bridge, "#address-cells", 3) ||
        !count_is(tree, bridge, "#size-cells", 2)) return 0;
    const char *type = AROSEarlyFDTProperty(tree, bridge, "device_type", &bytes);
    if (!type || bytes != 4 || type[0] != 'p' || type[1] != 'c' ||
        type[2] != 'i' || type[3]) return 0;
    buses = AROSEarlyFDTProperty(tree, bridge, "bus-range", &bytes);
    if (!buses || bytes != 8) return 0;
    result.first_bus = cell(buses); result.last_bus = cell(buses + 4);
    if (result.first_bus > result.last_bus || result.last_bus > 255) return 0;
    const void *reg = AROSEarlyFDTProperty(tree, bridge, "reg", &bytes);
    if (!reg || bytes != 16 ||
        !AROSEarlyFDTReg(tree, bridge, 0, &result.ecam, &result.ecam_bytes) ||
        !window_valid(result.ecam, result.ecam_bytes) || (result.ecam & 0xfffff) ||
        result.ecam_bytes < ((uint64_t)(result.last_bus-result.first_bus+1) << 20)) return 0;
    ranges = AROSEarlyFDTProperty(tree, bridge, "ranges", &bytes);
    if (!ranges || bytes <= 0 || bytes % 28 || bytes > 28 * AROS_FDT_PCI_MAX_WINDOWS) return 0;
    result.window_count = 0; result.node = bridge;
    for (int at = 0; at < bytes; at += 28) {
        uint32_t flags = cell(ranges + at), space = (flags >> 24) & 3;
        uint64_t bus = pair(ranges + at + 4), cpu = pair(ranges + at + 12);
        uint64_t span = pair(ranges + at + 20);
        if ((flags & ~UINT32_C(0x43000000)) || !span || bus > UINT64_MAX - span) return 0;
        if (space == 1) continue;
        if ((space != 2 && space != 3) || !window_valid(cpu, span) ||
            (bus & 4095) || (space == 2 && bus + span > UINT64_C(0x100000000)) ||
            overlaps(cpu, span, result.ecam, result.ecam_bytes)) return 0;
        for (uint32_t i = 0; i < result.window_count; ++i) {
            const struct AROSFDTPCIWindow *w = &result.windows[i];
            if (overlaps(cpu, span, w->cpu, w->bytes) || overlaps(bus, span, w->bus, w->bytes)) return 0;
        }
        struct AROSFDTPCIWindow *w = &result.windows[result.window_count++];
        w->bus = bus; w->cpu = cpu; w->bytes = span; w->flags = flags;
    }
    if (!result.window_count) return 0;
    out->ecam = result.ecam; out->ecam_bytes = result.ecam_bytes;
    out->first_bus = result.first_bus; out->last_bus = result.last_bus;
    out->node = result.node; out->window_count = result.window_count;
    for (uint32_t i = 0; i < result.window_count; ++i) out->windows[i] = result.windows[i];
    return 1;
}

int AROSFDTDiscoverMSIFrame(const struct AROSEarlyFDT *tree, int pci_node,
    struct AROSFDTMSIFrame *out)
{
    struct AROSFDTMSIFrame frame = {0};
    int bytes, node = -1, depth = -1, supplier = -1;
    const void *p = AROSEarlyFDTProperty(tree, pci_node, "msi-parent", &bytes);
    if (!p) return bytes == -FDT_ERR_NOTFOUND ? -1 : 0;
    if (bytes < 4 || bytes % 4 || !cell(p) || cell(p) == UINT32_MAX) return 0;
    uint32_t phandle = cell(p);
    while ((node = AROSEarlyFDTNext(tree, node, &depth)) >= 0 && depth >= 0) {
        if (fdt_get_phandle(tree->blob, node) != phandle) continue;
        if (supplier >= 0) return 0;
        supplier = node;
    }
    if (supplier < 0 || !AROSEarlyFDTEnabled(tree, supplier)) return 0;
    if (!AROSEarlyFDTCompatible(tree, supplier, "arm,gic-v2m-frame")) return -1;
    if (bytes != 4) return 0;
    p = AROSEarlyFDTProperty(tree, supplier, "#msi-cells", &bytes);
    if (p && (bytes != 4 || cell(p))) return 0;
    p = AROSEarlyFDTProperty(tree, supplier, "msi-controller", &bytes);
    if (!p || bytes) return 0;
    frame.controller = fdt_parent_offset(tree->blob, supplier);
    if (frame.controller < 0 || !AROSEarlyFDTEnabled(tree, frame.controller) ||
        (!AROSEarlyFDTCompatible(tree, frame.controller, "arm,gic-v3") &&
         !AROSEarlyFDTCompatible(tree, frame.controller, "arm,gic-400") &&
         !AROSEarlyFDTCompatible(tree, frame.controller, "arm,cortex-a15-gic"))) return 0;
    if (!AROSEarlyFDTReg(tree, supplier, 0, &frame.base, &frame.bytes) ||
        !window_valid(frame.base, frame.bytes) || frame.bytes != 4096) return 0;
    uint64_t extra_base, extra_bytes;
    if (AROSEarlyFDTRawReg(tree, supplier, 1, &extra_base, &extra_bytes)) return 0;
    const void *base = AROSEarlyFDTProperty(tree, supplier, "arm,msi-base-spi", &bytes);
    if (base && bytes != 4) return 0;
    const void *count = AROSEarlyFDTProperty(tree, supplier, "arm,msi-num-spis", &bytes);
    if (count && bytes != 4) return 0;
    if (!!base != !!count) return 0;
    if (base) {
        frame.spi_base = cell(base); frame.spi_count = cell(count);
        if (frame.spi_base < 32 || frame.spi_base >= 1020 || !frame.spi_count ||
            frame.spi_count > 1020 - frame.spi_base) return 0;
    }
    frame.node = supplier;
    *out = frame;
    return 1;
}
