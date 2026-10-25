/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Searches of a page bitmap (one bit per page, set when used), shared
 *       by the code arena (page_allocator.c) and the code window
 *       (code_window.c). The bitmap is searched a word at a time: 64 pages
 *       per test where a word is all free or all used. Each helper stays
 *       within [low, high).
 */

#ifndef AROS_AARCH64_APPLE_PAGE_BITMAP_H
#define AROS_AARCH64_APPLE_PAGE_BITMAP_H

#include <stdint.h>

static inline int page_bitmap_test(const uint64_t *bits, uint32_t page)
{
    return (bits[page / 64U] & (UINT64_C(1) << (page % 64U))) != 0;
}

static inline void page_bitmap_set(uint64_t *bits, uint32_t page, int used)
{
    uint64_t mask = UINT64_C(1) << (page % 64U);

    if (used)
        bits[page / 64U] |= mask;
    else
        bits[page / 64U] &= ~mask;
}

/* The first page at or above index, below high, that is used (or free);
 * high when there is none. */
static inline uint32_t page_bitmap_next(
    const uint64_t *bits, uint32_t index, uint32_t high, int used)
{
    while (index < high) {
        uint64_t word = bits[index / 64U];

        if (!used)
            word = ~word;
        word >>= index % 64U;
        if (word != 0) {
            index += (uint32_t)__builtin_ctzll(word);
            return index < high ? index : high;
        }
        index = (index | 63U) + 1U;
    }
    return high;
}

/* The highest page below high, at or above low, that is used (or free);
 * low - 1 (as UINT32_MAX when low is 0) when there is none. */
static inline uint32_t page_bitmap_previous(
    const uint64_t *bits, uint32_t high, uint32_t low, int used)
{
    while (high > low) {
        uint32_t last = high - 1U;
        uint64_t word = bits[last / 64U];
        uint32_t keep = last % 64U;

        if (!used)
            word = ~word;
        if (keep != 63U)
            word &= (UINT64_C(1) << (keep + 1U)) - 1U;
        if (word != 0) {
            uint32_t found = (last & ~63U) + 63U - (uint32_t)__builtin_clzll(word);
            return found >= low ? found : low - 1U;
        }
        high = last & ~63U;
    }
    return low - 1U;
}

/* The lowest start in [first, limit] of a free run of pages pages. */
static inline int page_bitmap_find_up(const uint64_t *bits,
    uint32_t first, uint32_t limit, uint32_t pages, uint32_t *start)
{
    uint32_t end = limit + pages;

    while (first <= limit) {
        uint32_t used;

        first = page_bitmap_next(bits, first, end, 0);
        if (first > limit)
            return 0;
        used = page_bitmap_next(bits, first, first + pages, 1);
        if (used == first + pages) {
            *start = first;
            return 1;
        }
        first = used + 1U;
    }
    return 0;
}

/* The highest start of such a run that ends at or below available. */
static inline int page_bitmap_find_down(const uint64_t *bits,
    uint32_t available, uint32_t pages, uint32_t *start)
{
    uint32_t high = available;

    while (high >= pages) {
        uint32_t free_top = page_bitmap_previous(bits, high, 0, 0);
        uint32_t used;

        if (free_top == UINT32_MAX || free_top + 1U < pages)
            return 0;
        used = page_bitmap_previous(bits, free_top + 1U, free_top + 1U - pages, 1);
        if (used == free_top - pages) {
            *start = free_top + 1U - pages;
            return 1;
        }
        high = used;
    }
    return 0;
}

/* The longest free run below available. */
static inline uint32_t page_bitmap_longest_free(const uint64_t *bits,
    uint32_t available)
{
    uint32_t index, run, largest = 0;

    for (index = 0; index < available; index = run + 1U) {
        index = page_bitmap_next(bits, index, available, 0);
        run = page_bitmap_next(bits, index, available, 1);
        if (run - index > largest)
            largest = run - index;
    }
    return largest;
}

#endif /* AROS_AARCH64_APPLE_PAGE_BITMAP_H */
