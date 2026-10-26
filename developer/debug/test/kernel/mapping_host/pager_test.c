/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pager.h"

#define PAGE 4096
struct Fixture {
    unsigned char bytes[PAGE];
    unsigned allocations, releases, reads, live;
    unsigned cancel_check, cancel_on_check, cancel_on_read;
    size_t chunk, eof;
    uint64_t base;
    int allocation_failure, bad_geometry, read_failure, oversized, cancelled;
};

static int allocate(void *context, size_t size, struct KrnPagerPage *page)
{
    struct Fixture *f = context;
    assert(size == PAGE && !f->live);
    ++f->allocations;
    /* A failed allocator may leave diagnostics in its output, but owns any
     * partial cleanup. Such an output must never escape as a usable page. */
    page->physical = f->bad_geometry ? 0x10001 : 0x10000;
    page->alias = f->bytes;
    if (f->allocation_failure)
        return 0;
    f->live = 1;
    memset(f->bytes, 0xA5, PAGE);
    return 1;
}

static void release(void *context, struct KrnPagerPage *page, size_t size)
{
    struct Fixture *f = context;
    assert(size == PAGE && f->live && page->alias == f->bytes);
    ++f->releases;
    f->live = 0;
}

static unsigned char pattern(uint64_t offset)
{
    return (unsigned char)((offset * 17 + (offset >> 9) + 31) & 255);
}

static int read_at(void *context, uint64_t offset, void *buffer, size_t size,
    size_t *actual)
{
    struct Fixture *f = context;
    assert(f->live && offset >= f->base && offset - f->base < PAGE);
    assert(size && size <= PAGE - (size_t)(offset - f->base));
    ++f->reads;
    if (f->cancel_on_read == f->reads)
        f->cancelled = 1;
    if (f->read_failure && f->reads == 2)
        return 0;
    if (f->oversized) {
        *actual = size + 1;
        return 1;
    }
    size_t position = (size_t)(offset - f->base);
    size_t count = position >= f->eof ? 0 : f->eof - position;
    if (count > size) count = size;
    if (count > f->chunk) count = f->chunk;
    for (size_t i = 0; i < count; ++i)
        ((unsigned char *)buffer)[i] = pattern(offset + i);
    *actual = count;
    return 1;
}

static int cancelled(void *context)
{
    struct Fixture *f = context;
    ++f->cancel_check;
    return f->cancelled || (f->cancel_on_check &&
        f->cancel_check >= f->cancel_on_check);
}

static struct KrnPagerOps ops(struct Fixture *f)
{
    return (struct KrnPagerOps){
        .context = f, .allocate = allocate, .release = release,
        .read_at = read_at, .cancelled = cancelled
    };
}

static struct Fixture fresh(void)
{
    return (struct Fixture){.chunk = 97, .eof = PAGE, .base = 2 * PAGE};
}

static void failed(struct Fixture *f, struct KrnPagerPage page,
    enum KrnPagerResult actual, enum KrnPagerResult expected)
{
    assert(actual == expected);
    assert(!page.physical && !page.alias && !f->live);
    assert(f->releases == (f->allocations && !f->allocation_failure ? 1u : 0u));
}

int main(void)
{
    struct Fixture f = fresh();
    struct KrnPagerOps o = ops(&f);
    struct KrnPagerPage page;
    enum KrnPagerResult result;

    assert(KrnPagerPageIn(&o, PAGE, KPAGER_ZERO, 0, 0, &page) == KPAGER_OK);
    for (size_t i = 0; i < PAGE; ++i) assert(f.bytes[i] == 0);
    assert(f.allocations == 1 && !f.reads && !f.releases);
    release(&f, &page, PAGE);

    /* Short positive reads are assembled without moving a shared file cursor;
     * a final partial page contains source bytes followed by zeroes. */
    f = fresh(); o = ops(&f);
    assert(KrnPagerPageIn(&o, PAGE, KPAGER_FILE, f.base,
        f.base + 701, &page) == KPAGER_OK);
    assert(f.reads == 8);
    for (size_t i = 0; i < PAGE; ++i)
        assert(f.bytes[i] == (i < 701 ? pattern(f.base + i) : 0));
    release(&f, &page, PAGE);

    /* Each refusal has its own oracle and observes allocation ownership. */
    for (unsigned mode = 0; mode < 9; ++mode) {
        f = fresh(); o = ops(&f);
        enum KrnPagerResult expected = KPAGER_INVALID;
        uint64_t length = f.base + PAGE;
        if (mode == 0) { f.allocation_failure = 1; expected = KPAGER_NO_MEMORY; }
        if (mode == 1) f.bad_geometry = 1;
        if (mode == 2) { f.read_failure = 1; expected = KPAGER_IO; }
        if (mode == 3) { f.oversized = 1; expected = KPAGER_IO; }
        if (mode == 4) { f.eof = 123; expected = KPAGER_TRUNCATED; }
        if (mode == 5) { length = f.base; expected = KPAGER_TRUNCATED; }
        if (mode == 6) { f.cancelled = 1; expected = KPAGER_CANCELLED; }
        if (mode == 7) { f.cancel_on_read = 1; expected = KPAGER_CANCELLED; }
        if (mode == 8) {
            f.chunk = PAGE; f.cancel_on_read = 1; expected = KPAGER_CANCELLED;
        }
        result = KrnPagerPageIn(&o, PAGE, KPAGER_FILE, f.base, length, &page);
        failed(&f, page, result, expected);
        if (mode == 5 || mode == 6) assert(!f.allocations);
        if (mode == 7 || mode == 8) assert(f.reads == 1);
    }

    f = fresh(); o = ops(&f); f.cancel_on_check = 2;
    result = KrnPagerPageIn(&o, PAGE, KPAGER_ZERO, 0, 0, &page);
    failed(&f, page, result, KPAGER_CANCELLED);

    f = fresh(); o = ops(&f); o.cancelled = NULL;
    assert(KrnPagerPageIn(&o, PAGE, KPAGER_FILE, f.base,
        f.base + PAGE, &page) == KPAGER_OK);
    for (size_t i = 0; i < PAGE; ++i) assert(f.bytes[i] == pattern(f.base + i));
    release(&f, &page, PAGE);

    /* High offsets must not wrap at the final byte of a 64-bit file size. */
    f = fresh(); f.base = UINT64_MAX - (PAGE - 1); o = ops(&f);
    assert(KrnPagerPageIn(&o, PAGE, KPAGER_FILE, f.base,
        UINT64_MAX, &page) == KPAGER_OK);
    for (size_t i = 0; i < PAGE; ++i)
        assert(f.bytes[i] == (i < PAGE - 1 ? pattern(f.base + i) : 0));
    release(&f, &page, PAGE);

    f = fresh(); o = ops(&f);
    result = KrnPagerPageIn(&o, PAGE, KPAGER_FILE, 1, PAGE, &page);
    failed(&f, page, result, KPAGER_INVALID);
    result = KrnPagerPageIn(&o, PAGE - 1, KPAGER_ZERO, 0, 0, &page);
    failed(&f, page, result, KPAGER_INVALID);
    puts("mapping pager HOST: PASS (zero/file/EOF/errors/cancellation/ownership)");
    return 0;
}
