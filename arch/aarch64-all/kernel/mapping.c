/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Arm DDI 0487: VMSAv8-64 descriptor and break-before-make rules.
 */
#include "mapping.h"

#define M_VALID UINT64_C(1)
#define M_TABLE UINT64_C(3)
#define M_GUARDED (UINT64_C(1) << 58)
#define M_CONTIGUOUS (UINT64_C(1) << 52)
#define M_TABLE_RESTRICTIONS (UINT64_C(0x1f) << 59)
#define M_PXN (UINT64_C(1) << 53)
#define M_UXN (UINT64_C(1) << 54)

struct MappingWalk {
    unsigned page_bits, stride, va_bits, first, physical_bits;
    uint64_t size, count, mask;
};

static int geometry(uint64_t tcr,
    uint64_t va, uint64_t pa, struct MappingWalk *walk)
{
    unsigned tg = (unsigned)((tcr >> 14) & 3);
    if ((tcr & ((UINT64_C(1) << 7) | (UINT64_C(1) << 37) |
                (UINT64_C(3) << 39) |
                (UINT64_C(1) << 59))) || ((tcr >> 32) & 7) > 5 ||
        (tg != 0 && tg != 2))
        return 0;
    walk->page_bits = tg ? 14 : 12;
    walk->stride = walk->page_bits - 3;
    walk->va_bits = 64 - (unsigned)(tcr & 63);
    walk->size = UINT64_C(1) << walk->page_bits;
    walk->count = walk->size / sizeof(uint64_t);
    walk->mask = UINT64_C(0x0000ffffffffffff) & ~(walk->size - 1);
    if (walk->va_bits < 25 || walk->va_bits > 48 || va >> walk->va_bits ||
        (va & (walk->size - 1)) || (pa & ~walk->mask))
        return 0;
    /* IPS smaller than 48 bits constrains physical addresses, too. */
    static const unsigned physical_bits[] = {32, 36, 40, 42, 44, 48};
    walk->physical_bits = physical_bits[(tcr >> 32) & 7];
    if (pa >> walk->physical_bits)
        return 0;
    walk->first = 3 - (walk->va_bits - walk->page_bits - 1) / walk->stride;
    return 1;
}

static int physical_valid(const struct MappingWalk *walk, uint64_t pa)
{
    return !(pa & ~walk->mask) && !(pa >> walk->physical_bits);
}

static int root_valid(const struct MappingWalk *walk, uint64_t ttbr0)
{
    /* ASID lives above the supported PA bits; CnP is bit zero. Neither
     * permits silently rounding a misaligned table base down. */
    return !(ttbr0 & (walk->size - 1) & ~UINT64_C(1)) &&
        physical_valid(walk, ttbr0 & walk->mask);
}

static int permissions_valid(unsigned permissions)
{
    return !(permissions & ~31U) &&
        (!((permissions & (AARCH64_PROTECT_WRITE | AARCH64_PROTECT_EXEC))) ||
            (permissions & AARCH64_PROTECT_READ)) &&
        (permissions & (AARCH64_PROTECT_WRITE | AARCH64_PROTECT_EXEC)) !=
            (AARCH64_PROTECT_WRITE | AARCH64_PROTECT_EXEC) &&
        (!(permissions & AARCH64_PROTECT_EXEC_BOTH) ||
            (permissions & (AARCH64_PROTECT_EXEC | AARCH64_PROTECT_USER)) ==
                (AARCH64_PROTECT_EXEC | AARCH64_PROTECT_USER));
}

/* Bits 49:48 require an output-address format outside this <=48-bit walker.
 * Bits 13:12 must be zero for a 16K page/table output address. */
static int descriptor_address_valid(const struct MappingWalk *walk, uint64_t desc)
{
    return !(desc & UINT64_C(0x0003000000000000)) &&
        !(desc & (walk->size - 1) & ~UINT64_C(0xfff)) &&
        physical_valid(walk, desc & walk->mask);
}

