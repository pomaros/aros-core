/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#include "mapping.h"

int KrnMappingInitialize(struct KrnMappingObject *m, uintptr_t start,
    size_t length, size_t size, uint64_t offset, unsigned protection,
    int shared, void *owner, struct KrnMappingPage *pages, size_t capacity)
{
    if (!m || !pages || !size || (size & (size - 1)) || !start || !length ||
        (start & (size - 1)) || (length & (size - 1)) ||
        (offset & (size - 1)) || length > UINTPTR_MAX - start ||
        length > UINT64_MAX - offset || length / size > capacity ||
        (protection & ~7U) ||
        ((protection & (KMAP_WRITE | KMAP_EXEC)) == (KMAP_WRITE | KMAP_EXEC)) ||
        ((protection & (KMAP_WRITE | KMAP_EXEC)) && !(protection & KMAP_READ)))
        return 0;
    *m = (struct KrnMappingObject){start, length, size, length / size, offset,
        owner, pages, protection, !!shared, 0, 0, 0, NULL, 0};
    for (size_t i = 0; i < m->page_count; ++i)
        pages[i] = (struct KrnMappingPage){0, 0, KMAP_ABSENT, 0, protection};
    return 1;
}

int KrnMappingArenaInitialize(struct KrnMappingArena *a, uintptr_t start,
    size_t length, size_t size)
{
    if (!a || !start || !size || (size & (size-1)) || !length ||
        (start & (size-1)) || (length & (size-1)) || length > UINTPTR_MAX-start)
        return 0;
    *a = (struct KrnMappingArena){start, length, size, NULL};
    return 1;
}

int KrnMappingReserve(struct KrnMappingArena *a, struct KrnMappingObject *m,
    size_t length, uint64_t offset, unsigned protection, int shared,
    void *owner, struct KrnMappingPage *pages, size_t capacity)
{
    if (!a || !m || !a->page_size || !length || length > a->length)
        return 0;
    /* Catch reuse of a live record before Initialize overwrites it. */
    for (struct KrnMappingObject *p=a->first; p; p=p->next)
        if (p==m)
            return 0;
    uintptr_t start=a->start;
    struct KrnMappingObject **link=&a->first;
    while (*link) {
        struct KrnMappingObject *p=*link;
        if (length <= p->start-start)
            break;
        start=p->start+p->length;
        link=&p->next;
    }
    if (length > a->start+a->length-start ||
        !KrnMappingInitialize(m,start,length,a->page_size,offset,protection,
            shared,owner,pages,capacity))
        return 0;
    m->next=*link;
    *link=m;
    return 1;
}

int KrnMappingReserveAt(struct KrnMappingArena *a, struct KrnMappingObject *m,
    uintptr_t start, size_t length, uint64_t offset, unsigned protection,
    int shared, void *owner, struct KrnMappingPage *pages, size_t capacity)
{
    if (!a || !m || !a->page_size || !length || start<a->start ||
        start-a->start>=a->length || length>a->length-(start-a->start))
        return 0;
    /* Preflight the entire list: m may be linked after the requested hole. */
    for (struct KrnMappingObject *p=a->first; p; p=p->next) {
        if (p==m) return 0;
        if (start<p->start+p->length && p->start<start+length) return 0;
    }
    if (!KrnMappingInitialize(m,start,length,a->page_size,offset,protection,
            shared,owner,pages,capacity)) return 0;
    struct KrnMappingObject **link=&a->first;
    while (*link && (*link)->start<start) link=&(*link)->next;
    m->next=*link;*link=m;
    return 1;
}

