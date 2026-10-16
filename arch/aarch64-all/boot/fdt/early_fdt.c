/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Platform-neutral early reader; DTSpec 0.4, libfdt 1.7.2 (BSD terms).
 */
#include "early_fdt.h"
#include <libfdt.h>

static uint32_t cell(const void *v)
{
    const uint8_t *p = v;
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static uint64_t cells(const uint8_t *p, int n)
{
    return n == 2 ? (uint64_t)cell(p) << 32 | cell(p + 4) : n == 1 ? cell(p) : 0;
}
static int number(const void *b, int node, const char *name, int fallback)
{
    int n;
    const uint8_t *p = fdt_getprop(b, node, name, &n);
    if (!p)
        return n == -FDT_ERR_NOTFOUND ? fallback : -1;
    return n == 4 && cell(p) <= INT32_MAX ? (int)cell(p) : -1;
}
int AROSEarlyFDTOpen(struct AROSEarlyFDT *tree, const void *blob, size_t bytes)
{
    if (!tree)
        return 0;
    tree->blob = NULL;
    tree->bytes = tree->nodes = 0;
    if (!blob || bytes < 40 || bytes > AROS_FDT_MAX_BYTES || ((uintptr_t)blob & 7) ||
        /* libfdt's token/reservation walkers use aligned scalar loads.
         * Validate block alignment before asking them to inspect the blob. */
        (fdt_off_dt_struct(blob) & 3) || (fdt_off_mem_rsvmap(blob) & 7) ||
        fdt_check_full(blob, bytes))
        return 0;
    int depth = -1, node = -1;
    uint32_t count = 0;
    while ((node = fdt_next_node(blob, node, &depth)) >= 0 && depth >= 0)
    {
        if (depth >= AROS_FDT_MAX_DEPTH || ++count > AROS_FDT_MAX_NODES)
            return 0;
        /* Validate string-list semantics used for matching, not just token bounds. */
        int n;
        const char *p = fdt_getprop(blob, node, "compatible", &n);
        if (p && (n < 2 || p[n - 1] || fdt_stringlist_count(blob, node, "compatible") < 1))
            return 0;
    }
    if (!count || (node < 0 && node != -FDT_ERR_NOTFOUND))
        return 0;
    tree->blob = blob;
    tree->bytes = fdt_totalsize(blob);
    tree->nodes = count;
    return 1;
}
int AROSEarlyFDTNext(const struct AROSEarlyFDT *t, int node, int *depth)
{
    return t && t->blob ? fdt_next_node(t->blob, node, depth) : -FDT_ERR_BADSTATE;
}
int AROSEarlyFDTPath(const struct AROSEarlyFDT *t, int node, char *p, size_t n)
{
    return t && t->blob && p && n <= INT32_MAX && !fdt_get_path(t->blob, node, p, (int)n);
}
int AROSEarlyFDTFindPath(const struct AROSEarlyFDT *t, const char *p)
{
    return t && t->blob && p ? fdt_path_offset(t->blob, p) : -FDT_ERR_BADSTATE;
}
const void *AROSEarlyFDTProperty(const struct AROSEarlyFDT *t, int node, const char *key, int *n)
{
    if (n)
        *n = -FDT_ERR_BADSTATE;
    return t && t->blob && key ? fdt_getprop(t->blob, node, key, n) : NULL;
}
int AROSEarlyFDTCompatible(const struct AROSEarlyFDT *t, int node, const char *name)
{
    return t && t->blob && name && !fdt_node_check_compatible(t->blob, node, name);
}
int AROSEarlyFDTEnabled(const struct AROSEarlyFDT *t, int node)
{
    if (!t || !t->blob || node < 0)
        return 0;
    /* A disabled supplier bus makes its children inaccessible too. */
    for (int d = 0; node >= 0 && d < AROS_FDT_MAX_DEPTH; ++d)
    {
        int n;
        const char *p = fdt_getprop(t->blob, node, "status", &n);
        if (p && !((n == 5 && !memcmp(p, "okay", 5)) || (n == 3 && !memcmp(p, "ok", 3))))
            return 0;
        if (!p && n != -FDT_ERR_NOTFOUND)
            return 0;
        if (node == 0)
            return 1;
        node = fdt_parent_offset(t->blob, node);
    }
    return 0;
}
int AROSEarlyFDTRawReg(const struct AROSEarlyFDT *t, int node, unsigned index, uint64_t *a,
                       uint64_t *s)
{
    if (!t || !t->blob || !a || !s)
        return 0;
    int parent = fdt_parent_offset(t->blob, node), n;
    if (parent < 0)
        return 0;
    int ac = number(t->blob, parent, "#address-cells", 2),
        sc = number(t->blob, parent, "#size-cells", 1);
    if (ac < 1 || ac > 2 || sc < 0 || sc > 2)
        return 0;
    const uint8_t *p = fdt_getprop(t->blob, node, "reg", &n);
    unsigned stride = (unsigned)(ac + sc) * 4;
    if (!p || n <= 0 || (unsigned)n % stride || index >= (unsigned)n / stride)
        return 0;
    p += index * stride;
    *a = cells(p, ac);
    *s = cells(p + ac * 4, sc);
    return *s <= UINT64_MAX - *a;
}
int AROSEarlyFDTReg(const struct AROSEarlyFDT *t, int node, unsigned index, uint64_t *a,
                    uint64_t *s)
{
    uint64_t address, size;
    if (!a || !s || !AROSEarlyFDTRawReg(t, node, index, &address, &size) || !size)
        return 0;
    int bus = fdt_parent_offset(t->blob, node);
    for (int depth = 0; bus > 0 && depth < AROS_FDT_MAX_DEPTH; ++depth)
    {
        int parent = fdt_parent_offset(t->blob, bus), n;
        if (parent < 0)
            return 0;
        int ac = number(t->blob, bus, "#address-cells", 2),
            sc = number(t->blob, bus, "#size-cells", 1);
        int pc = number(t->blob, parent, "#address-cells", 2);
        if (ac < 1 || ac > 2 || pc < 1 || pc > 2 || sc < 1 || sc > 2)
            return 0;
        const uint8_t *p = fdt_getprop(t->blob, bus, "ranges", &n);
        if (!p)
            return 0; /* Absent means untranslated, empty means identity. */
        if (n)
        {
            unsigned stride = (unsigned)(ac + pc + sc) * 4, matches = 0;
            uint64_t translated = 0;
            if (n < 0 || (unsigned)n % stride)
                return 0;
            for (unsigned i = 0; i < (unsigned)n; i += stride)
            {
                uint64_t child = cells(p + i, ac), host = cells(p + i + ac * 4, pc),
                         span = cells(p + i + (ac + pc) * 4, sc);
                if (span > UINT64_MAX - child || span > UINT64_MAX - host)
                    return 0;
                if (address >= child && address - child < span && size <= span - (address - child))
                {
                    translated = host + (address - child);
                    ++matches;
                }
            }
            if (matches != 1)
                return 0;
            address = translated;
        }
        else if (pc == 1 && (address > UINT32_MAX || size - 1 > UINT32_MAX - address))
            return 0;
        bus = parent;
    }
    if (bus != 0)
        return 0;
    *a = address;
    *s = size;
    return 1;
}
int AROSEarlyFDTStdout(const struct AROSEarlyFDT *t)
{
    if (!t || !t->blob)
        return -FDT_ERR_BADSTATE;
    int chosen = fdt_path_offset(t->blob, "/chosen"), n;
    if (chosen < 0)
        return chosen;
    const char *p = fdt_getprop(t->blob, chosen, "stdout-path", &n);
    if (!p)
        return n;
    if (n < 2 || n > AROS_FDT_MAX_PATH || p[n - 1] || strnlen(p, (size_t)n) != (size_t)n - 1)
        return -FDT_ERR_BADVALUE;
    const char *colon = memchr(p, ':', (size_t)n);
    int length = colon ? (int)(colon - p) : n - 1;
    int node = fdt_path_offset_namelen(t->blob, p, length);
    return node >= 0 && !AROSEarlyFDTEnabled(t, node) ? -FDT_ERR_BADVALUE : node;
}
static int irq_cells(const void *b, int controller)
{
    int n;
    if (controller < 0 || !fdt_getprop(b, controller, "interrupt-controller", &n) || n)
        return -1;
    int count = number(b, controller, "#interrupt-cells", -1);
    return count > 0 && count <= AROS_FDT_MAX_IRQ_CELLS ? count : -1;
}
int AROSEarlyFDTInterrupt(const struct AROSEarlyFDT *t, int node, unsigned index,
                          struct AROSFDTIRQ *out)
{
    if (!t || !t->blob || !out || !AROSEarlyFDTEnabled(t, node))
        return 0;
    int n, controller = -1, count = -1;
    const uint8_t *p = fdt_getprop(t->blob, node, "interrupts-extended", &n);
    if (p)
    {
        if (n <= 0 || n % 4)
            return 0;
        unsigned at = 0, entry = 0;
        const uint8_t *selected = NULL;
        while (at < (unsigned)n)
        {
            int c = fdt_node_offset_by_phandle(t->blob, cell(p + at));
            at += 4;
            int nc = irq_cells(t->blob, c);
            if (nc < 0 || (unsigned)n - at < (unsigned)nc * 4)
                return 0;
            if (entry++ == index)
            {
                controller = c;
                count = nc;
                selected = p + at;
            }
            at += (unsigned)nc * 4;
        }
        if (!selected)
            return 0;
        p = selected;
    }
    else
    {
        if (n != -FDT_ERR_NOTFOUND)
            return 0;
        for (int current = node, d = 0; current >= 0 && d < AROS_FDT_MAX_DEPTH; ++d)
        {
            const uint8_t *parent = fdt_getprop(t->blob, current, "interrupt-parent", &n);
            if (parent)
            {
                if (n != 4)
                    return 0;
                controller = fdt_node_offset_by_phandle(t->blob, cell(parent));
                break;
            }
            if (current != node && irq_cells(t->blob, current) > 0)
            {
                controller = current;
                break;
            }
            current = fdt_parent_offset(t->blob, current);
        }
        count = irq_cells(t->blob, controller);
        p = fdt_getprop(t->blob, node, "interrupts", &n);
        if (count < 0 || !p || n <= 0 || (unsigned)n % ((unsigned)count * 4) ||
            index >= (unsigned)n / ((unsigned)count * 4))
            return 0;
        p += index * (unsigned)count * 4;
    }
    if (!AROSEarlyFDTEnabled(t, controller))
        return 0;
    /* Interrupt nexuses require their own interrupt-map binding. */
    if (fdt_getprop(t->blob, controller, "interrupt-map", &n))
        return 0;
    out->controller = controller;
    out->count = (uint32_t)count;
    for (int i = 0; i < count; ++i)
        out->cells[i] = cell(p + i * 4);
    return 1;
}

int AROSEarlyFDTInterruptNamed(const struct AROSEarlyFDT *tree, int node,
    const char *name, struct AROSFDTIRQ *irq)
{
    int bytes, index = 0, selected = -1;
    if (!tree || !tree->blob || !name || !irq) return 0;
    const char *entry = AROSEarlyFDTProperty(tree, node, "interrupt-names", &bytes);
    if (!entry || bytes < 1) return 0;
    /* Scan once: repeated string-list lookups would make long lists quadratic. */
    while (bytes > 0) {
        size_t length = strnlen(entry, (size_t)bytes);
        if (length == 0 || length >= (size_t)bytes) return 0;
        if (strcmp(entry, name) == 0) {
            if (selected >= 0) return 0;
            selected = index;
        }
        entry += length + 1;
        bytes -= (int)length + 1;
        ++index;
    }
    return selected >= 0 && AROSEarlyFDTInterrupt(tree, node, (unsigned)selected, irq);
}
