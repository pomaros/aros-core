/*
 * Copyright (C) 2026 John KNIPPER.
 * Distributed under the AROS Public License 1.1.
 */
#ifndef DOS_ELF_BRANCH_H
#define DOS_ELF_BRANCH_H
#include <stdint.h>
#include <stddef.h>

struct elf_branch_function { uint64_t value; uint32_t section; };
struct elf_branch_symbols { struct elf_branch_function *entries; uint32_t count; };

static inline int elf_branch_function_less(struct elf_branch_function a,
                                            struct elf_branch_function b)
{
    return a.section < b.section || (a.section == b.section && a.value < b.value);
}

/* Heapsort bounds both memory and work even for adversarial symbol ordering. */
static inline void elf_branch_function_sift(struct elf_branch_function *a,
                                             uint32_t root, uint32_t end)
{
    while (root < end / 2) {
        uint32_t child = root * 2 + 1;
        struct elf_branch_function tmp;
        if (child + 1 < end && elf_branch_function_less(a[child], a[child+1])) ++child;
        if (!elf_branch_function_less(a[root], a[child])) break;
        tmp = a[root]; a[root] = a[child]; a[child] = tmp;
        root = child;
    }
}

static inline void elf_branch_function_sort(struct elf_branch_symbols *table)
{
    uint32_t n = table->count, i;
    struct elf_branch_function *a = table->entries;
    if (n < 2) return;
    for (i = n / 2; i > 0; --i) elf_branch_function_sift(a, i - 1, n);
    for (i = n; i > 1; --i) {
        struct elf_branch_function tmp = a[0]; a[0] = a[i-1]; a[i-1] = tmp;
        elf_branch_function_sift(a, 0, i - 1);
    }
}

static inline int elf_branch_function_contains(const struct elf_branch_symbols *table,
                                                uint32_t section, uint64_t value)
{
    uint32_t lo = 0, hi = table->count;
    struct elf_branch_function key = { value, section };
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (elf_branch_function_less(table->entries[mid], key)) lo = mid + 1;
        else hi = mid;
    }
    return lo < table->count && table->entries[lo].section == section &&
        table->entries[lo].value == value;
}

/* AAELF64 permits IP0-clobbering veneers for function targets and references
 * across input sections. Intra-section non-function branches must be refused. */
struct elf_branch_pool {
    uint32_t before_bytes, after_bytes;
    uint32_t before_used, after_used;
    uintptr_t before, after;
};

static inline int elf_branch_reachable(uintptr_t place, uintptr_t target)
{
    if ((place | target) & 3) return 0;
    if (target >= place) return target - place <= UINT64_C(0x7fffffc);
    return place - target <= UINT64_C(0x8000000);
}

static inline int elf_branch_reserve(struct elf_branch_pool *pool,
                                      uint64_t size, uint64_t offset)
{
    uint32_t *budget;
    if (offset > size || size - offset < 4 || (offset & 3)) return 0;
    budget = offset < size / 2 ? &pool->before_bytes : &pool->after_bytes;
    if (*budget > UINT32_MAX - 16) return 0;
    *budget += 16;
    return 1;
}

/* Only consumes a pre-budgeted, aligned slot that the actual instruction can
 * reach. Large sections or exhausted pools never cause offset truncation. */
static inline uintptr_t elf_branch_slot(struct elf_branch_pool *pool,
                                        uintptr_t place, uint64_t offset,
                                        uint64_t size)
{
    int before = offset < size / 2;
    uintptr_t start = before ? pool->before : pool->after;
    uint32_t *used = before ? &pool->before_used : &pool->after_used;
    uint32_t budget = before ? pool->before_bytes : pool->after_bytes;
    uintptr_t slot;
    if (*used > budget || budget - *used < 16 || start > UINTPTR_MAX - *used)
        return 0;
    slot = start + *used;
    if ((slot & 7) || !elf_branch_reachable(place, slot)) return 0;
    *used += 16;
    return slot;
}
#endif
