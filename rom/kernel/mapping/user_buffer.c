/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#include "user_buffer.h"

struct KrnBufferSpace *KrnBufferLookup(struct KrnBufferRegistry *r,
    uint64_t principal, uint64_t handle)
{
    if (!r || !principal || !handle) return NULL;
    for (struct KrnBufferSpace *s=r->first;s;s=s->next)
        if (s->domain && s->principal==principal && s->generation==handle) return s;
    return NULL;
}

int KrnBufferRegister(struct KrnBufferRegistry *r,struct KrnBufferSpace *space,uint64_t principal,
    struct KrnMappingDomain *d,struct KrnMappingArena *a,uint64_t *handle)
{
    if (!r || !space || space->domain || space->next || space->retention.domain ||
        space->leases || space->closing || !principal || !d || d->closing || !a ||
        !a->page_size || a->first || !handle || r->generation==UINT64_MAX) return 0;
    for (struct KrnBufferSpace *s=r->first;s;s=s->next)
        if (s==space || s->domain==d || s->arena==a) return 0;
    if (!KrnMappingDomainJoin(d,&space->retention,space)) return 0;
    space->domain=d;space->arena=a;space->principal=principal;
    space->generation=++r->generation;space->next=r->first;r->first=space;
    *handle=space->generation;return 1;
}

int KrnBufferClose(struct KrnBufferRegistry *r, uint64_t principal, uint64_t handle)
{
    struct KrnBufferSpace *s = KrnBufferLookup(r, principal, handle);
    if (!s) return 0;
    s->closing = 1; KrnMappingDomainStop(s->domain);
    for (struct KrnMappingObject *m = s->arena->first; m; m = m->next)
        (void)KrnMappingBeginClose(m);
    return 1;
}

int KrnBufferUnregister(struct KrnBufferRegistry *r, uint64_t principal, uint64_t handle)
{
    struct KrnBufferSpace *s = KrnBufferLookup(r, principal, handle);
    if (!s || !s->closing || s->leases || s->arena->first ||
        !KrnMappingDomainLeave(&s->retention)) return 0;
    struct KrnBufferSpace **link=&r->first;
    while (*link!=s) link=&(*link)->next;
    *link=s->next;*s=(struct KrnBufferSpace){0};
    return 1;
}

static int part_valid(const struct KrnBufferSpace *s, const struct KrnBufferPart *b,
    unsigned access)
{
    struct KrnMappingObject *m = b->mapping;
    if (!m || m->owner != s || m->closing || m->shared || b->page >= m->page_count ||
        KrnMappingLookup(s->arena, m->start) != m || !b->length ||
        b->offset >= m->page_size || b->length > m->page_size - b->offset) return 0;
    struct KrnMappingPage *p = &m->pages[b->page];
    return p->physical && p->physical == b->physical && p->generation == b->generation &&
        (p->protection & access) == access &&
        (p->state == KMAP_DIRTY || (access == KMAP_READ && p->state == KMAP_CLEAN));
}