static uint64_t *leaf(const struct AArch64ProtectionOps *ops, uint64_t ttbr0,
    const struct MappingWalk *walk, uint64_t va, int create)
{
    uint64_t root = ttbr0 & walk->mask;
    if (!root_valid(walk, ttbr0))
        return NULL;
    uint64_t *table = ops->resolve(ops->context, root);
    if (!table)
        return NULL;
    for (unsigned level = walk->first; level < 3; ++level) {
        unsigned shift = walk->page_bits + walk->stride * (3 - level);
        uint64_t *slot = &table[(va >> shift) & (walk->count - 1)];
        if (*slot == 0 && create) {
            uint64_t physical = ops->allocate(ops->context, walk->page_bits);
            if (!physical || !physical_valid(walk, physical))
                return NULL;
            uint64_t *child = ops->resolve(ops->context, physical);
            if (!child)
                return NULL;
            for (uint64_t i = 0; i < walk->count; ++i)
                child[i] = 0;
            ops->clean(ops->context, child, (size_t)walk->size);
            *slot = physical | M_TABLE;
            ops->clean(ops->context, slot, sizeof(*slot));
            ops->invalidate(ops->context);
        }
        if ((*slot & M_TABLE) != M_TABLE ||
            (*slot & (M_TABLE_RESTRICTIONS | M_GUARDED | M_CONTIGUOUS)) ||
            !descriptor_address_valid(walk, *slot))
            return NULL;
        table = ops->resolve(ops->context, *slot & walk->mask);
        if (!table)
            return NULL;
    }
    return &table[(va >> walk->page_bits) & (walk->count - 1)];
}

int AArch64MapPage(const struct AArch64ProtectionOps *ops, uint64_t ttbr0,
    uint64_t tcr, uint64_t va, uint64_t pa, unsigned permissions,
    unsigned normal_index)
{
    struct MappingWalk walk;
    if (!ops || !ops->resolve || !ops->allocate || !ops->clean ||
        !ops->invalidate || !ops->synchronize ||
        !geometry(tcr, va, pa, &walk) || normal_index > 7 ||
        !permissions_valid(permissions))
        return 0;
    uint64_t *slot = leaf(ops, ttbr0, &walk, va, 1);
    if (!slot || *slot)
        return 0;
    uint64_t desc = pa | M_TABLE | ((uint64_t)normal_index << 2) |
        (UINT64_C(3) << 8) | (UINT64_C(1) << 10) | M_PXN | M_UXN;
    if (permissions & AARCH64_PROTECT_USER)
        desc |= UINT64_C(1) << 6;
    if (!(permissions & AARCH64_PROTECT_WRITE))
        desc |= UINT64_C(1) << 7;
    if (!(permissions & AARCH64_PROTECT_READ))
        desc = (desc & ~M_VALID) | M_GUARDED;
    *slot = desc;
    ops->clean(ops->context, slot, sizeof(*slot));
    ops->invalidate(ops->context);
    if (permissions & AARCH64_PROTECT_EXEC) {
        /* Maintenance uses the new readable alias before execute is granted. */
        ops->synchronize(ops->context, va, (size_t)walk.size);
        *slot = 0;
        ops->clean(ops->context, slot, sizeof(*slot));
        ops->invalidate(ops->context);
        if (permissions & AARCH64_PROTECT_EXEC_BOTH)
            desc &= ~(M_PXN | M_UXN);
        else
            desc &= ~(permissions & AARCH64_PROTECT_USER ? M_UXN : M_PXN);
        *slot = desc;
        ops->clean(ops->context, slot, sizeof(*slot));
        ops->invalidate(ops->context);
    }
    return 1;
}

int AArch64UnmapPage(const struct AArch64ProtectionOps *ops, uint64_t ttbr0,
    uint64_t tcr, uint64_t va, uint64_t pa)
{
    struct MappingWalk walk;
    if (!ops || !ops->resolve || !ops->clean || !ops->invalidate ||
        !geometry(tcr, va, pa, &walk))
        return 0;
    uint64_t *slot = leaf(ops, ttbr0, &walk, va, 0);
    if (!slot || !descriptor_address_valid(&walk, *slot) ||
        (*slot & walk.mask) != pa || (*slot & M_CONTIGUOUS) ||
        ((*slot & M_VALID) ? ((*slot & M_TABLE) != M_TABLE ||
            (*slot & M_GUARDED)) :
            ((*slot & (M_GUARDED | M_TABLE)) != (M_GUARDED | 2))))
        return 0;
    *slot = 0;
    ops->clean(ops->context, slot, sizeof(*slot));
    ops->invalidate(ops->context);
    return 1;
}

int AArch64UnmapPrivilegedPage(const struct AArch64ProtectionOps *ops,
    uint64_t ttbr0, uint64_t tcr, uint64_t va, uint64_t expected_physical)
{
    struct MappingWalk walk;
    if (!ops || !ops->resolve ||
        !geometry(tcr, va, expected_physical, &walk)) return 0;
    uint64_t *slot = leaf(ops, ttbr0, &walk, va, 0);
    if (!slot || (*slot & (UINT64_C(1) << 6)) != 0 ||
        (*slot & M_UXN) == 0) return 0;
    return AArch64UnmapPage(ops, ttbr0, tcr, va, expected_physical);
}

