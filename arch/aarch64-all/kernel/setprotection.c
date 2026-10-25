/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 *
 * Stage-1 descriptor protection engine. Architecture reference: Arm DDI 0487,
 * VMSAv8-64 translation table descriptors and break-before-make requirements.
 * This engine is opt-in: hosted targets must retain their host mprotect path.
 */
#include "protection.h"

#define VALID UINT64_C(1)
#define TYPE UINT64_C(3)
#define TABLE UINT64_C(3)
#define AP_USER (UINT64_C(1) << 6)
#define AP_RO (UINT64_C(1) << 7)
#define PXN (UINT64_C(1) << 53)
#define UXN (UINT64_C(1) << 54)
#define CONTIGUOUS (UINT64_C(1) << 52)
#define GUARDED (UINT64_C(1) << 58)
#define TABLE_RESTRICTIONS (UINT64_C(0x1f) << 59)

static void replace(const struct AArch64ProtectionOps *ops,
    uint64_t *slot, uint64_t value)
{
    if (*slot == value)
        return;
    if (*slot & VALID) {
        *slot = 0;
        ops->clean(ops->context, slot, sizeof(*slot));
        ops->invalidate(ops->context);
    }
    *slot = value;
    ops->clean(ops->context, slot, sizeof(*slot));
    ops->invalidate(ops->context);
}

int AArch64ProtectPage(const struct AArch64ProtectionOps *ops, uint64_t ttbr0,
    uint64_t tcr, uint64_t address, unsigned permissions)
{
    unsigned tg = (unsigned)((tcr >> 14) & 3);
    unsigned page_bits, stride, va_bits, level, shift;
    uint64_t page_size, mask, count, *table, *slot, desc;

    if (!ops || !ops->resolve || !ops->allocate || !ops->clean ||
        !ops->invalidate || !ops->synchronize || (permissions & ~63U) ||
        ((permissions & AARCH64_PROTECT_EXEC_BOTH) &&
            (permissions & (AARCH64_PROTECT_EXEC | AARCH64_PROTECT_USER)) !=
                (AARCH64_PROTECT_EXEC | AARCH64_PROTECT_USER)) ||
        ((permissions & AARCH64_PROTECT_JIT) &&
            permissions != (AARCH64_PROTECT_JIT | AARCH64_PROTECT_READ |
                AARCH64_PROTECT_WRITE | AARCH64_PROTECT_EXEC |
                AARCH64_PROTECT_USER)) ||
        (!(permissions & AARCH64_PROTECT_JIT) &&
            (permissions & (AARCH64_PROTECT_WRITE | AARCH64_PROTECT_EXEC)) ==
                (AARCH64_PROTECT_WRITE | AARCH64_PROTECT_EXEC)) ||
        ((permissions & (AARCH64_PROTECT_WRITE | AARCH64_PROTECT_EXEC)) &&
            !(permissions & AARCH64_PROTECT_READ)))
        return 0;
    /* EPD0, HA/HD, DS: this walker does not implement their semantics. */
    if ((tcr & ((UINT64_C(1) << 7) | (UINT64_C(3) << 39) |
            (UINT64_C(1) << 59))) || ((tcr >> 32) & 7) > 5 ||
            (tg != 0 && tg != 2))
        return 0;
    page_bits = tg == 0 ? 12 : 14;
    stride = page_bits - 3;
    va_bits = 64 - (unsigned)(tcr & 63);
    if (va_bits < 25 || va_bits > 48 || address >> va_bits)
        return 0;
    page_size = UINT64_C(1) << page_bits;
    if (address & (page_size - 1))
        return 0;
    mask = UINT64_C(0x0000ffffffffffff) & ~(page_size - 1);
    count = page_size / sizeof(uint64_t);
    level = 3 - (va_bits - page_bits - 1) / stride;
    table = ops->resolve(ops->context, ttbr0 & mask);
    if (!table)
        return 0;
    for (; level < 3; ++level) {
        shift = page_bits + stride * (3 - level);
        slot = &table[(address >> shift) & (count - 1)];
        desc = *slot;
        if (!(desc & VALID))
            return 0;
        if ((desc & TYPE) == TABLE) {
            /* Do not silently promise permissions denied by a parent. */
            if (desc & TABLE_RESTRICTIONS)
                return 0;
            table = ops->resolve(ops->context, desc & mask);
        } else {
            uint64_t physical, *child, attributes, base, i;
            unsigned child_shift = shift - stride;
            if (level == 0 || (desc & (CONTIGUOUS | GUARDED)) ||
                ((desc & mask) & ((UINT64_C(1) << shift) - 1)))
                return 0;
            if (ops->can_split && !ops->can_split(ops->context, desc))
                return 0;
            physical = ops->allocate(ops->context, page_bits);
            if (!physical || (physical & ~mask))
                return 0;
            child = ops->resolve(ops->context, physical);
            if (!child)
                return 0;
            base = desc & mask;
            attributes = desc & ~(mask | TYPE);
            for (i = 0; i < count; ++i)
                child[i] = (base + (i << child_shift)) | attributes |
                    (level == 2 ? TABLE : VALID);
            ops->clean(ops->context, child, (size_t)page_size);
            replace(ops, slot, physical | TABLE);
            table = child;
        }
        if (!table)
            return 0;
    }
    slot = &table[(address >> page_bits) & (count - 1)];
    desc = *slot;
    if ((desc & CONTIGUOUS) ||
        ((desc & VALID) ? ((desc & TYPE) != TABLE || (desc & GUARDED)) :
            ((desc & (GUARDED | TYPE)) != (GUARDED | 2))))
        return 0;
    if (!(permissions & (AARCH64_PROTECT_READ | AARCH64_PROTECT_WRITE |
            AARCH64_PROTECT_EXEC))) {
        desc = (desc & ~VALID) | GUARDED;
    } else {
        desc = (desc & ~(AP_USER | AP_RO | PXN | UXN | GUARDED)) | VALID;
        if (permissions & AARCH64_PROTECT_USER)
            desc |= AP_USER;
        if (!(permissions & AARCH64_PROTECT_WRITE))
            desc |= AP_RO;
        /* Execute is granted only at the requested privilege level. */
        if (!(permissions & AARCH64_PROTECT_EXEC))
            desc |= PXN | UXN;
        else if (permissions & AARCH64_PROTECT_JIT) {
            /* The JIT descriptor: writable at EL0, PXN, no UXN. Published
             * readable and non-executable first, like any code page. */
            desc |= PXN;
            replace(ops, slot, desc | UXN);
            ops->synchronize(ops->context, address, (size_t)page_size);
        } else {
            if (!(permissions & AARCH64_PROTECT_EXEC_BOTH))
                desc |= permissions & AARCH64_PROTECT_USER ? PXN : UXN;
            /* A guard has no translation: publish a readable NX mapping
             * before cache maintenance through its VA, then grant execute.
             * Users are quiesced by the caller for the whole operation. */
            replace(ops, slot, desc | PXN | UXN);
            ops->synchronize(ops->context, address, (size_t)page_size);
        }
    }
    replace(ops, slot, desc);
    return 1;
}