int KrnBufferPin(struct KrnBufferRegistry *r, uint64_t principal, uint64_t handle,
    uintptr_t address, size_t length, unsigned access,
    const struct KrnBufferOps *ops, struct KrnBufferLease *lease)
{
    struct KrnBufferSpace *s = KrnBufferLookup(r, principal, handle);
    if (!s || s->closing || s->domain->closing || s->leases == SIZE_MAX ||
        !lease || lease->space || lease->retention.domain || !ops || !ops->admit ||
        !length || length > KBUF_BYTES || length > UINTPTR_MAX - address ||
        (access != KMAP_READ && access != KMAP_WRITE)) return 0;
    struct KrnBufferLease temp = {0};
    temp.space = s; temp.generation = s->generation; temp.access = access; temp.length = length;
    size_t remaining = length;
    while (remaining) {
        if (temp.count == KBUF_PAGES) return 0;
        struct KrnMappingObject *m = KrnMappingLookup(s->arena, address);
        if (!m) return 0;
        size_t page = (address - m->start) / m->page_size;
        size_t offset = (address - m->start) % m->page_size;
        size_t n = m->page_size - offset;
        if (n > remaining) n = remaining;
        struct KrnBufferPart b = {m, page, offset, n, m->pages[page].physical, m->pages[page].generation};
        if (!part_valid(s, &b, access) || !ops->admit(ops->context, s,
            m->start + page * m->page_size, b.physical, m->page_size, access)) return 0;
        size_t refs = 1;
        for (unsigned j = 0; j < temp.count; ++j) if (temp.parts[j].mapping == m) ++refs;
        if (m->in_flight > SIZE_MAX - refs) return 0;
        temp.parts[temp.count++] = b;
        remaining -= n; address += n;
    }
    /* Retain the actual caller's record, never the temporary preflight copy. */
    if (!KrnMappingDomainJoin(s->domain, &lease->retention, lease)) return 0;
    temp.retention = lease->retention; *lease = temp;
    for (unsigned i = 0; i < lease->count; ++i) ++lease->parts[i].mapping->in_flight;
    ++s->leases;
    return 1;
}

static int aliases(const struct KrnBufferLease *l, uint64_t principal,
    unsigned access, const struct KrnBufferOps *ops, unsigned char **bytes)
{
    if (!l || !l->space || !ops || !ops->admit || !l->count || l->count > KBUF_PAGES) return 0;
    struct KrnBufferSpace *s = l->space;
    if (!s->domain || s->principal != principal || s->generation != l->generation ||
        s->closing || s->domain->closing || l->access != access ||
        l->retention.domain != s->domain || l->retention.identity != l) return 0;
    size_t length = 0;
    for (unsigned i = 0; i < l->count; ++i) {
        const struct KrnBufferPart *b = &l->parts[i];
        if (!part_valid(s, b, access)) return 0;
        struct KrnMappingObject *m = b->mapping;
        bytes[i] = ops->admit(ops->context, s, m->start + b->page * m->page_size,
            b->physical, m->page_size, access);
        if (!bytes[i]) return 0;
        length += b->length;
    }
    return length == l->length;
}

int KrnBufferRead(const struct KrnBufferLease *l, uint64_t principal,
    void *output, size_t capacity, const struct KrnBufferOps *ops)
{
    unsigned char *parts[KBUF_PAGES], *dst = output;
    if (!l || !output || capacity < l->length || !aliases(l, principal, KMAP_READ, ops, parts)) return 0;
    size_t n = 0;
    for (unsigned i = 0; i < l->count; ++i)
        for (size_t j = 0; j < l->parts[i].length; ++j) dst[n++] = parts[i][l->parts[i].offset + j];
    return 1;
}

int KrnBufferWrite(const struct KrnBufferLease *l, uint64_t principal,
    const void *input, size_t length, const struct KrnBufferOps *ops)
{
    unsigned char *parts[KBUF_PAGES]; const unsigned char *src = input;
    if (!l || !input || length != l->length || !aliases(l, principal, KMAP_WRITE, ops, parts)) return 0;
    size_t n = 0;
    for (unsigned i = 0; i < l->count; ++i)
        for (size_t j = 0; j < l->parts[i].length; ++j) parts[i][l->parts[i].offset + j] = src[n++];
    return 1;
}

int KrnBufferRelease(struct KrnBufferLease *l)
{
    if (!l || !l->space || !l->space->leases || !l->count || l->count > KBUF_PAGES ||
        l->retention.domain != l->space->domain || l->retention.identity != l) return 0;
    for (unsigned i = 0; i < l->count; ++i) {
        size_t refs = 0;
        for (unsigned j = 0; j < l->count; ++j) if (l->parts[i].mapping == l->parts[j].mapping) ++refs;
        if (l->parts[i].mapping->in_flight < refs) return 0;
    }
    if (!KrnMappingDomainLeave(&l->retention)) return 0;
    for (unsigned i = 0; i < l->count; ++i) --l->parts[i].mapping->in_flight;
    --l->space->leases;
    *l = (struct KrnBufferLease){0};
    return 1;
}