int AArch64MappingPhysical(const struct AArch64ProtectionOps *ops,
    uint64_t ttbr0, uint64_t tcr, uint64_t va, uint64_t *physical)
{
    struct MappingWalk walk;
    if (!ops || !ops->resolve || !physical || !geometry(tcr,va,0,&walk))
        return 0;
    uint64_t *slot=leaf(ops,ttbr0,&walk,va,0);
    if (!slot || !descriptor_address_valid(&walk, *slot) || (*slot & M_CONTIGUOUS) ||
        ((*slot & M_VALID) ? ((*slot & M_TABLE)!=M_TABLE ||
            (*slot & M_GUARDED)) :
            ((*slot & (M_GUARDED|M_TABLE))!=(M_GUARDED|2))))
        return 0;
    *physical=*slot & walk.mask;
    return 1;
}

int AArch64UserPagePhysical(const struct AArch64ProtectionOps *ops,
    uint64_t ttbr0, uint64_t tcr, uint64_t va, unsigned access,
    unsigned normal_index, uint64_t *physical)
{
    struct MappingWalk walk;
    if (!ops || !ops->resolve || !physical || normal_index > 7 ||
        (access != AARCH64_PROTECT_READ && access != AARCH64_PROTECT_WRITE &&
         access != AARCH64_PROTECT_EXEC) ||
        !geometry(tcr,va,0,&walk)) return 0;
    uint64_t *slot = leaf(ops,ttbr0,&walk,va,0);
    if (!slot || !descriptor_address_valid(&walk,*slot) ||
        (*slot & (M_GUARDED | M_CONTIGUOUS)) || (*slot & M_TABLE) != M_TABLE ||
        !(*slot & (UINT64_C(1) << 10)) ||
        !(*slot & (UINT64_C(1) << 6)) ||
        (access == AARCH64_PROTECT_WRITE && (*slot & (UINT64_C(1) << 7))) ||
        (access == AARCH64_PROTECT_EXEC && ((*slot & M_UXN) || !(*slot & (UINT64_C(1) << 7)))) ||
        ((*slot >> 2) & 7) != normal_index) return 0;
    *physical = *slot & walk.mask;
    return 1;
}

int AArch64RemapPage(const struct AArch64ProtectionOps *ops, uint64_t ttbr0,
    uint64_t tcr, uint64_t va, uint64_t expected_physical, unsigned permissions)
{
    uint64_t actual;
    if (!ops || !ops->allocate || !ops->clean || !ops->invalidate ||
        !ops->synchronize || !permissions_valid(permissions) ||
        !AArch64MappingPhysical(ops, ttbr0, tcr, va, &actual) ||
        actual != expected_physical)
        return 0;
    /* Caller holds the same mutation lock through lookup and update. No
     * block splitting is reachable: MappingPhysical requires a leaf. */
    return AArch64ProtectPage(ops, ttbr0, tcr, va, permissions);
}

int AArch64RemapPrivilegedPage(const struct AArch64ProtectionOps *ops,uint64_t ttbr0,
    uint64_t tcr,uint64_t va,uint64_t expected_physical,unsigned permissions)
{
    struct MappingWalk walk;
    if (!ops || !ops->resolve || (permissions & ~7U) ||
        !geometry(tcr,va,expected_physical,&walk)) return 0;
    uint64_t *slot=leaf(ops,ttbr0,&walk,va,0);
    if (!slot || (*slot & (UINT64_C(1)<<6)) || !(*slot & M_UXN)) return 0;
    return AArch64RemapPage(ops,ttbr0,tcr,va,expected_physical,permissions);
}

int AArch64TranslateResident(const struct AArch64ProtectionOps *ops,
    uint64_t ttbr0, uint64_t tcr, uint64_t va, uint64_t *physical)
{
    struct MappingWalk walk;
    unsigned page=(unsigned)((tcr>>14)&3)==2 ? 14 : 12;
    if (!ops || !ops->resolve || !physical ||
        !geometry(tcr, va & ~((UINT64_C(1)<<page)-1), 0, &walk))
        return 0;
    uint64_t mask=walk.mask;
    uint64_t pa=ttbr0 & mask;
    if (!root_valid(&walk,ttbr0)) return 0;
    uint64_t *table=ops->resolve(ops->context,pa);
    for (unsigned level=walk.first;table && level<=3;++level) {
        unsigned shift=page+walk.stride*(3-level);
        uint64_t desc=table[(va>>shift)&((UINT64_C(1)<<walk.stride)-1)];
        unsigned type=(unsigned)(desc&3);
        if (!(desc&M_VALID) || (desc&M_GUARDED) ||
            !descriptor_address_valid(&walk,desc)) return 0;
        pa=desc & mask;
        if (level==3 || type==1) {
            /* Level zero blocks are reserved. A level three leaf is type3.
             * Block output-address low bits must be zero, not silently lost. */
            uint64_t offset=(UINT64_C(1)<<shift)-1;
            if ((level==3 ? type!=3 : level==0 || (page==14 && level==1) || type!=1) || (pa&offset))
                return 0;
            *physical=pa|(va&offset);
            return 1;
        }
        if (type!=3) return 0;
        table=ops->resolve(ops->context,pa);
    }
    return 0;
}