int KrnMappingReserveVirtual(struct KrnMappingArena *a, struct KrnMappingObject *m,
    uintptr_t address,size_t length,void *owner,struct KrnMappingPage *pages,size_t capacity)
{
    int ok=address ? KrnMappingReserveAt(a,m,address,length,0,0,0,owner,pages,capacity) :
        KrnMappingReserve(a,m,length,0,0,0,owner,pages,capacity);
    if (!ok) return 0;
    m->virtual_reservation=1;
    for (size_t i=0;i<m->page_count;++i) m->pages[i].state=KMAP_RESERVED;
    return 1;
}
int KrnMappingCommitRange(struct KrnMappingObject *m,uintptr_t address,
    size_t length,unsigned protection)
{
    if (!m || !m->virtual_reservation || m->shared || m->closing || !length ||
        !m->page_size || (address&(m->page_size-1)) || (length&(m->page_size-1)) ||
        address<m->start || address-m->start>=m->length ||
        length>m->length-(address-m->start) || (protection&~7U) ||
        ((protection&(KMAP_WRITE|KMAP_EXEC))==(KMAP_WRITE|KMAP_EXEC)) ||
        ((protection&(KMAP_WRITE|KMAP_EXEC)) && !(protection&KMAP_READ))) return 0;
    size_t first=(address-m->start)/m->page_size,count=length/m->page_size;
    for (size_t i=first;i<first+count;++i) {
        struct KrnMappingPage *p=&m->pages[i];
        if (p->state==KMAP_RESERVED && (p->physical || p->generation==UINT64_MAX)) return 0;
    }
    for (size_t i=first;i<first+count;++i) {
        struct KrnMappingPage *p=&m->pages[i];
        if (p->state==KMAP_RESERVED) { p->state=KMAP_ABSENT;p->protection=protection; }
    }
    return 1;
}
int KrnMappingDecommitPage(struct KrnMappingObject *m,size_t index,
    const struct KrnMappingOps *ops,uintptr_t *released)
{
    if (!m || !m->virtual_reservation || m->shared || m->closing || m->in_flight ||
        index>=m->page_count || !released) return 0;
    struct KrnMappingPage *p=&m->pages[index];
    if (p->state==KMAP_RESERVED) { *released=0;return 1; }
    if (p->generation==UINT64_MAX ||
        (p->state!=KMAP_ABSENT && p->state!=KMAP_CLEAN &&
         p->state!=KMAP_DIRTY && p->state!=KMAP_ERROR)) return 0;
    uintptr_t physical=p->physical;
    if (physical) {
        if (!m->resident || (p->state!=KMAP_CLEAN && p->state!=KMAP_DIRTY) ||
            !ops || !ops->remove ||
            !ops->remove(ops->context,m->start+index*m->page_size,physical)) return 0;
        --m->resident;
    }
    *p=(struct KrnMappingPage){0,p->generation+1,KMAP_RESERVED,0,p->protection};
    *released=physical;return 1;
}

struct KrnMappingObject *KrnMappingLookup(struct KrnMappingArena *a,
    uintptr_t address)
{
    if (!a || address < a->start || address-a->start >= a->length)
        return NULL;
    for (struct KrnMappingObject *m=a->first; m; m=m->next) {
        if (address < m->start)
            break;
        if (address-m->start < m->length)
            return m;
    }
    return NULL;
}

int KrnMappingRelease(struct KrnMappingArena *a, struct KrnMappingObject *m)
{
    if (!a || !m || !m->closing || m->resident || m->in_flight)
        return 0;
    struct KrnMappingObject **link=&a->first;
    while (*link && *link!=m)
        link=&(*link)->next;
    if (!*link)
        return 0;
    *link=m->next;
    m->next=NULL;
    return 1;
}

static size_t resident_pages(const struct KrnMappingPage *pages, size_t count)
{
    size_t resident=0;
    for (size_t i=0;i<count;++i)
        if (pages[i].physical && (pages[i].state==KMAP_CLEAN ||
                pages[i].state==KMAP_DIRTY || pages[i].state==KMAP_WRITEBACK))
            ++resident;
    return resident;
}

