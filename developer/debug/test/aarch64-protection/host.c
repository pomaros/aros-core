/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#include "protection.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint64_t tables[5][2048];
static unsigned allocated, pagebits, cleans, invalidations, code_syncs;
static uint64_t *resolve(void *ctx, uint64_t pa)
{
    (void)ctx;
    unsigned i = (unsigned)(pa >> pagebits);
    return i && i <= 5 ? tables[i - 1] : NULL;
}
static uint64_t allocate(void *ctx, unsigned bits)
{
    (void)ctx;
    assert(bits == pagebits);
    return allocated < 5 ? (uint64_t)++allocated << bits : 0;
}
static void clean(void *ctx, const void *p, size_t size)
{
    (void)ctx; (void)p;
    assert(size == 8 || size == (1U << pagebits));
    ++cleans;
}
static void invalidate(void *ctx) { (void)ctx; ++invalidations; }
static int deny_split(void *ctx, uint64_t desc)
{
    (void)ctx; (void)desc;
    return 0;
}
static void sync_code(void *ctx, uint64_t va, size_t size)
{
    (void)ctx; (void)va;
    assert(size == (1U << pagebits));
    ++code_syncs;
}
static const struct AArch64ProtectionOps ops = {
    NULL, resolve, allocate, clean, invalidate, sync_code, NULL
};

static void exercise(unsigned bits, unsigned va_bits)
{
    uint64_t tcr = 64 - va_bits;
    uint64_t root = UINT64_C(1) << bits;
    uint64_t address = UINT64_C(3) << bits;
    uint64_t *leaf;
    unsigned start, stride = bits - 3;
    unsigned level, slot, writes;
    pagebits = bits;
    if (bits == 14) tcr |= UINT64_C(2) << 14;
    memset(tables, 0, sizeof(tables));
    allocated = 1; cleans = invalidations = code_syncs = 0;
    start = 3 - (va_bits - bits - 1) / stride;
    if (start == 0) {
        tables[0][0] = allocate(NULL, bits) | 3;
        tables[1][0] = UINT64_C(0x400) | 1; /* identity L1 RAM block */
    } else {
        tables[0][0] = UINT64_C(0x400) | 1;
    }
    {
        struct AArch64ProtectionOps restricted = ops;
        restricted.can_split = deny_split;
        assert(!AArch64ProtectPage(&restricted, root, tcr, address, 1));
        assert(invalidations == 0 && cleans == 0);
        unsigned saved = allocated;
        allocated = 5;
        assert(!AArch64ProtectPage(&ops, root, tcr, address, 1));
        assert(invalidations == 0 && cleans == 0);
        allocated = saved;
    }
    assert(AArch64ProtectPage(&ops, root, tcr, address, 1 | 2 | 8));
    leaf = tables[0];
    for (level = start; level < 3; ++level) {
        slot = (unsigned)((address >> (bits + stride * (3 - level))) &
            ((1U << stride) - 1));
        assert((leaf[slot] & 3) == 3);
        leaf = resolve(NULL, leaf[slot] & ~UINT64_C(3));
    }
    assert((leaf[3] & UINT64_C(0x0000fffffffff000)) == address);
    assert(leaf[3] & (UINT64_C(1) << 6));
    assert(!(leaf[3] & (UINT64_C(1) << 7)));
    assert((leaf[3] & (UINT64_C(3) << 53)) == (UINT64_C(3) << 53));
    assert((leaf[2] & UINT64_C(0x0000fffffffff000)) == address - (1U << bits));
    assert(AArch64ProtectPage(&ops, root, tcr, address, 0));
    assert(!(leaf[3] & 1));
    assert(leaf[3] & (UINT64_C(1) << 58));
    assert(AArch64ProtectPage(&ops, root, tcr, address, 1 | 4));
    assert((leaf[3] & 3) == 3 && !(leaf[3] & (UINT64_C(1) << 58)));
    assert(leaf[3] & (UINT64_C(1) << 7));
    assert(!(leaf[3] & (UINT64_C(1) << 53)));
    assert(leaf[3] & (UINT64_C(1) << 54));
    assert(code_syncs == 1);
    assert(AArch64ProtectPage(&ops, root, tcr, address, 1 | 4 | 8));
    assert(leaf[3] & (UINT64_C(1) << 6));
    assert(leaf[3] & (UINT64_C(1) << 53));
    assert(!(leaf[3] & (UINT64_C(1) << 54)));
    assert(AArch64ProtectPage(&ops, root, tcr, address, 1 | 4 | 8 | 16));
    assert(!(leaf[3] & (UINT64_C(3) << 53)));
    assert(leaf[3] & (UINT64_C(1) << 7));
    writes = invalidations;
    assert(!AArch64ProtectPage(&ops, root, tcr, address, 1 | 2 | 4));
    assert(!AArch64ProtectPage(&ops, root, tcr, address, 1 | 16));
    assert(!AArch64ProtectPage(&ops, root, tcr, address + 1, 1));
    assert(!AArch64ProtectPage(&ops, root, tcr, UINT64_C(1) << va_bits, 1));
    assert(!AArch64ProtectPage(&ops, root, tcr | (UINT64_C(1) << 39), address, 1));
    assert(!AArch64ProtectPage(&ops, root, tcr | (UINT64_C(1) << 59), address, 1));
    assert(!AArch64ProtectPage(&ops, root, tcr | (UINT64_C(6) << 32), address, 1));
    assert(!AArch64ProtectPage(&ops, root,
        (tcr & ~(UINT64_C(3) << 14)) | (UINT64_C(1) << 14), address, 1));
    tables[0][0] |= UINT64_C(1) << 61;
    assert(!AArch64ProtectPage(&ops, root, tcr, address, 1));
    tables[0][0] &= ~(UINT64_C(1) << 61);
    leaf[4] = 0;
    assert(!AArch64ProtectPage(&ops, root, tcr, address + (1U << bits), 1));
    leaf[3] |= UINT64_C(1) << 52;
    assert(!AArch64ProtectPage(&ops, root, tcr, address, 1));
    assert(writes == invalidations);
    assert(cleans >= invalidations);
}

int main(void)
{
    extern void test_guarded_stack(void);
    extern void test_guard_fault(void);
    test_guarded_stack();
    test_guard_fault();
    exercise(12, 48);
    exercise(12, 39);
    exercise(12, 32);
    exercise(14, 48);
    exercise(14, 39);
    exercise(14, 32);
    puts("AARCH64-PROTECTION HOST PASS granules=4K,16K roots=L0,L1,L2 guard-restore wx-rejected bounds");
    return 0;
}