/* Snapshot into unpublished, separately owned tables. No live descriptor is
 * changed here. The allocator's ledger owns every page even on failure. */
static int clone_privileged(const struct AArch64ProtectionOps *ops,
    const struct MappingWalk *walk, uint64_t source, unsigned level,
    uint64_t entries, size_t *budget, uint64_t *destination, int count_only)
{
    if (!*budget) return 0;
    uint64_t *input = ops->resolve(ops->context, source);
    if (!input) return 0;
    uint64_t physical = 0;
    uint64_t *output = NULL;
    if (!count_only) {
        physical = ops->allocate(ops->context, walk->page_bits);
        if (!physical || !physical_valid(walk, physical)) return 0;
        output = ops->resolve(ops->context, physical);
        if (!output) return 0;
    }
    --*budget;
    for (uint64_t i = 0; i < walk->count; ++i) {
        uint64_t desc = input[i];
        if (output) output[i] = 0;
        if (!desc) continue;
        if (i >= entries || !(desc & M_VALID) ||
            !descriptor_address_valid(walk, desc) ||
            (desc & (M_GUARDED | M_CONTIGUOUS))) return 0;
        if (level < 3 && (desc & 3) == M_TABLE) {
            uint64_t child;
            if ((desc & ~(walk->mask | M_TABLE)) ||
                !clone_privileged(ops, walk, desc & walk->mask, level + 1,
                    walk->count, budget, &child, count_only)) return 0;
            if (output) output[i] = child | M_TABLE;
        } else {
            unsigned shift = walk->page_bits + walk->stride * (3 - level);
            uint64_t block_mask = (UINT64_C(1) << shift) - 1;
            if (!level || (desc & 3) != (level == 3 ? M_TABLE : M_VALID) ||
                ((desc & walk->mask) & block_mask)) return 0;
            /* Writable EL0 mappings are implicitly non-executable at EL1.
             * Preserve that restriction when removing EL0 permission. Refuse
             * already privileged writable/executable mappings: a restricted
             * kernel template must have an explicit W^X policy. */
            if (!(desc & (UINT64_C(1) << 7))) {
                if (!(desc & (UINT64_C(1) << 6)) && !(desc & M_PXN)) return 0;
                desc |= M_PXN;
            }
            if (output) output[i] = (desc & ~(UINT64_C(1) << 6)) | M_UXN;
        }
    }
    if (output) ops->clean(ops->context, output, (size_t)walk->size);
    *destination = physical;
    return 1;
}

int AArch64ClonePrivilegedRoot(const struct AArch64ProtectionOps *ops,
    uint64_t ttbr0, uint64_t tcr, size_t table_budget, uint64_t *output_root)
{
    struct MappingWalk walk;
    if (!ops || !ops->resolve || !ops->allocate || !ops->clean || !output_root ||
        !table_budget || !geometry(tcr, 0, 0, &walk) || !root_valid(&walk, ttbr0)) return 0;
    unsigned shift = walk.page_bits + walk.stride * (3 - walk.first);
    uint64_t entries = UINT64_C(1) << (walk.va_bits - shift), root;
    if (!clone_privileged(ops, &walk, ttbr0 & walk.mask, walk.first,
        entries, &table_budget, &root, 0)) return 0;
    *output_root = root;
    return 1;
}

int AArch64CountPrivilegedRoot(const struct AArch64ProtectionOps *ops,
    uint64_t ttbr0, uint64_t tcr, size_t table_budget, size_t *table_count)
{
    struct MappingWalk walk;
    if (!ops || !ops->resolve || !table_count || !table_budget ||
        !geometry(tcr, 0, 0, &walk) || !root_valid(&walk, ttbr0)) return 0;
    unsigned shift = walk.page_bits + walk.stride * (3 - walk.first);
    uint64_t entries = UINT64_C(1) << (walk.va_bits - shift), unused;
    size_t remaining = table_budget;
    if (!clone_privileged(ops, &walk, ttbr0 & walk.mask, walk.first,
        entries, &remaining, &unused, 1)) return 0;
    *table_count = table_budget - remaining;
    return 1;
}