enum KrnMappingCutResult KrnMappingCut(struct KrnMappingArena *a,
    struct KrnMappingObject *m, uintptr_t address, size_t length,
    struct KrnMappingObject *right, struct KrnMappingPage *right_pages,
    size_t right_capacity)
{
    if (!a || !m || !m->closing || m->in_flight || !length ||
        !m->page_size || (address&(m->page_size-1)) ||
        (length&(m->page_size-1)) || address<m->start ||
        address-m->start>=m->length || length>m->length-(address-m->start))
        return KMAP_CUT_INVALID;
    struct KrnMappingObject **link=&a->first;
    while (*link && *link!=m) link=&(*link)->next;
    if (!*link) return KMAP_CUT_INVALID;
    size_t first=(address-m->start)/m->page_size;
    size_t after=first+length/m->page_size;
    size_t remaining=m->page_count-after;
    for (size_t i=first;i<after;++i)
        if (m->pages[i].physical || m->pages[i].state==KMAP_LOADING ||
            m->pages[i].state==KMAP_WRITEBACK)
            return KMAP_CUT_INVALID;
    uint64_t generation=0;
    for (size_t i=0;i<m->page_count;++i)
        if (m->pages[i].generation>generation) generation=m->pages[i].generation;
    if ((first || remaining) && generation==UINT64_MAX)
        return KMAP_CUT_INVALID;
    ++generation;
    if (first && remaining) {
        if (!right || !right_pages || right_capacity<remaining ||
            m->page_count>SIZE_MAX/sizeof(*m->pages) ||
            remaining>SIZE_MAX/sizeof(*right_pages))
            return KMAP_CUT_INVALID;
        for (struct KrnMappingObject *p=a->first;p;p=p->next)
            if (p==right) return KMAP_CUT_INVALID;
        uintptr_t old_start=(uintptr_t)m->pages, new_start=(uintptr_t)right_pages;
        size_t old_bytes=m->page_count*sizeof(*m->pages);
        size_t new_bytes=remaining*sizeof(*right_pages);
        if (old_bytes>UINTPTR_MAX-old_start || new_bytes>UINTPTR_MAX-new_start ||
            (new_start<old_start+old_bytes && old_start<new_start+new_bytes))
            return KMAP_CUT_INVALID;
        *right=*m;
        right->start=address+length;
        right->length=remaining*m->page_size;
        right->page_count=remaining;
        right->offset=m->offset+after*m->page_size;
        right->pages=right_pages;
        right->closing=0;
        for (size_t i=0;i<remaining;++i) {
            right_pages[i]=m->pages[after+i];
            right_pages[i].generation=generation;
        }
        right->resident=resident_pages(right_pages,remaining);
        m->next=right;
    }
    if (first) {
        m->length=first*m->page_size;
        m->page_count=first;
        m->closing=0;
        for (size_t i=0;i<first;++i) m->pages[i].generation=generation;
        m->resident=resident_pages(m->pages,first);
        return remaining ? KMAP_CUT_SPLIT : KMAP_CUT_TRIMMED;
    }
    if (remaining) {
        m->start=address+length;
        m->offset+=after*m->page_size;
        m->length=remaining*m->page_size;
        m->page_count=remaining;
        for (size_t i=0;i<remaining;++i) {
            m->pages[i]=m->pages[after+i];
            m->pages[i].generation=generation;
        }
        m->closing=0;
        m->resident=resident_pages(m->pages,remaining);
        return KMAP_CUT_TRIMMED;
    }
    *link=m->next;
    m->next=NULL;
    m->length=m->page_count=m->resident=0;
    return KMAP_CUT_RELEASED;
}

static void request(struct KrnMappingRequest *r, struct KrnMappingObject *m,
    size_t index)
{
    r->mapping = m;
    r->page = index;
    r->generation = m->pages[index].generation;
}

static struct KrnMappingPage *match(const struct KrnMappingRequest *r,
    unsigned state)
{
    if (!r || !r->mapping || r->page >= r->mapping->page_count)
        return NULL;
    struct KrnMappingPage *p = &r->mapping->pages[r->page];
    return p->state == state && p->generation == r->generation ? p : NULL;
}

enum KrnMappingFaultResult KrnMappingFault(struct KrnMappingObject *m,
    uintptr_t address, unsigned access, int may_block,
    struct KrnMappingRequest *r, const struct KrnMappingOps *ops)
{
    if (!m || address < m->start || address - m->start >= m->length)
        return KMAP_FAULT_OUTSIDE;
    if (!may_block)
        return KMAP_FAULT_FATAL;
    if (!r || !ops || !ops->install || m->closing || !access)
        return KMAP_FAULT_TRAP;
    size_t index = (address - m->start) / m->page_size;
    struct KrnMappingPage *p = &m->pages[index];
    if (access & ~p->protection)
        return KMAP_FAULT_TRAP;
    switch (p->state) {
    case KMAP_ABSENT:
        if (p->generation == UINT64_MAX)
            return KMAP_FAULT_TRAP;
        ++p->generation;
        p->state = KMAP_LOADING;
        ++m->in_flight;
        request(r, m, index);
        return KMAP_FAULT_QUEUE;
    case KMAP_LOADING:
    case KMAP_WRITEBACK:
        request(r, m, index);
        return KMAP_FAULT_WAIT;
    case KMAP_CLEAN:
        if (access & KMAP_WRITE) {
            /* Each private object owns its own physical page from page-in;
             * first store only grants write, never modifies its file. */
            if (!ops->install(ops->context, m->start + index * m->page_size,
                    p->physical, p->protection))
                return KMAP_FAULT_TRAP;
            p->state = KMAP_DIRTY;
        }
        return KMAP_FAULT_RETRY;
    case KMAP_DIRTY:
        if ((access & KMAP_WRITE) &&
            !ops->install(ops->context, m->start + index * m->page_size,
                p->physical, p->protection))
            return KMAP_FAULT_TRAP;
        return KMAP_FAULT_RETRY;
    default:
        return KMAP_FAULT_TRAP;
    }
}

int KrnMappingComplete(const struct KrnMappingRequest *r, uintptr_t physical,
    int error, const struct KrnMappingOps *ops)
{
    struct KrnMappingPage *p = match(r, KMAP_LOADING);
    if (!p)
        return 0;
    struct KrnMappingObject *m = r->mapping;
    /* Closing is authoritative even when page fill already finished. Drain
     * exactly this request without publishing a PTE or accepting its backing;
     * the pager still owns that temporary physical page and must release it.
     * No install callback is required for a cancelled completion. */
    if (m->closing) {
        p->state = KMAP_ABSENT;
        p->error = 0;
        --m->in_flight;
        return 0;
    }
    if (!ops || !ops->install)
        return 0;
    if (error || !physical || (physical & (m->page_size - 1)) ||
        !ops->install(ops->context, m->start + r->page * m->page_size,
            physical, p->protection & ~KMAP_WRITE)) {
        p->state = KMAP_ERROR;
        p->error = error ? error : -1;
        --m->in_flight;
        return 0;
    }
    p->physical = physical;
    p->state = KMAP_CLEAN;
    ++m->resident;
    --m->in_flight;
    return 1;
}

int KrnMappingBeginWriteback(struct KrnMappingObject *m, size_t index,
    struct KrnMappingRequest *r, const struct KrnMappingOps *ops)
{
    if (!m || index >= m->page_count || !r || !ops || !ops->install ||
        !m->shared)
        return 0;
    struct KrnMappingPage *p = &m->pages[index];
    if (p->state != KMAP_DIRTY || p->generation == UINT64_MAX ||
        !ops->install(ops->context, m->start + index * m->page_size,
            p->physical, p->protection & ~KMAP_WRITE))
        return 0;
    /* Only after all CPUs acknowledge RO may the pager checksum bytes. */
    p->state = KMAP_WRITEBACK;
    ++p->generation;
    ++m->in_flight;
    request(r, m, index);
    return 1;
}

int KrnMappingEndWriteback(const struct KrnMappingRequest *r, int error)
{
    struct KrnMappingPage *p = match(r, KMAP_WRITEBACK);
    if (!p)
        return 0;
    /* Keep RO after a failed write-back too. A later write fault must grant
     * write again; the dirty state alone cannot imply a writable PTE. */
    p->state = error ? KMAP_DIRTY : KMAP_CLEAN;
    p->error = error;
    --r->mapping->in_flight;
    return 1;
}

static int writeback_batch_valid(const struct KrnMappingRequest *requests,
    size_t count, unsigned state)
{
    if (count && !requests) return 0;
    uintptr_t previous=0;
    for (size_t i=0;i<count;++i) {
        const struct KrnMappingRequest *r=&requests[i];
        struct KrnMappingPage *p=match(r,state);
        if (!p || !r->mapping->shared || !p->physical ||
            (state==KMAP_DIRTY && p->generation==UINT64_MAX) ||
            (state==KMAP_WRITEBACK && !r->mapping->in_flight))
            return 0;
        uintptr_t address=r->mapping->start+r->page*r->mapping->page_size;
        if (i && address<=previous) return 0;
        previous=address;
    }
    return 1;
}

int KrnMappingBeginWritebackBatch(struct KrnMappingRequest *requests,size_t count,
    const struct KrnMappingOps *ops)
{
    if (!ops || !ops->install || !writeback_batch_valid(requests,count,KMAP_DIRTY))
        return 0;
    for (size_t i=0;i<count;++i) {
        struct KrnMappingRequest *r=&requests[i];
        if (!KrnMappingBeginWriteback(r->mapping,r->page,r,ops)) {
            /* No I/O started and no callback can reenter while the mapping
             * lock is held. Keep RO rather than require fallible rollback
             * PTE updates. Metadata becomes faultable DIRTY again. */
            for (size_t j=0;j<i;++j) {
                struct KrnMappingRequest *prior=&requests[j];
                prior->mapping->pages[prior->page].state=KMAP_DIRTY;
                --prior->mapping->in_flight;
            }
            return 0;
        }
    }
    return 1;
}

int KrnMappingEndWritebackBatch(const struct KrnMappingRequest *requests,
    size_t count,int error)
{
    if (!writeback_batch_valid(requests,count,KMAP_WRITEBACK)) return 0;
    for (size_t i=0;i<count;++i)
        (void)KrnMappingEndWriteback(&requests[i],error);
    return 1;
}

int KrnMappingProtectPage(struct KrnMappingObject *m, size_t index,
    unsigned protection, const struct KrnMappingOps *ops)
{
    if (!m || index>=m->page_count || m->closing || !ops || !ops->install ||
        (protection&~7U) || ((protection&(KMAP_WRITE|KMAP_EXEC))==
            (KMAP_WRITE|KMAP_EXEC)) ||
        ((protection&(KMAP_WRITE|KMAP_EXEC)) && !(protection&KMAP_READ)))
        return 0;
    struct KrnMappingPage *p=&m->pages[index];
    if (p->state==KMAP_RESERVED) return 0;
    unsigned actual=p->state==KMAP_DIRTY ? protection : protection&~KMAP_WRITE;
    if (p->physical && !ops->install(ops->context,m->start+index*m->page_size,
            p->physical,actual))
        return 0;
    p->protection=protection;
    return 1;
}

int KrnMappingBeginClose(struct KrnMappingObject *m)
{
    if (!m)
        return 0;
    m->closing = 1;
    return m->in_flight == 0;
}

uintptr_t KrnMappingReleasePage(struct KrnMappingObject *m, size_t index,
    const struct KrnMappingOps *ops)
{
    if (!m || !m->closing || m->in_flight || index >= m->page_count ||
        !ops || !ops->remove)
        return 0;
    struct KrnMappingPage *p = &m->pages[index];
    if ((p->state != KMAP_CLEAN && p->state != KMAP_DIRTY) ||
        (m->shared && p->state == KMAP_DIRTY) ||
        !ops->remove(ops->context, m->start + index * m->page_size, p->physical))
        return 0;
    uintptr_t physical = p->physical;
    p->physical = 0;
    p->state = KMAP_ABSENT;
    --m->resident;
    return physical;
}

int KrnMappingPopulate(struct KrnMappingObject *m,uintptr_t address,size_t length,
    int queue,size_t *resident,size_t *pending,int *page_error)
{
    if (!m || !m->page_size || (m->page_size&(m->page_size-1)) ||
        m->closing || !resident || !pending || !page_error || !length ||
        (address&(m->page_size-1)) || (length&(m->page_size-1)) ||
        address<m->start || address-m->start>=m->length ||
        length>m->length-(address-m->start)) return 0;
    size_t first=(address-m->start)/m->page_size,count=length/m->page_size;
    *resident=0; *pending=0; *page_error=0;
    /* A late invalid page must not leave earlier pages newly queued. */
    for (size_t i=first;i<first+count;++i) {
        struct KrnMappingPage *p=&m->pages[i];
        if (queue && (p->state==KMAP_ERROR || p->state==KMAP_RESERVED ||
            (p->state==KMAP_ABSENT && p->generation==UINT64_MAX))) {
            *page_error=p->error ? p->error : -1;
            return 0;
        }
    }
    for (size_t i=first;i<first+count;++i) {
        struct KrnMappingPage *p=&m->pages[i];
        if (queue && p->state==KMAP_ABSENT) {
            ++p->generation;
            p->state=KMAP_LOADING;
            ++m->in_flight;
        }
        if (p->physical) ++*resident;
        if (p->state==KMAP_LOADING || p->state==KMAP_WRITEBACK) ++*pending;
    }
    return 1;
}
