/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Experimental restricted-task integration in the shared AArch64 runtime.
 */
#include <aros/config.h>
#include <aros/debug.h>
#include <aros/kernel.h>
#include <aros/apple/startup.h>
#include <aros/apple/jit.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <proto/exec.h>
#include <string.h>
#include "restricted.h"
#include "kernel_intern.h"
#include "restricted_service.h"
#include "code_window.h"
#include "task_tls.h"
#include "kernel_cpu.h"
#include "kernel_intr.h"
#include "kernel_syscall.h"
#include "page_allocator.h"
#include "jit_protect.h"
#if defined(__AROSEXEC_SMP__)
#include "smp.h"
#include "cpu_monitor.h"
#endif
#include "../../aarch64-all/kernel/mapping.h"

#define R_PAGE 4096U
#define R_PREPARE 0xa531
#define R_CLAIM 0xa538
#define R_PUBLISH 0xa539
#define R_RELEASED 0xa53a
#define R_LIFETIME 0xa53b
#define R_SERVE 0xa53c
#define R_DOMAIN_CLAIM 0xa560
#define R_DOMAIN_PUBLISH 0xa561
#define R_DOMAIN_PREPARE 0xa562
#define R_DOMAIN_CLOSE 0xa563
#define R_DOMAIN_RELEASED 0xa564
#define R_DOMAIN_REAP 0xa565
#define R_DOMAIN_ABORT 0xa566
#define R_MEMORY 0xa567
#define R_SHARED 0xa568
#define R_TABLE_GROW 0xa569
#define R_WAKE 0xa56a
#define R_THREAD_STATE 0xa56b
#define R_SUSPEND 0xa56c
#define R_RESUME 0xa56d
#define R_DOMAIN_WORK 0xa56e
#define R_COPY 0xa56f
#define R_REAP_REFRESH 0xa570
#define R_DEBUG 0xa571
#define R_COOKIE UINT64_C(0x7265737472696374)
enum RestrictedState { R_FREE, R_ALLOCATING, R_LIVE, R_REAPING };
/* Address-space allocation lifetime is separate from an execution context.
 * The legacy run wrapper creates one domain; explicit attachment follows here,
 * rather than making a thread's native continuation own shared root storage. */
/* The page records and backing ledger come in chunks of chunk_pages pages
 * (R_CHUNK_PAGES at most), attached at the first commit into the chunk: a
 * reservation costs its chunk tables, never a record per reserved page. Each
 * attached chunk is one page-rounded allocation of chunk_bytes holding the
 * records, then the ledger. */
#define R_CHUNK_PAGES 512U
struct RestrictedAllocation {
    struct RestrictedAllocation *next;
    struct RestrictedAllocation **list_link;
    struct AROSRestrictedRegion region;
    /* Root owns chunk storage even after its own address view is detached.
     * views is coordinator-owned accounting; refs includes native frees still
     * in progress and reaches zero only after their backing is released. */
    struct RestrictedAllocation *storage;
    size_t storage_views,storage_refs;
    size_t count,storage_bytes,chunk_pages,chunk_count,chunk_bytes,chunks_attached;
    struct KrnMappingPage **pages;
    uintptr_t **backing;
};
static uintptr_t r_backing(const struct RestrictedAllocation *a,size_t page)
{
    if (page>=a->count) return 0;
    page+=a->region.mapping.chunk_offset;
    uintptr_t *chunk=a->backing[page/a->chunk_pages];
    return chunk ? chunk[page%a->chunk_pages] : 0;
}
struct RestrictedTableChunk {
    struct RestrictedTableChunk *next;
    void *pages;
    size_t count,used;
};
struct RestrictedDomain {
    struct RestrictedDomain *next;
    size_t storage_bytes;
    enum RestrictedState state;
    void *code, *data;
    struct RestrictedTableChunk *tables;
    size_t table_pages,table_used,table_metadata_pages,table_shortage;
    unsigned threads, managed, closing, high_window, wide_window;
    /* AROS_RESTRICTED_DEBUG_* chosen by the controller. */
    unsigned debug;
    void *owner_context, *releaser_context;
    uint64_t root;
    size_t code_bytes, memory_pages, memory_metadata_pages;
    struct AROSRestrictedSpace space;
    struct RestrictedAllocation *allocations;
};
struct RestrictedShared {
    struct RestrictedShared *next;
    struct AROSRestrictedSharedBacking backing;
    void *owner_context,*releaser_context;
    uint64_t handle;
    size_t count,storage_bytes;
    uintptr_t *pages;
};
static struct RestrictedShared *restricted_shared;
static uint64_t shared_generation;
static void r_shared_lifetime(struct AROSRestrictedLifetime *state)
{
    for (struct RestrictedShared *b=restricted_shared;b;b=b->next) {
        ++state->shared_objects;state->pages+=b->count+b->storage_bytes/R_PAGE;
        state->metadata_pages+=b->storage_bytes/R_PAGE;
    }
}
struct RestrictedCodeIdentity {
    uint64_t pc,generation,reservation;
    uintptr_t physical;
};
struct Restricted {
    struct Restricted *next;
    struct Restricted *index_next;
    size_t storage_bytes;
    struct RestrictedDomain *domain;
    /* The last fault retried as spurious, and how often in a row. */
    uint64_t spurious_far, spurious_pc;
    unsigned spurious_count;
    /* The last fault whose access was run again because the page was no
     * longer the repaired one, and how often in a row. */
    uint64_t fault_retry_far, fault_retry_pc;
    unsigned fault_retry_count;
    enum RestrictedState state;
    void *releaser_context;
    uint64_t cookie;
    struct Task *owner;
    void *owner_context;
    unsigned abandoned, snapshot_data;
    void *stack, *fault_stack, *tls;
    uint64_t stack_address, fault_address, tls_address;
    size_t stack_bytes, fault_bytes, address_span;
    void *native_lower, *native_upper;
    unsigned last_cpu;
    uint64_t seed;
    uint64_t wake_generation;
    uint32_t suspend_count;
    uint64_t wait_deadline;
    unsigned wait_pending,wait_result;
    uint64_t callback_pc;
    struct RestrictedCodeIdentity callback_entry,callback_return,fault_code,handler_code;
    unsigned callback_active, fault_active, exception_active;
    uint64_t fault_handler, fault_generation, fault_page_generation, fault_reservation;
    uint64_t exception_handler, exception_generation;
    struct RestrictedCodeIdentity exception_code;
    uintptr_t fault_physical;
    struct AROSAppleRuntimeFrame fault;
    struct AROSAppleRuntimeFrame callback;
    struct AROSAppleRuntimeFrame native;
    /* A thread served by its own task (run_served): where the task wants
     * the request, and while the task handles one, the thread's frame and
     * the request's address in the domain. */
    unsigned char *serve_buffer;
    unsigned call_active;
    /* The call in progress is a debug stop: no request in the domain. */
    unsigned debug_stop;
    uint64_t call_address;
    struct AROSAppleRuntimeFrame call;
    struct AROSRestrictedResult result;
    struct AROSRestrictedService service;
};
/* Every state transition and published metadata access uses the coordinator.
 * Native allocation/release has an exclusive slot claim, never that lock.
 * Forbid prevents remote terminal freeze during unpublished native allocation
 * or the release transaction; the page arena operations do not Wait. */
static struct Restricted *restricted_slots;
/* Updated under the coordinator, atomic fast-path observation by timer IRQs. */
static uint64_t timed_waiters;
static struct RestrictedDomain *restricted_domains;
struct RestrictedPublication {
    struct Restricted *slot;
    void *code, *data, *stack, *fault_stack, *tls;
    uint64_t seed;
    size_t code_bytes;
};
struct RestrictedDomainPublication {
    struct RestrictedDomain *domain;
    void *code,*data;
    size_t code_bytes;
};
static int r_prepare_domain(struct RestrictedDomain *,uint64_t);
static int r_unmap_thread(struct Restricted *);
static void r_cancel_waiters(struct RestrictedDomain *);
static struct RestrictedDomain *r_domain_slot(uintptr_t address)
{
    for (struct RestrictedDomain *d=restricted_domains;d;d=d->next)
        if ((uintptr_t)d==address) return d;
    return NULL;
}
static int r_domain_link(struct RestrictedDomain *d)
{
    if (!d || d->state!=R_FREE || d->next || !d->storage_bytes ||
        r_domain_slot((uintptr_t)d)) return 0;
    d->next=restricted_domains;restricted_domains=d;return 1;
}
static void r_domain_forget(struct RestrictedDomain *d)
{
    struct RestrictedDomain **link=&restricted_domains;
    while (*link && *link!=d) link=&(*link)->next;
    if (*link) *link=d->next;
    memset(d,0,sizeof(*d));
}

static struct RestrictedDomain *r_domain_lookup(uint64_t handle)
{
    if (!handle) return NULL;
    for (struct RestrictedDomain *d=restricted_domains;d;d=d->next) {
        if (d->managed && d->state==R_LIVE && d->space.handle==handle) return d;
    }
    return NULL;
}
static int r_domain_retire(struct RestrictedDomain *d,void *context)
{
    d->closing=1;AROSRestrictedSpaceStop(&d->space);
    r_cancel_waiters(d);
    if (d->threads) return 0;
    for (struct RestrictedAllocation *a=d->allocations;a;a=a->next)
        if (a->region.space && !AROSRestrictedRegionDetach(&a->region)) return 0;
    if (!AROSRestrictedSpaceClose(&d->space)) return 0;
    d->state=R_REAPING;d->releaser_context=context;return 1;
}
/* Native callers may skip cleanup only while no retired owner's records remain.
 * Refresh under the coordinator; an unclaimable lease keeps the hint set. */
static unsigned orphan_reap_pending;
static int r_orphan_work_pending(void)
{
    for (struct Restricted *r=restricted_slots;r;r=r->next)
        if (r->abandoned && (r->state==R_LIVE || r->state==R_REAPING)) return 1;
    for (struct RestrictedDomain *d=restricted_domains;d;d=d->next)
        if (!d->owner_context && d->managed &&
            (d->state==R_LIVE || d->state==R_REAPING)) return 1;
    for (struct RestrictedShared *b=restricted_shared;b;b=b->next)
        if (!b->owner_context) return 1;
    return 0;
}
static int r_refresh_reap_hint(void)
{
    int pending=r_orphan_work_pending();
    __atomic_store_n(&orphan_reap_pending,(unsigned)pending,__ATOMIC_RELEASE);
    return pending;
}
static uint64_t owners_retired, owners_reaped;
static void r_reap(void);
static void r_mark_retired_resources(void *retired);
static void r_coordinate(void);
static void r_uncoordinate(void);
/* Every trap from a domain and every dispatch into one asks whether the
 * task's record still names a linked slot. With one slot per thread and a
 * few hundred threads, walking the list for that answer was a quarter of a
 * CPU's time. The index holds exactly the slots of the list. */
#define R_SLOT_INDEX 512U
static struct Restricted *slot_index[R_SLOT_INDEX];
static struct Restricted **r_slot_bucket(uintptr_t address)
{ return &slot_index[(address/R_PAGE)%R_SLOT_INDEX]; }
static void r_slot_link(struct Restricted *r)
{
    struct Restricted **link=&restricted_slots;
    while (*link) link=&(*link)->next;
    *link=r;
    r->index_next=*r_slot_bucket((uintptr_t)r);*r_slot_bucket((uintptr_t)r)=r;
}
static void r_slot_unlink(struct Restricted *r)
{
    struct Restricted **link=&restricted_slots;
    while (*link && *link!=r) link=&(*link)->next;
    if (*link) *link=r->next;
    for (link=r_slot_bucket((uintptr_t)r);*link && *link!=r;) link=&(*link)->index_next;
    if (*link) *link=r->index_next;
}
static struct Restricted *r_slot(uintptr_t address)
{
    for (struct Restricted *r=*r_slot_bucket(address);r;r=r->index_next)
        if ((uintptr_t)r==address) return r;
    return NULL;
}
/* A task whose execution record names no live restricted thread it owns
 * carries stale or foreign state: never dispatch it into a domain root.
 * Counted and kept for the debugger; the record is cleared. */
volatile uint64_t AROSAppleStaleExecutions, AROSAppleStaleTask, AROSAppleStaleRoot, AROSAppleStaleDomain;
int AROSAppleRestrictedExecutionCheck(struct Task *task, struct AROSAppleTaskExecution *execution)
{
    struct Restricted *r = execution->domain;
    if (!execution->root) return 1;
    if (r && r_slot((uintptr_t)r) == r && r->cookie == R_COOKIE && r->owner == task) return 1;
    ++AROSAppleStaleExecutions;
    AROSAppleStaleTask = (uintptr_t)task;
    AROSAppleStaleRoot = execution->root;
    AROSAppleStaleDomain = (uintptr_t)r;
    *execution = (struct AROSAppleTaskExecution){0};
    return 0;
}
static size_t r_code_storage(size_t bytes)
{
    /* The native protection entry accepts a 32-bit byte length. Round only
     * after checking both that limit and the dedicated image VA window. */
    if (!bytes || bytes>UINT32_MAX-(R_PAGE-1) ||
        bytes>AROS_RESTRICTED_CODE_END-AROS_RESTRICTED_CODE) return 0;
    return (bytes+R_PAGE-1)&~(size_t)(R_PAGE-1);
}
static int r_thread_addresses(struct Restricted *r,struct RestrictedDomain *domain)
{
    /* Include the guards between stacks/TLS and after TLS. Round the owned
     * interval to the allocation granule; retain it through release ACK. */
    const size_t limit=AROS_RESTRICTED_THREAD_END-AROS_RESTRICTED_STACK;
    if (!r->stack_bytes || !r->fault_bytes ||
        ((r->stack_bytes|r->fault_bytes)&(R_PAGE-1)) ||
        r->stack_bytes>limit || r->fault_bytes>limit-r->stack_bytes ||
        r->stack_bytes+r->fault_bytes>limit-4*R_PAGE) return 0;
    size_t span=r->stack_bytes+r->fault_bytes+4*R_PAGE;
    if (span>limit-(AROS_RESTRICTED_THREAD_STRIDE-1)) return 0;
    span=(span+AROS_RESTRICTED_THREAD_STRIDE-1)&~(AROS_RESTRICTED_THREAD_STRIDE-1);
    uint64_t offset=0;
    for (;;) {
        if (offset>limit-span) return 0;
        uint64_t base=AROS_RESTRICTED_STACK+offset,next=offset;
        for (struct Restricted *p=restricted_slots;p;p=p->next) {
            if (p->domain==domain && base<p->stack_address+p->address_span &&
                p->stack_address<base+span) {
                next=p->stack_address+p->address_span-AROS_RESTRICTED_STACK;break;
            }
        }
        if (next==offset) break;
        offset=next;
    }
    r->stack_address=AROS_RESTRICTED_STACK+offset;
    r->fault_address=r->stack_address+r->stack_bytes+R_PAGE;
    r->tls_address=r->fault_address+r->fault_bytes+R_PAGE;
    r->address_span=span;return 1;
}
static void r_thread_forget(struct Restricted *r)
{
    r_slot_unlink(r);
    memset(r,0,sizeof(*r));
}
/* Native-only SVCs, called with local scheduling excluded. The exclusive
 * ALLOCATING/REAPING states reserve storage while allocator work runs outside
 * the coordinator and its mapping/scheduler lock order. */
static int r_lifecycle(struct AROSAppleRuntimeFrame *frame,struct Task *task,
    struct ExceptionContext *context,unsigned svc)
{
    if (svc==R_CLAIM) {
        uint64_t handle=frame->x[0];frame->x[0]=0;
        struct RestrictedDomain *domain=r_domain_lookup(handle);
        if (handle && (!domain || domain->closing)) return 1;
        if (!handle) domain=(void *)(uintptr_t)frame->x[1];
        struct Restricted *r=(void *)(uintptr_t)frame->x[2];
        if (!domain || !r || r->state!=R_FREE || r->next || !r->storage_bytes ||
            r_slot((uintptr_t)r) || domain->threads==(unsigned)-1 || !r_thread_addresses(r,domain)) return 1;
        if (!handle) {
            if (!r_domain_link(domain)) return 1;
            domain->state=R_ALLOCATING;
        }
        ++domain->threads;r->domain=domain;
        r->state=R_ALLOCATING;r->owner=task;r->owner_context=context;
        r_slot_link(r);frame->x[0]=(uintptr_t)r;return 1;
    }
    if (svc==R_PUBLISH) {
        const struct RestrictedPublication *p=(void *)(uintptr_t)frame->x[0];
        struct Restricted *r=p ? r_slot((uintptr_t)p->slot) : NULL;
        frame->x[0]=0;
        if (!r || r->state!=R_ALLOCATING || r->owner!=task || r->owner_context!=context) return 1;
        if (r->domain->managed) {
            if (p->code || p->data || p->code_bytes) return 1;
        } else {
            r->domain->code=p->code;r->domain->data=p->data;
            r->domain->code_bytes=p->code_bytes;r->domain->state=R_LIVE;
        }
        r->stack=p->stack;r->fault_stack=p->fault_stack;r->tls=p->tls;
        r->seed=p->seed;r->cookie=R_COOKIE;
        r->state=R_LIVE;frame->x[0]=1;
        return 1;
    }
    if (svc==R_RELEASED) {
        struct Restricted *r=r_slot(frame->x[0]);
        frame->x[0]=0;
        if (!r || r->state!=R_REAPING || r->releaser_context!=context) return 1;
        if (!r->domain || !r->domain->threads) return 1;
        if (r->domain->managed) {
            if (r->domain->state!=R_LIVE) return 1;
            --r->domain->threads;
        } else {
            if (r->domain->state!=R_REAPING || r->domain->threads!=1) return 1;
            r_domain_forget(r->domain);
        }
        if (r->abandoned) ++owners_reaped;
        r_thread_forget(r);frame->x[0]=1;
        return 1;
    }
    if (svc==R_LIFETIME) {
        struct AROSRestrictedLifetime *state=(void *)(uintptr_t)frame->x[0];
        frame->x[0]=0;if (!state) return 1;
        *state=(struct AROSRestrictedLifetime){.retired=owners_retired,.reaped=owners_reaped};
        state->timed_waits=__atomic_load_n(&timed_waiters,__ATOMIC_RELAXED);
        for (struct Restricted *r=restricted_slots;r;r=r->next) {
            if (r->state==R_FREE) continue;
            ++state->live;state->pending+=r->abandoned!=0;
            state->waits+=r->wait_pending!=0;state->suspended_threads+=r->suspend_count!=0;
            /* ALLOCATING counts as live even before its backing is published.
             * REAPING retains its accounting until the release acknowledgement. */
            state->pages+=(r->stack ? r->stack_bytes/R_PAGE : 0)+(r->fault_stack ? r->fault_bytes/R_PAGE : 0)+(r->tls!=NULL)+r->storage_bytes/R_PAGE;
            state->metadata_pages+=r->storage_bytes/R_PAGE;
        }
        for (struct RestrictedDomain *d=restricted_domains;d;d=d->next) {
            if (d->state!=R_FREE) {
                ++state->domains;state->table_pages+=d->table_pages;
                state->pages+=d->table_pages+d->table_metadata_pages+(d->code ? r_code_storage(d->code_bytes)/R_PAGE : 0)+(d->data!=NULL)+d->memory_pages+d->storage_bytes/R_PAGE;
                state->metadata_pages+=d->storage_bytes/R_PAGE+d->memory_metadata_pages+d->table_metadata_pages;
            }
        }
        r_shared_lifetime(state);
        frame->x[0]=1;return 1;
    }
    return 0;
}
static int r_close(struct Restricted *r)
{
    if (!r->domain || (!r->domain->managed && r->domain->threads!=1)) return 0;
    if (!r->domain->managed) AROSRestrictedSpaceStop(&r->domain->space);
    if (!AROSRestrictedServiceClose(&r->service) || !r_unmap_thread(r)) return 0;
    if (!r->domain->managed) {
        if (!AROSRestrictedSpaceClose(&r->domain->space)) return 0;
        r->domain->state=R_REAPING;
    }
    /* Under the coordinator: close prevents new attachment and every other
     * context/worker has acknowledged release when this is the last member.
     * Publish permission for the exclusive native cleanup phase to copy data. */
    r->snapshot_data=!r->domain->managed || (r->domain->closing && r->domain->threads==1);
    return 1;
}
static struct Restricted *r_claim_reap(struct ExceptionContext *context)
{
    for (struct Restricted *owned=restricted_slots;owned;owned=owned->next) {
        if (owned->state==R_LIVE && owned->abandoned && r_close(owned)) {
            owned->state=R_REAPING;owned->releaser_context=context;
            return owned;
        }
    }
    return NULL;
}
static int r_code_pc(const struct Restricted *r,uint64_t pc)
{
    return pc>=AROS_RESTRICTED_CODE && !(pc&3) && r->domain->code_bytes>=4 &&
        pc-AROS_RESTRICTED_CODE<=r->domain->code_bytes-4;
}
/* Captured under the coordinator; never authorize a saved continuation from
 * VA equality alone. Re-publication advances generation even at the same PA. */
static int r_capture_code(const struct Restricted *r,uint64_t pc,struct RestrictedCodeIdentity *identity)
{
    if (!identity || !r || !r->domain || r->domain->closing || (pc&3)) return 0;
    struct RestrictedCodeIdentity found={.pc=pc};
    if (r_code_pc(r,pc)) {
        found.physical=(uintptr_t)r->domain->code+((pc-AROS_RESTRICTED_CODE)&~(uint64_t)(R_PAGE-1));
        found.reservation=r->domain->space.handle;
        if (!r->domain->code || !found.reservation) return 0;
    } else if (!AROSRestrictedSpacePageIdentity(&r->domain->space,pc,KMAP_EXEC,
        &found.physical,&found.generation,&found.reservation)) return 0;
    *identity=found;return 1;
}
static int r_match_code(const struct Restricted *r,const struct RestrictedCodeIdentity *identity)
{
    struct RestrictedCodeIdentity current;
    return identity && r_capture_code(r,identity->pc,&current) &&
        current.physical==identity->physical && current.generation==identity->generation &&
        current.reservation==identity->reservation;
}
/* The normal stack, or any owned writable page directly below SP: a guest
 * may switch to stacks it owns (an emulator stack, a fiber), and the kernel
 * admits them for preemption, restoration and exception delivery alike. */
static int r_owned_sp(const struct Restricted *r, uint64_t sp)
{
    uintptr_t physical;uint64_t generation,reservation;
    if (!sp || (sp&15)) return 0;
    if (sp>r->stack_address && sp<=r->stack_address+r->stack_bytes) return 1;
    uintptr_t page=(sp-1)&~(uint64_t)(R_PAGE-1);
    if (AROSRestrictedSpacePageIdentity(&r->domain->space,page,KMAP_WRITE,
        &physical,&generation,&reservation)) return 1;
    /* A stack growing into an owned reserved page that a demand-commit
     * repair will back: every access there faults first. */
    return AROSRestrictedSpaceFaultIdentity(&r->domain->space,page,
        &physical,&generation,&reservation) && !physical;
}
static int r_stack_sp(const struct Restricted *r, uint64_t sp)
{
    if (r->fault_active || r->exception_active)
        return sp>r->fault_address && sp<=r->fault_address+r->fault_bytes && !(sp&15);
    return r_owned_sp(r,sp);
}
/* Coordinator held throughout admission, copy, validation and application.
 * Read only kernel-owned physical aliases, never an untrusted virtual pointer.
 * The fixed record spans at most two pages; preflight both before any copy. */
static int r_read_context(struct Restricted *r,uint64_t address,
    struct AROSRestrictedContext *context)
{
    if (!address || (address&7) || address>UINT64_MAX-sizeof(*context)) return 0;
    struct AROSRestrictedSpace *space=&r->domain->space;
    uintptr_t aliases[2];size_t lengths[2],count=0,left=sizeof(*context);
    uint64_t cursor=address;
    while (left) {
        uintptr_t page=cursor&~(uint64_t)(R_PAGE-1),physical;
        uint64_t generation,reservation,actual;
        size_t offset=cursor&(R_PAGE-1),bytes=R_PAGE-offset;
        if (count==2) return 0;
        if (bytes>left) bytes=left;
        if (page>=r->stack_address && page-r->stack_address<r->stack_bytes) {
            physical=(uintptr_t)r->stack+(page-r->stack_address);
            if (!AArch64UserPagePhysical(&space->hardware,space->root,space->tcr,
                page,KMAP_READ,1,&actual) || actual!=physical) return 0;
        } else if (!AROSRestrictedSpacePageIdentity(space,page,KMAP_READ,
            &physical,&generation,&reservation)) return 0;
        aliases[count]=physical+offset;lengths[count++]=bytes;
        cursor+=bytes;left-=bytes;
    }
    unsigned char *output=(unsigned char *)context;
    for (size_t i=0;i<count;++i) {
        memcpy(output,(const void *)aliases[i],lengths[i]);output+=lengths[i];
    }
    return 1;
}
static int r_restore_context(struct Restricted *r,struct AROSAppleRuntimeFrame *frame)
{
    struct AROSRestrictedContext context;
    struct RestrictedCodeIdentity code;
    if (frame->x[1]!=sizeof(context) || r->domain->closing ||
        r->callback_active || r->callback_pc || r->fault_active ||
        r->wait_pending || r->service.request.queue ||
        !r_read_context(r,frame->x[0],&context) ||
        (context.nzcv&~UINT64_C(0xf0000000)) ||
        context.fpcr>UINT32_MAX || context.fpsr>UINT32_MAX ||
        !r_owned_sp(r,context.sp) || !r_capture_code(r,context.pc,&code)) return 0;
    memcpy(frame->x,context.x,sizeof(frame->x));
    frame->sp=context.sp;frame->elr=context.pc;
    frame->spsr=(frame->spsr&~UINT64_C(0xf0000000))|context.nzcv;
    memcpy(frame->simd,context.simd,sizeof(context.simd));
    frame->simd[64]=context.fpcr;frame->simd[65]=context.fpsr;
    return 1;
}
static void r_stack_bounds(struct Restricted *r, int fault)
{
    uintptr_t base=fault ? r->fault_address : r->stack_address;
    r->owner->tc_SPLower=(void *)base;
    r->owner->tc_SPUpper=(void *)(base+(fault ? r->fault_bytes : r->stack_bytes));
}
/* Exec's scheduler alerts on a saved SP outside tc_SPLower/tc_SPUpper. Keep
 * those bounds on the admitted stack: the normal stack, or the owned page
 * directly below an SP that switched elsewhere. Called after r_stack_sp. */
static void r_track_stack(struct Restricted *r, uint64_t sp)
{
    if (r->fault_active || r->exception_active) return;
    if (sp>r->stack_address && sp<=r->stack_address+r->stack_bytes) { r_stack_bounds(r,0);return; }
    uintptr_t base=(sp-1)&~(uint64_t)(R_PAGE-1);
    r->owner->tc_SPLower=(void *)base;
    r->owner->tc_SPUpper=(void *)(base+R_PAGE);
}
/* Synchronous exception classes a registered guest handler may receive. The
 * SVC class and asynchronous entries never reach this test; other classes
 * (trapped system registers, external aborts, debug steps) end the domain. */
/* Linux emulates EL0 reads of the CPU identification registers (op0 3,
 * op1 0, CRn 0) and advertises it with HWCAP_CPUID; restricted code reads
 * the values this kernel recorded at boot. Others in that space read zero. */
static int r_emulate_id_read(struct AROSAppleRuntimeFrame *frame)
{
    unsigned iss=(unsigned)frame->esr&0x1ffffffU;
    unsigned op0=(iss>>20)&3,op2=(iss>>17)&7,op1=(iss>>14)&7,crn=(iss>>10)&15,rt=(iss>>5)&31,crm=(iss>>1)&15;
    const struct AROSAppleCPUIDRegisters *ids=NULL;
    intptr_t value=0;
    uint64_t v=0;
    if (!(iss&1) || op0!=3 || op1!=0 || crn!=0) return 0;
    if (AROSAppleKernelIdentity(AROS_APPLE_KATTR_CPU_ID_REGISTERS,&value)) ids=(const void *)value;
    if (ids) switch ((crm<<3)|op2) {
    case 0x00: v=ids->midr;break;
    case 0x20: v=ids->pfr0;break;
    case 0x21: v=ids->pfr1;break;
    case 0x24: v=ids->zfr0;break;
    case 0x30: v=ids->isar0;break;
    case 0x31: v=ids->isar1;break;
    case 0x32: v=ids->isar2;break;
    case 0x38: v=ids->mmfr0;break;
    case 0x39: v=ids->mmfr1;break;
    case 0x3a: v=ids->mmfr2;break;
    }
    if (rt!=31) frame->x[rt]=v;
    frame->elr+=4;
    return 1;
}
static int r_deliverable(unsigned ec)
{
    return ec==0x00 || ec==0x20 || ec==0x21 || ec==0x22 || ec==0x24 || ec==0x25 ||
        ec==0x26 || ec==0x2c || ec==0x3c;
}
/* Coordinator held. Writes the faulting context through the kernel alias of the
 * fault stack and enters the registered handler there. Returns 0 when the
 * exception must end the domain instead. */
static int r_deliver_exception(struct Restricted *r,struct AROSAppleRuntimeFrame *frame)
{
    struct AROSRestrictedException *record;
    unsigned ec=(unsigned)(frame->esr>>26)&63;
    if (!r_deliverable(ec) || !r->exception_handler || r->exception_active ||
        r->fault_active || r->callback_active || r->wait_pending ||
        r->service.request.queue || !r->fault_stack ||
        r->fault_bytes<sizeof(*record)+1024 || r->exception_generation==UINT64_MAX ||
        !r_match_code(r,&r->exception_code)) return 0;
    record=(struct AROSRestrictedException *)((unsigned char *)r->fault_stack+r->fault_bytes-sizeof(*record));
    memset(record,0,sizeof(*record));
    memcpy(record->context.x,frame->x,sizeof(record->context.x));
    record->context.sp=frame->sp;record->context.pc=frame->elr;
    record->context.nzcv=frame->spsr&UINT64_C(0xf0000000);
    memcpy(record->context.simd,frame->simd,sizeof(record->context.simd));
    record->context.fpcr=frame->simd[64];record->context.fpsr=frame->simd[65];
    record->esr=frame->esr;record->far=frame->far;
    record->generation=++r->exception_generation;
    r->exception_active=1;++r->result.exceptions;
    frame->elr=r->exception_handler;
    frame->x[0]=r->fault_address+r->fault_bytes-sizeof(*record);
    frame->x[1]=record->esr;frame->x[2]=record->far;frame->x[3]=record->generation;
    frame->x[30]=0;frame->sp=frame->x[0];r_stack_bounds(r,1);
    return 1;
}
/* Coordinator held. A debugged domain's exception stops its thread for the
 * controller: the thread leaves the domain as for CALL, with the event in
 * its served buffer and its frame kept in the slot for R_SERVE. Returns 0
 * when the exception takes its ordinary course. */
static int r_debug_stop(struct Restricted *r,struct AROSAppleTaskExecution *execution,
    struct AROSAppleRuntimeFrame *frame)
{
    unsigned ec=(unsigned)(frame->esr>>26)&63;
    unsigned wanted=(ec==0x3c || ec==0x30 || ec==0x32 || ec==0x34) ?
        AROS_RESTRICTED_DEBUG_BREAK : AROS_RESTRICTED_DEBUG_FAULTS;
    uint64_t event[4]={AROS_RESTRICTED_DEBUG_EVENT,frame->esr,frame->far,frame->elr};
    if (!(r->domain->debug&wanted) || !r->serve_buffer || r->call_active || r->callback_active ||
        r->callback_pc || r->service.request.queue || r->wait_pending) return 0;
    memset(r->serve_buffer,0,256);memcpy(r->serve_buffer,event,sizeof(event));
    r->call=*frame;r->call_address=0;r->call_active=1;r->debug_stop=1;
    r->call.spsr&=~UINT64_C(0x200000);
    r->owner->tc_SPLower=r->native_lower;
    r->owner->tc_SPUpper=r->native_upper;
    r->owner->tc_SPReg=(void *)(uintptr_t)r->native.sp;
    *frame=r->native;
    frame->x[0]=4;
    *execution=(struct AROSAppleTaskExecution){0};
    execution->domain=r;
    return 1;
}
/* Coordinator held; the caller owns the domain. */
static int r_debug(struct RestrictedDomain *d,uint64_t channel,unsigned op,uint64_t argument)
{
    if (d->closing) return 0;
    if (op==1) { d->debug=(unsigned)argument&(AROS_RESTRICTED_DEBUG_BREAK|AROS_RESTRICTED_DEBUG_FAULTS);return 1; }
    if (!channel || op>4) return 0;
    for (struct Restricted *r=restricted_slots;r;r=r->next) {
        if (r->domain!=d || r->state!=R_LIVE || r->service.channel!=channel ||
            !r->owner || !r->owner_context) continue;
        struct AROSRestrictedDebugContext *c=(void *)(uintptr_t)argument;
        struct ExceptionContext *saved=r->owner_context;
        struct AROSAppleRuntimeFrame *f=r->call_active ? &r->call : NULL;
        if (!f && !(r->suspend_count && AROSAppleRestrictedParked(r->owner,saved))) return 0;
        if (op==4) {
            if (f) f->spsr=argument ? f->spsr|UINT64_C(0x200000) : f->spsr&~UINT64_C(0x200000);
            else saved->cpsr=argument ? saved->cpsr|0x200000U : saved->cpsr&~0x200000U;
            return 1;
        }
        if (!c) return 0;
        uint64_t *simd=f ? f->simd : (uint64_t *)saved->fpuContext;
        if (!simd) return 0;
        if (op==2) {
            memset(c,0,sizeof(*c));
            if (f) {
                memcpy(c->context.x,f->x,sizeof(c->context.x));
                c->context.sp=f->sp;c->context.pc=f->elr;c->pstate=f->spsr;
                c->tls=f->task_tls;
                if (r->debug_stop) { c->esr=f->esr;c->far=f->far; }
            } else {
                memcpy(c->context.x,saved->x,sizeof(saved->x));
                c->context.x[29]=saved->fp;c->context.x[30]=saved->lr;
                c->context.sp=saved->sp;c->context.pc=saved->pc;c->pstate=saved->cpsr;
                c->tls=AROSAppleTaskExecution(saved)->user_tls;
            }
            c->context.nzcv=c->pstate&UINT64_C(0xf0000000);
            memcpy(c->context.simd,simd,sizeof(c->context.simd));
            c->context.fpcr=simd[64];c->context.fpsr=simd[65];
            c->state=(f ? AROS_RESTRICTED_DEBUG_IN_HANDLER : 0)|(r->debug_stop ? AROS_RESTRICTED_DEBUG_STOPPED : 0);
            return 1;
        }
        if (f) {
            memcpy(f->x,c->context.x,sizeof(c->context.x));
            f->sp=c->context.sp;f->elr=c->context.pc;
            f->spsr=(f->spsr&~UINT64_C(0xf0000000))|(c->context.nzcv&UINT64_C(0xf0000000));
        } else {
            memcpy(saved->x,c->context.x,sizeof(saved->x));
            saved->fp=c->context.x[29];saved->lr=c->context.x[30];
            saved->sp=c->context.sp;saved->pc=c->context.pc;
            saved->cpsr=(saved->cpsr&~0xf0000000U)|((uint32_t)c->context.nzcv&0xf0000000U);
        }
        memcpy(simd,c->context.simd,sizeof(c->context.simd));
        simd[64]=c->context.fpcr;simd[65]=c->context.fpsr;
        return 1;
    }
    return 0;
}
/* A JIT-class page: owned, current, writable and executable in the ledger. */
static int r_jit_page(const struct Restricted *r,uint64_t address)
{
    uintptr_t physical;uint64_t generation,reservation;
    return AROSRestrictedSpacePageIdentity(&r->domain->space,address,KMAP_WRITE,
            &physical,&generation,&reservation) &&
        AROSRestrictedSpacePageIdentity(&r->domain->space,address,KMAP_EXEC,
            &physical,&generation,&reservation);
}
/* SPRR JIT mode (M1): JIT pages are r-x, or rw- while the task writes.
 * Windows translators never toggle, so the switch follows the faulting
 * access: a write to a JIT page switches the thread to writing, a fetch
 * from one switches it back. An instruction in a JIT page that writes a JIT
 * page cannot be satisfied either way; it is delivered as a fault. */
static int r_jit_switch(struct Restricted *r,struct Task *task,
    struct ExceptionContext *context,struct AROSAppleRuntimeFrame *frame,
    unsigned ec,unsigned fault)
{
    if (AROSAppleJitMode()!=AROS_APPLE_JIT_SPRR || (fault&0x3c)!=0x0c) return 0;
    int writing=(context->Flags&ECF_JITWRITE)!=0;
    if (ec==0x24 && !writing && (frame->esr&(UINT64_C(1)<<6)) &&
        !(frame->esr&(UINT64_C(1)<<10)) && r_jit_page(r,frame->far) &&
        !r_jit_page(r,frame->elr))
        return AROSAppleJitSetWritable(task,1)!=UINT64_MAX;
    if (ec==0x20 && writing && r_jit_page(r,frame->elr))
        return AROSAppleJitSetWritable(task,0)!=UINT64_MAX;
    return 0;
}
/* A translation or permission fault the page tables no longer explain: the
 * leaf grants the access by the time the fault is examined. Another CPU has
 * just installed the page or raised its permissions, and this CPU faulted on
 * the earlier state, which a TLB entry that caused a permission fault may
 * keep. Drop this CPU's entries and run the instruction again. A fault that
 * recurs at the same place is not spurious and takes the ordinary path. */
#define R_SPURIOUS_LIMIT 8
#define R_FAULT_RETRY_LIMIT 8
static int r_spurious_fault(struct Restricted *r,struct AROSAppleRuntimeFrame *frame,
    unsigned ec,unsigned fault)
{
    if ((ec!=0x20 && ec!=0x24) || ((fault&0x3c)!=0x04 && (fault&0x3c)!=0x0c)) return 0;
    /* Cache maintenance and external aborts are never retried. */
    if (ec==0x24 && (frame->esr&((UINT64_C(1)<<8)|(UINT64_C(1)<<10)))) return 0;
    unsigned access=ec==0x20 ? KMAP_EXEC : (frame->esr&(UINT64_C(1)<<6)) ? KMAP_WRITE : KMAP_READ;
    uintptr_t physical;uint64_t generation,reservation;
    /* SPRR decides JIT pages per thread, not in the leaf. */
    if ((AROSAppleJitMode()==AROS_APPLE_JIT_SPRR && r_jit_page(r,frame->far)) ||
        !AROSRestrictedSpacePageIdentity(&r->domain->space,frame->far,access,
            &physical,&generation,&reservation)) {
        r->spurious_count=0;
        return 0;
    }
    if (r->spurious_far!=frame->far || r->spurious_pc!=frame->elr) {
        r->spurious_far=frame->far;r->spurious_pc=frame->elr;r->spurious_count=0;
    }
    if (++r->spurious_count>R_SPURIOUS_LIMIT) return 0;
    __asm__ volatile("tlbi vmalle1; dsb nsh; isb":::"memory");
    return 1;
}
static struct ExceptionContext *r_context(struct Task *task)
{
    return task && (task->tc_Flags & TF_ETASK) && task->tc_UnionETask.tc_ETask ?
        task->tc_UnionETask.tc_ETask->et_RegFrame : NULL;
}
static uint64_t r_wait_microseconds(uint64_t ticks,uint64_t hz)
{
    if (!hz || hz>UINT32_MAX || ticks/hz>UINT64_MAX/1000000U) return UINT64_MAX;
    uint64_t base=(ticks/hz)*1000000U,part=(ticks%hz)*1000000U/hz;
    return part>UINT64_MAX-base ? UINT64_MAX : base+part;
}
static uint64_t r_wait_clock(void)
{
    uint64_t ticks,hz;
    __asm__ volatile("isb; mrs %0,cntvct_el0; mrs %1,cntfrq_el0":"=r"(ticks),"=r"(hz));
    return r_wait_microseconds(ticks,hz);
}
/* Coordinator held. Only a saved/admitted WAIT owns a pending return value.
 * Wake/expiry clear WAIT, preserving SUSPEND; whichever completes first owns
 * the result until save/dispatch delivers it once. A late wake still advances
 * the notification generation, but cannot turn an expired wait into success. */
static void r_clear_deadline(struct Restricted *r)
{
    if (r->wait_deadline) {
        r->wait_deadline=0;
        __atomic_sub_fetch(&timed_waiters,1,__ATOMIC_RELEASE);
    }
}
static void r_complete_wait(struct Restricted *r,unsigned result)
{
    if (r->wait_pending && !r->wait_result) {
        r->wait_result=result;r_clear_deadline(r);
    }
}
static void r_clear_wait(struct Restricted *r)
{
    r_clear_deadline(r);r->wait_pending=r->wait_result=0;
}
static void r_deliver_wait(struct Restricted *r,struct AROSAppleRuntimeFrame *frame)
{
    if (r->wait_pending && r->wait_result) {
        frame->x[0]=r->wait_result;
        r_clear_wait(r);
    }
}
static void r_expire_waits(uint64_t now)
{
    for (struct Restricted *r=restricted_slots;r;r=r->next) {
        if (r->state!=R_LIVE || !r->owner || !r->owner_context || r->domain->closing ||
            !r->wait_pending || r->wait_result || !r->wait_deadline || now<r->wait_deadline) continue;
        if (AROSAppleRestrictedSetWait(r->owner,r->owner_context,0))
            r_complete_wait(r,AROS_RESTRICTED_WAIT_EXPIRED);
    }
}
void AROSAppleRestrictedExpireWaits(void)
{
    /* Called from masked timer IRQ/FIQ after handlers release their locks,
     * before scheduler exit. Never allocate, run payload code or acquire a
     * page-map lock here. The coordinator orders expiry against wake/close. */
    if (!__atomic_load_n(&timed_waiters,__ATOMIC_ACQUIRE)) return;
    r_coordinate();r_expire_waits(r_wait_clock());r_uncoordinate();
}
static void r_cancel_waiters(struct RestrictedDomain *domain)
{
    for (struct Restricted *r=restricted_slots;r;r=r->next)
        if (r->domain==domain && r->state==R_LIVE && r->owner && r->owner_context) {
            r->suspend_count=0;r_clear_wait(r);
            AROSAppleRestrictedCancelPark(r->owner,r->owner_context);
        }
}
static int r_wake_channel(struct RestrictedDomain *domain,uint64_t channel)
{
    if (!domain || domain->closing || !channel) return 0;
    for (struct Restricted *r=restricted_slots;r;r=r->next) {
        if (r->domain!=domain || r->state!=R_LIVE || r->service.channel!=channel ||
            !r->owner || !r->owner_context || r->wake_generation==UINT64_MAX) continue;
        if (!AROSAppleRestrictedSetWait(r->owner,r->owner_context,0)) return 0;
        unsigned result=r->wait_deadline && r_wait_clock()>=r->wait_deadline ?
            AROS_RESTRICTED_WAIT_EXPIRED : AROS_RESTRICTED_WAIT_WOKEN;
        r_complete_wait(r,result);
        ++r->wake_generation;return 1;
    }
    return 0;
}
static int64_t r_suspend_channel(struct RestrictedDomain *domain,uint64_t channel,int resume)
{
    if (!domain || domain->closing || !channel) return -1;
    for (struct Restricted *r=restricted_slots;r;r=r->next) {
        if (r->domain!=domain || r->state!=R_LIVE || r->service.channel!=channel ||
            !r->owner || !r->owner_context) continue;
        uint32_t prior=r->suspend_count;
        if (!resume && prior==UINT32_MAX) return -1;
        /* A thread inside its own handler runs no domain code and has no
         * restricted context to park: the count alone keeps it out. */
        if (!r->call_active && !AROSAppleRestrictedSetSuspend(r->owner,r->owner_context,
            !resume || prior>1)) return -1;
        r->suspend_count=resume ? (prior ? prior-1 : 0) : prior+1;
        return prior;
    }
    return -1;
}
static struct Task *r_current(void)
{
#if defined(__AROSEXEC_SMP__)
    return TLS_GET(ThisTask);
#else
    return SysBase ? SysBase->ThisTask : NULL;
#endif
}
static uint64_t *r_resolve(void *unused, uint64_t pa)
{ (void)unused; return (void *)(uintptr_t)pa; }
static uint64_t r_allocate(void *opaque, unsigned bits)
{
    struct RestrictedDomain *domain = opaque;
    if (bits != 12) return 0;
    for (struct RestrictedTableChunk *c=domain->tables;c;c=c->next) {
        if (c->used<c->count) {
            ++domain->table_used;
            return (uintptr_t)c->pages+R_PAGE*c->used++;
        }
    }
    return 0;
}
static void r_clean(void *unused, const void *address, size_t bytes)
{
    uint64_t ctr;
    (void)unused;
    __asm__ volatile("mrs %0,ctr_el0":"=r"(ctr));
    uintptr_t line = 4U << ((ctr >> 16) & 15);
    for (uintptr_t p=(uintptr_t)address & ~(line-1); p<(uintptr_t)address+bytes; p+=line)
        __asm__ volatile("dc cvac,%0"::"r"(p):"memory");
    __asm__ volatile("dsb ish":::"memory");
}
/* A change to one leaf of a domain's tables, and to the table levels above
 * it that the change created, concerns the translations of that one page:
 * invalidate those, on every CPU and for every ASID. Emptying every TLB for
 * each committed page made both CPUs walk the tables again for everything
 * they touched, thousands of times a second, and in a virtual machine each
 * of those walks is a two-stage one. Anything else (a domain being
 * prepared, inherited blocks being split) still empties the TLBs. */
static void r_invalidate(void *context)
{
    struct RestrictedDomain *d=context;
    uintptr_t page=d ? d->space.flush_address : 0;
    if (page) {
        uint64_t argument=((uint64_t)page>>12)&UINT64_C(0xfffffffffff);
        __asm__ volatile("dsb ishst; tlbi vaae1is,%0; dsb ish; isb"::"r"(argument):"memory");
    } else __asm__ volatile("dsb ish; tlbi vmalle1is; dsb ish; isb":::"memory");
}
static const void *r_execution_alias(struct RestrictedDomain *d,uint64_t va,size_t bytes)
{
    if (!bytes) return NULL;
    if (va>=AROS_RESTRICTED_CODE && va<AROS_RESTRICTED_CODE_END) {
        size_t storage=r_code_storage(d->code_bytes);
        if (!d->code || va-AROS_RESTRICTED_CODE>=storage ||
            bytes>storage-(va-AROS_RESTRICTED_CODE)) return NULL;
        return (const unsigned char *)d->code+(va-AROS_RESTRICTED_CODE);
    }
    for (struct RestrictedAllocation *a=d->allocations;a;a=a->next) {
        if (va<a->region.mapping.start || va-a->region.mapping.start>=a->region.mapping.length) continue;
        size_t offset=va-a->region.mapping.start,page=offset/R_PAGE,in_page=offset%R_PAGE;
        uintptr_t backing=r_backing(a,page);
        const struct KrnMappingPage *p=KrnMappingPageAt(&a->region.mapping,page);
        if (page>=a->count || bytes>R_PAGE-in_page ||
            !backing || !p || p->physical!=backing ||
            (a->region.shared && (a->region.shared_first+page>=a->region.shared->count ||
             a->region.shared->pages[a->region.shared_first+page]!=backing))) return NULL;
        return (const unsigned char *)backing+in_page;
    }
    return NULL;
}
static void r_sync(void *opaque, uint64_t va, size_t bytes)
{
    /* Clean owned native backing, never the inactive user VA. Dynamic pages
     * arrive with native aliases already RO/NX and remote CPUs quiesced. */
    const void *alias=r_execution_alias(opaque,va,bytes);
    if (!alias) for (;;) __asm__ volatile("wfe");
    r_clean(opaque,alias,bytes);
    __asm__ volatile("ic ialluis; dsb ish; isb":::"memory");
}
/* Source tables are frozen by the native caller's mapping lock. Preflight
 * precedes any clone mutation. Native code supplies missing pages outside all
 * mapping/coordinator locks, then retries against the current source root. */
static int r_tables_need(struct RestrictedDomain *d,size_t count)
{
    size_t available=d->table_pages-d->table_used;
    d->table_shortage=count>available ? count-available : 0;
    return !d->table_shortage;
}
/* Upper bound on new table pages for a range in a four-level 4K root.
 * Existing shared ancestors only reduce this count. No allocation occurs in
 * the mapping critical section, including when a stack crosses a table edge. */
static size_t r_range_tables(uint64_t address,size_t bytes)
{
    size_t count=0;
    for (unsigned shift=21;shift<=39;shift+=9)
        count+=((address+bytes-1)>>shift)-(address>>shift)+1;
    return count;
}
/* Remove inherited privileged identity aliases only from the private clone.
 * Data and stacks can overlap native RAM just as low Windows addresses do. */
static int r_prepare_private_page(const struct AArch64ProtectionOps *ops,
    uint64_t root,uint64_t tcr,uintptr_t address)
{
    uint64_t physical;
    if (!AROSAppleKernelTransitionRangeAvailable(address,R_PAGE)) return 0;
    if (!AArch64MappingPhysical(ops,root,tcr,address,&physical)) {
        if (!AArch64TranslateResident(ops,root,tcr,address,&physical)) return 1;
        if (!AArch64PreparePrivilegedPage(ops,root,tcr,address,physical)) return 0;
    }
    return AArch64UnmapPrivilegedPage(ops,root,tcr,address,physical);
}
static int r_prepare_domain(struct RestrictedDomain *d,uint64_t source)
{
    uint64_t tcr,root=0;
    struct AArch64ProtectionOps ops={d,r_resolve,r_allocate,r_clean,r_invalidate,r_sync,0};
    __asm__ volatile("mrs %0,tcr_el1":"=r"(tcr));
    size_t count,storage=r_code_storage(d->code_bytes);
    size_t additional=storage ? r_range_tables(AROS_RESTRICTED_CODE,storage)+3 : 0;
    d->table_shortage=0;
    if (d->root || !d->code || !d->data || !storage || ((tcr>>14)&3) ||
        !AArch64CountPrivilegedRoot(&ops,source,tcr,SIZE_MAX,&count) || count>SIZE_MAX-additional ||
        !r_tables_need(d,count+additional) ||
        !AArch64ClonePrivilegedRoot(&ops,source,tcr,count,&root)) return 0;
    d->root=root;
    for (size_t offset=0;offset<storage;offset+=R_PAGE)
        if (!AArch64MapPage(&ops,root,tcr,AROS_RESTRICTED_CODE+offset,(uintptr_t)d->code+offset,
            AARCH64_PROTECT_READ|AARCH64_PROTECT_EXEC|AARCH64_PROTECT_USER,1)) return 0;
    /* API 34: the high window exists only where the kernel template maps
     * nothing (above RAM on QEMU and on M1 machines up to 32 GB). */
    d->high_window=AArch64RangeUnmapped(&ops,root,tcr,AROS_RESTRICTED_HIGH_BASE,
        AROS_RESTRICTED_HIGH_END-AROS_RESTRICTED_HIGH_BASE);
    d->wide_window=AArch64RangeUnmapped(&ops,root,tcr,AROS_RESTRICTED_WIDE_BASE,
        AROS_RESTRICTED_WIDE_END-AROS_RESTRICTED_WIDE_BASE);
    if (!r_prepare_private_page(&ops,root,tcr,AROS_RESTRICTED_DATA)) return 0;
    return AROSRestrictedSpaceInit(&d->space,(uintptr_t)d,root,tcr,(uintptr_t)d->data,&ops);
}
static int r_prepare(struct Restricted *r,uint64_t source)
{
    struct RestrictedDomain *d=r->domain;
    d->table_shortage=0;
    if (d->closing || (!d->managed && !d->root && !r_prepare_domain(d,source)) ||
        !d->space.handle || !r_tables_need(d,r_range_tables(r->stack_address,r->stack_bytes)+
        r_range_tables(r->fault_address,r->fault_bytes)+3)) return 0;
    unsigned rw=AARCH64_PROTECT_READ|AARCH64_PROTECT_WRITE|AARCH64_PROTECT_USER;
    uintptr_t va[]={r->stack_address,r->fault_address,r->tls_address};
    uintptr_t pa[]={(uintptr_t)r->stack,(uintptr_t)r->fault_stack,(uintptr_t)r->tls};
    size_t bytes[]={r->stack_bytes,r->fault_bytes,R_PAGE};
    for (unsigned i=0;i<3;++i)
        for (size_t offset=0;offset<bytes[i];offset+=R_PAGE)
            if (!r_prepare_private_page(&d->space.hardware,d->root,d->space.tcr,va[i]+offset) ||
                !AArch64MapPage(&d->space.hardware,d->root,d->space.tcr,
                    va[i]+offset,pa[i]+offset,rw,1)) return 0;
    return AROSRestrictedServiceInit(&r->service,&d->space,(uintptr_t)r->owner);
}
static int r_unmap_thread(struct Restricted *r)
{
    struct RestrictedDomain *d=r->domain;
    if (!d->space.root) return 1; /* Domain construction failed before maps. */
    uintptr_t va[]={r->stack_address,r->fault_address,r->tls_address};
    uintptr_t pa[]={(uintptr_t)r->stack,(uintptr_t)r->fault_stack,(uintptr_t)r->tls};
    size_t bytes[]={r->stack_bytes,r->fault_bytes,R_PAGE};
    for (unsigned i=0;i<3;++i) {
        for (size_t offset=0;offset<bytes[i];offset+=R_PAGE) {
            uint64_t actual;
            if (!AArch64MappingPhysical(&d->space.hardware,d->root,d->space.tcr,va[i]+offset,&actual)) continue;
            if (actual!=pa[i]+offset || !AArch64UnmapPage(&d->space.hardware,
                d->root,d->space.tcr,va[i]+offset,pa[i]+offset)) return 0;
        }
    }
    return 1;
}
/* Trusted native packet only; restricted execution never reaches this dispatcher.
 * The controller's Forbid transaction protects its unpublished allocation and
 * native release, while the coordinator protects domain lookup and PTE changes. */
struct RestrictedMemoryOperation {
    uint64_t handle;
    uintptr_t address, physical;
    unsigned operation, protection;
    struct RestrictedAllocation *allocation,*slice;
    size_t length;
    /* A commit into a chunk without records: the handler asks for chunk_bytes
     * of zeroed storage (chunk_needed), the caller supplies it in chunk and
     * retries; the handler takes it (chunk becomes NULL) or leaves it. */
    void *chunk;
    size_t chunk_bytes;
    unsigned chunk_needed;
};
/* Set only by the native publication wrapper while PageMapLock and the
 * remote rendezvous are held. Scalar payload SVCs cannot set this identity. */
static void *publication_context;
/* The task whose trap is being handled, for checks that must name the task
 * itself and not the controller it acts for. Coordinator held. */
static void *trap_context;
static int r_publish_native(void *context,uintptr_t physical,unsigned protection)
{ (void)context;return AROSApplePageProtectArenaLocked(physical,protection); }
static const struct AROSRestrictedPublicationOps publication_ops={NULL,r_publish_native};
/* Evict inherited identity leaves in both low and dynamic windows, in this
 * domain's private table clone. The dynamic window can overlap physical RAM.
 * Owned pages (including guards) are handled by RegionCommit itself. No native
 * physical backing is released here; the trusted identity map remains intact. */
static int r_prepare_low_page(struct RestrictedDomain *d,
    struct RestrictedAllocation *a, size_t page, uintptr_t address)
{
    if (address >= AROS_RESTRICTED_DYNAMIC_END || r_backing(a,page)) return 1;
    return r_prepare_private_page(&d->space.hardware,d->root,d->space.tcr,address);
}
struct RestrictedCopyOperation {
    uint64_t handle;
    uintptr_t address;
    void *buffer;
    size_t length;
    unsigned access;
};
/* API 36: the owner's copy into or out of its live domain's memory. */
static int r_copy(struct AROSAppleRuntimeFrame *frame,void *context,unsigned svc)
{
    if (svc!=R_COPY) return 0;
    struct RestrictedCopyOperation *p=(void *)(uintptr_t)frame->x[0];
    frame->x[0]=0;if (!p) return 1;
    struct RestrictedDomain *d=r_domain_lookup(p->handle);
    if (!d || d->closing || d->state!=R_LIVE || d->owner_context!=context) return 1;
    frame->x[0]=AROSRestrictedSpaceCopy(&d->space,p->address,p->buffer,p->length,p->access);
    return 1;
}
/* Only an authorized COW transition may renew pending write repairs.
 * Releasing/remapping, stale generations and read faults stay invalid. */
static void r_renew_private_faults(struct RestrictedDomain *d,uintptr_t address,
    uintptr_t old_physical,uint64_t old_generation,uint64_t reservation,
    const struct KrnMappingPage *replacement)
{
    for (struct Restricted *r=restricted_slots;r;r=r->next)
        if (r->domain==d && r->fault_active && (r->fault.esr&64) &&
            (r->fault.far&~(uintptr_t)(R_PAGE-1))==address &&
            r->fault_reservation==reservation && r->fault_physical==old_physical &&
            r->fault_page_generation==old_generation) {
            r->fault_physical=replacement->physical;
            r->fault_page_generation=replacement->generation;
        }
}
/* The domain arena indexes exactly its bootstrap and its allocation mappings.
 * Use that index for page operations; the allocation list is for teardown. */
static struct RestrictedAllocation *r_allocation_at(struct RestrictedDomain *d,uintptr_t address)
{
    struct KrnMappingObject *m=KrnMappingLookup(&d->space.arena,address);
    if (!m || m==&d->space.mapping) return NULL;
    struct RestrictedAllocation *a=(void *)((unsigned char *)m-
        offsetof(struct RestrictedAllocation,region)-offsetof(struct AROSRestrictedRegion,mapping));
    return a->region.space==&d->space ? a : NULL;
}
/* The address index identifies the allocation before removal. Preserve its
 * incoming list link so retiring N pages does not walk N live records N times.
 * Both list updates run under the domain coordinator. */
static void r_allocation_link(struct RestrictedDomain *d,struct RestrictedAllocation *a)
{
    a->next=d->allocations;a->list_link=&d->allocations;
    if (a->next) a->next->list_link=&a->next;
    d->allocations=a;
}
static int r_allocation_unlink(struct RestrictedAllocation *a)
{
    struct RestrictedAllocation **link=a->list_link;
    if (!link || *link!=a) return 0;
    *link=a->next;
    if (a->next) a->next->list_link=link;
    a->next=NULL;a->list_link=NULL;return 1;
}
/* Sharing publishes only descriptor ownership, never another physical alias.
 * The caller has prepared right's region and allocated its own descriptor. */
static void r_storage_share(struct RestrictedAllocation *a,struct RestrictedAllocation *right)
{
    struct RestrictedAllocation *root=a->storage;
    right->storage=root;right->count=right->region.mapping.page_count;
    right->chunk_pages=root->chunk_pages;right->chunk_count=root->chunk_count;
    right->chunk_bytes=root->chunk_bytes;right->pages=root->pages;right->backing=root->backing;
    ++root->storage_views;
    __atomic_add_fetch(&root->storage_refs,1,__ATOMIC_RELAXED);
}
/* Metadata stops being charged to the domain at the last linked view; its
 * storage may stay pinned until native backing frees have all finished. */
static size_t r_storage_retire(struct RestrictedAllocation *a)
{
    struct RestrictedAllocation *root=a->storage;
    size_t metadata=a!=root ? a->storage_bytes/R_PAGE : 0;
    if (!--root->storage_views)
        metadata+=root->storage_bytes/R_PAGE+root->chunks_attached*(root->chunk_bytes/R_PAGE);
    return metadata;
}
static int r_memory(struct AROSAppleRuntimeFrame *frame,void *context,unsigned svc)
{
    if (svc!=R_MEMORY) return 0;
    struct RestrictedMemoryOperation *p=(void *)(uintptr_t)frame->x[0];
    frame->x[0]=0;frame->x[1]=0;if (!p) return 1;
    struct RestrictedDomain *d=r_domain_lookup(p->handle);
    if (!d || d->closing || d->owner_context!=context) return 1;
    if (p->operation==0) {
        struct RestrictedAllocation *a=p->allocation;
        if (a && (p->address < AROS_RESTRICTED_HIGH_BASE ||
            (p->address < AROS_RESTRICTED_WIDE_BASE ? d->high_window : d->wide_window)) &&
            (p->address >= AROS_RESTRICTED_DYNAMIC_END ||
            AROSAppleKernelTransitionRangeAvailable(p->address,a->count*R_PAGE)) &&
            AROSRestrictedRegionReserve(&d->space,&a->region,p->address,
            a->count*R_PAGE,a->pages,a->backing,a->chunk_pages,a->count)) {
            r_allocation_link(d,a);d->memory_pages+=a->storage_bytes/R_PAGE;
            d->memory_metadata_pages+=a->storage_bytes/R_PAGE;frame->x[0]=1;
        }
        return 1;
    }
    struct RestrictedAllocation *a=r_allocation_at(d,p->address);
    if (!a || (p->address&(R_PAGE-1))) return 1;
    size_t page=(p->address-a->region.mapping.start)/R_PAGE;
    if (p->operation==7) {
        struct RestrictedAllocation *right=p->slice,*root=a->storage;
        if (!right || !a->list_link || *a->list_link!=a || !root ||
            root->storage_views==SIZE_MAX ||
            __atomic_load_n(&root->storage_refs,__ATOMIC_RELAXED)==SIZE_MAX) return 1;
        enum KrnMappingCutResult cut=AROSRestrictedRegionCut(&a->region,
            p->address,p->length,&right->region);
        if (cut==KMAP_CUT_INVALID) return 1;
        a->count=a->region.mapping.page_count;
        if (cut==KMAP_CUT_SPLIT) {
            r_storage_share(a,right);r_allocation_link(d,right);p->slice=NULL;
            d->memory_pages+=right->storage_bytes/R_PAGE;
            d->memory_metadata_pages+=right->storage_bytes/R_PAGE;
        } else if (cut==KMAP_CUT_RELEASED) {
            size_t metadata=r_storage_retire(a);
            d->memory_pages-=metadata;d->memory_metadata_pages-=metadata;
            r_allocation_unlink(a);p->allocation=a;
        }
        frame->x[0]=1;return 1;
    }
    if ((p->operation==1 || p->operation==5) && !r_tables_need(d,3)) {
        frame->x[1]=d->table_shortage;return 1;
    }
    if ((p->operation==1 || p->operation==5) && AROSRestrictedRegionChunkMissing(&a->region,page)) {
        if (!p->chunk) { p->chunk_bytes=a->chunk_bytes;p->chunk_needed=1;return 1; }
        struct KrnMappingPage *records=p->chunk;
        if (!AROSRestrictedRegionAttachChunk(&a->region,page,records,
            (uintptr_t *)(records+a->chunk_pages))) return 1;
        p->chunk=NULL;++a->storage->chunks_attached;
        d->memory_pages+=a->chunk_bytes/R_PAGE;d->memory_metadata_pages+=a->chunk_bytes/R_PAGE;
    }
    if ((p->operation==1 || p->operation==5) &&
        !r_prepare_low_page(d,a,page,p->address)) return 1;
    if (p->operation==1) {
        frame->x[0]=AROSRestrictedRegionCommit(&a->region,page,p->physical,p->protection);
        d->memory_pages+=frame->x[0]!=0;
    } else if (p->operation==2) {
        frame->x[0]=AROSRestrictedRegionDecommit(&a->region,page,&p->physical);
        if (frame->x[0] && p->physical) --d->memory_pages;
    } else if (p->operation==3) {
        const struct KrnMappingPage *record=KrnMappingPageAt(&a->region.mapping,page);
        if ((p->protection&KMAP_EXEC) || a->region.publication_pending ||
            (record && (record->protection&KMAP_EXEC))) {
            if (publication_context && publication_context==trap_context)
                frame->x[0]=AROSRestrictedRegionPublish(&a->region,page,p->protection,&publication_ops);
        } else frame->x[0]=AROSRestrictedRegionProtect(&a->region,page,p->protection);
    } else if (p->operation==5)
        frame->x[0]=AROSRestrictedRegionCommitShared(&a->region,page,p->protection);
    else if (p->operation==6 && page==0 && a->count==1 &&
        d->state==R_LIVE) {
        uintptr_t old_physical=0;uint64_t old_generation=0,reservation=0;
        if (!AROSRestrictedSpacePageIdentity(&d->space,p->address,KMAP_READ,
            &old_physical,&old_generation,&reservation)) return 1;
        if (!AROSRestrictedRegionPrivatize(&a->region,p->physical)) return 1;
        ++d->memory_pages;
        const struct KrnMappingPage *replacement=KrnMappingPageAt(&a->region.mapping,0);
        r_renew_private_faults(d,p->address,old_physical,old_generation,reservation,replacement);
        frame->x[0]=1;
    }
    else if (p->operation==4 && p->address==a->region.mapping.start) {
        if (!a->list_link || *a->list_link!=a ||
            !AROSRestrictedRegionDetach(&a->region)) return 1;
        for (size_t i=0;i<a->count;++i) d->memory_pages-=r_backing(a,i)!=0;
        size_t metadata=r_storage_retire(a);
        d->memory_pages-=metadata;d->memory_metadata_pages-=metadata;
        r_allocation_unlink(a);p->allocation=a;frame->x[0]=1;
    }
    return 1;
}
struct RestrictedSharedOperation {
    unsigned operation;
    uint64_t handle,domain;
    uintptr_t address;
    struct RestrictedShared *record;
    size_t offset,length;
    void *buffer;
};
static int r_shared_operation(struct AROSAppleRuntimeFrame *frame,void *context,unsigned svc)
{
    if (svc!=R_SHARED) return 0;
    struct RestrictedSharedOperation *p=(void *)(uintptr_t)frame->x[0];
    frame->x[0]=0;if (!p) return 1;
    if (p->operation==0) {
        struct RestrictedShared *b=p->record;
        if (!b || shared_generation==UINT64_MAX ||
            !AROSRestrictedSharedInitialize(&b->backing,b->pages,b->count)) return 1;
        b->handle=++shared_generation;b->owner_context=context;
        b->next=restricted_shared;restricted_shared=b;frame->x[0]=b->handle;return 1;
    }
    struct RestrictedShared **link=&restricted_shared;
    if (p->operation==4) {
        while (*link && ((*link)->owner_context || (*link)->releaser_context || (*link)->backing.views)) link=&(*link)->next;
    } else if (p->operation==11) {
        /* A retired controller's native view, for the reaper to unmap */
        while (*link && ((*link)->owner_context || (*link)->releaser_context ||
                         !(*link)->backing.native_view || (*link)->backing.native_pending)) link=&(*link)->next;
    } else {
        while (*link && (*link)->handle!=p->handle) link=&(*link)->next;
    }
    struct RestrictedShared *b=*link;
    if (!b) return 1;
    if (p->operation==9 && !b->owner_context && !b->releaser_context) {
        /* The reaper ends a retired controller's view */
        frame->x[0]=AROSRestrictedSharedViewAbort(&b->backing);return 1;
    }
    if (p->operation==11) {
        frame->x[0]=AROSRestrictedSharedViewEnd(&b->backing,&p->address);
        p->record=b;p->handle=b->handle;return 1;
    }
    if (p->operation==3) {
        if (b->releaser_context==context && b->backing.pages==NULL) {
            *link=b->next;frame->x[0]=1;
        }
        return 1;
    }
    if (b->releaser_context || (p->operation!=4 && b->owner_context!=context)) return 1;
    if (p->operation>=7 && p->operation<=10) {
        /* API 35 native view steps; the mapping itself is the caller's */
        if (p->operation==7) frame->x[0]=AROSRestrictedSharedViewBegin(&b->backing);
        else if (p->operation==8) frame->x[0]=AROSRestrictedSharedViewCommit(&b->backing,p->address);
        else if (p->operation==9) frame->x[0]=AROSRestrictedSharedViewAbort(&b->backing);
        else frame->x[0]=AROSRestrictedSharedViewEnd(&b->backing,&p->address);
        p->record=b;
        return 1;
    }
    if (p->operation==5 || p->operation==6) {
        frame->x[0]=p->operation==5 ?
            AROSRestrictedSharedWrite(&b->backing,p->offset,p->buffer,p->length) :
            AROSRestrictedSharedRead(&b->backing,p->offset,p->buffer,p->length);
    } else if (p->operation==1 || p->operation==12) {
        struct RestrictedDomain *d=r_domain_lookup(p->domain);
        if (!d || d->closing || d->owner_context!=context) return 1;
        for (struct RestrictedAllocation *a=d->allocations;a;a=a->next)
            if (a->region.mapping.start==p->address) {
                frame->x[0]=p->operation==1 ? AROSRestrictedRegionAttachShared(&a->region,&b->backing) :
                    AROSRestrictedRegionAttachSharedAt(&a->region,&b->backing,p->offset);
                break;
            }
    } else if ((p->operation==2 || p->operation==4) && AROSRestrictedSharedClose(&b->backing)) {
        b->releaser_context=context;p->record=b;frame->x[0]=1;
    }
    return 1;
}
static int r_domain_lifecycle(struct AROSAppleRuntimeFrame *frame,void *context,unsigned svc)
{
    if (svc<R_DOMAIN_CLAIM || svc>R_DOMAIN_ABORT) return 0;
    uintptr_t argument=frame->x[0];frame->x[0]=0;frame->x[1]=0;
    if (svc==R_DOMAIN_CLAIM) {
        struct RestrictedDomain *d=(void *)argument;
        if (!r_domain_link(d)) return 1;
        d->state=R_ALLOCATING;d->managed=1;d->owner_context=context;
        frame->x[0]=(uintptr_t)d;return 1;
    }
    if (svc==R_DOMAIN_PUBLISH) {
        const struct RestrictedDomainPublication *p=(void *)argument;
        struct RestrictedDomain *d=p ? r_domain_slot((uintptr_t)p->domain) : NULL;
        if (!d || !d->managed || d->state!=R_ALLOCATING || d->owner_context!=context) return 1;
        d->code=p->code;d->data=p->data;d->code_bytes=p->code_bytes;
        d->state=R_LIVE;frame->x[0]=1;return 1;
    }
    if (svc==R_DOMAIN_REAP) {
        for (struct RestrictedDomain *d=restricted_domains;d;d=d->next) {
            if (d->managed && d->state==R_LIVE && !d->owner_context &&
                r_domain_retire(d,context)) { frame->x[0]=(uintptr_t)d;break; }
        }
        return 1;
    }
    struct RestrictedDomain *d=svc==R_DOMAIN_CLOSE ? r_domain_lookup(argument) : r_domain_slot(argument);
    if (!d || !d->managed) { frame->x[1]=UINT64_MAX;return 1; }
    if (svc==R_DOMAIN_RELEASED) {
        if (d->state!=R_REAPING || d->releaser_context!=context || d->threads) return 1;
        r_domain_forget(d);frame->x[0]=1;return 1;
    }
    if (d->state!=R_LIVE || d->owner_context!=context) { frame->x[1]=UINT64_MAX;return 1; }
    if (svc==R_DOMAIN_PREPARE) {
        d->table_shortage=0;
        if (!d->closing && frame->entry_ttbr0==AROSAppleRuntimeLocal()->trusted_ttbr0 &&
            r_prepare_domain(d,frame->entry_ttbr0)) frame->x[0]=d->space.handle;
        else if (!d->closing) frame->x[1]=d->table_shortage;
    } else if (svc==R_DOMAIN_CLOSE || svc==R_DOMAIN_ABORT) {
        if (r_domain_retire(d,context)) { frame->x[0]=(uintptr_t)d;frame->x[1]=1; }
    }
    return 1;
}
struct RestrictedTableTransfer {
    uint64_t handle;
    struct RestrictedDomain *domain;
    struct Restricted *thread;
    struct RestrictedTableChunk *chunk;
};
static int r_table_transfer(struct AROSAppleRuntimeFrame *frame,void *context,unsigned svc)
{
    if (svc!=R_TABLE_GROW) return 0;
    struct RestrictedTableTransfer *p=(void *)(uintptr_t)frame->x[0];frame->x[0]=0;
    if (!p) return 1;
    struct RestrictedDomain *d=NULL;
    if (p->thread) {
        struct Restricted *r=r_slot((uintptr_t)p->thread);
        if (r && r->state==R_LIVE && r->owner_context==context) d=r->domain;
    } else {
        d=p->handle ? r_domain_lookup(p->handle) : r_domain_slot((uintptr_t)p->domain);
        if (d && (!d->managed || d->owner_context!=context)) d=NULL;
    }
    struct RestrictedTableChunk *c=p->chunk;
    if (!d || d->state!=R_LIVE || d->closing || !c || c->next || c->used ||
        !c->pages || ((uintptr_t)c->pages&(R_PAGE-1)) || !c->count || c->count>SIZE_MAX/R_PAGE ||
        c->count*R_PAGE>UINTPTR_MAX-(uintptr_t)c->pages ||
        c->count>SIZE_MAX-d->table_pages || d->table_metadata_pages==SIZE_MAX) return 1;
    for (struct RestrictedTableChunk *old=d->tables;old;old=old->next)
        if (old==c || ((uintptr_t)c->pages<(uintptr_t)old->pages+old->count*R_PAGE &&
            (uintptr_t)old->pages<(uintptr_t)c->pages+c->count*R_PAGE)) return 1;
    c->next=d->tables;d->tables=c;d->table_pages+=c->count;++d->table_metadata_pages;
    frame->x[0]=1;return 1;
}
static void r_finish(struct Restricted *r, struct AROSAppleTaskExecution *execution,
    struct AROSAppleRuntimeFrame *frame, int64_t status)
{
    r->callback_active=0;r->fault_active=0;r->exception_active=0;r->suspend_count=0;
    r->call_active=0;
    r_clear_wait(r);
    r->result.status = status;
    r->result.value = *(uint64_t *)r->domain->data;
    r->result.esr = frame->esr;
    r->result.far = frame->far;
    r->result.pc = frame->elr;
    r->owner->tc_SPLower = r->native_lower;
    r->owner->tc_SPUpper = r->native_upper;
    r->owner->tc_SPReg = (void *)(uintptr_t)r->native.sp;
    *frame = r->native;
    frame->x[0] = 1;
    *execution = (struct AROSAppleTaskExecution){0};
    /* Return uses the saved native root/TLS. No table/backing is freed until
     * that eret completes and the native API wrapper owns execution again. */
}
void AROSAppleRestrictedCheckStack(struct AROSAppleRuntimeFrame *frame,
    struct Task *task)
{
    struct ExceptionContext *context = r_context(task);
    if (!context || (frame->spsr & 31)) return;
    struct AROSAppleTaskExecution *execution = AROSAppleTaskExecution(context);
    if (!execution->root) return;
    struct Restricted *r = execution->domain;
    if (!r || r->cookie != R_COOKIE || r->owner != task)
        for (;;) __asm__ volatile("wfe");
    /* Restricted tasks cannot hold native Wait locks here. The coordinator
     * serializes a controller's close with preemption of CPU-bound payloads. */
    r_coordinate();
    if (r->domain->closing) r_finish(r,execution,frame,-5);
    else if (!r_stack_sp(r,frame->sp)) r_finish(r,execution,frame,-3);
    else { r_track_stack(r,frame->sp);r_deliver_wait(r,frame); }
    r_uncoordinate();
}
static int r_trap_locked(struct AROSAppleRuntimeFrame *frame)
{
    if (!frame || frame->vector != 8 || (frame->spsr & 31)) return 0;
    struct Task *task = r_current();
    struct ExceptionContext *context = r_context(task);
    if (!context) return 0;
    struct AROSAppleTaskExecution *execution = AROSAppleTaskExecution(context);
    struct Restricted *r = execution->domain;
    unsigned ec = (unsigned)(frame->esr >> 26) & 63;
    unsigned svc = (unsigned)frame->esr & 65535;
    if (execution->root && !AROSAppleRestrictedExecutionCheck(task, execution)) return 0;
    if (execution->root) {
        /* Identity comes from this task's kernel context, never x0 or TLS. */
        if (!r || r->cookie != R_COOKIE || r->owner != task ||
            r->domain->root != execution->root || frame->entry_ttbr0 != r->domain->root)
            for (;;) __asm__ volatile("wfe"); /* Fail closed on kernel corruption. */
        if (r->domain->closing) { r_finish(r,execution,frame,-5);return 1; }
#if defined(__AROSEXEC_SMP__)
        unsigned cpu=AROSCPUCurrentIndex();
#else
        unsigned cpu=0;
#endif
        r->result.cpus|=UINT64_C(1)<<cpu;
        if (cpu!=r->last_cpu) { ++r->result.migrations;r->last_cpu=cpu; }
        if (ec != 0x15) {
            uintptr_t physical;uint64_t generation,reservation;
            unsigned fault=(unsigned)frame->esr&63;
            if (r_jit_switch(r,task,context,frame,ec,fault)) return 1;
            if (r_spurious_fault(r,frame,ec,fault)) return 1;
            if (ec==0x18 && r_emulate_id_read(frame)) return 1;
            /* Owned resident or reserved data pages can enter a repair handler.
             * Missing tables/pages produce translation faults; RO writes permission faults.
             * An inherited privileged Device identity mapping can report an
             * alignment abort before its EL0 permission abort. Permit repair
             * only when the owned reservation has no resident backing; after
             * commitment, real alignment faults still reach the exception
             * handler. No external or unowned-address abort is repairable.
             * Bind the continuation to the retained page generation. */
            if (ec==0x24 && ((fault>=4 && fault<=7) || (fault>=12 && fault<=15) || fault==0x21) && !(frame->esr&(UINT64_C(1)<<10)) &&
                r_capture_code(r,frame->elr,&r->fault_code) && r_stack_sp(r,frame->sp) &&
                r->fault_handler && r_match_code(r,&r->handler_code) &&
                !r->fault_active && !r->exception_active && r->fault_generation!=UINT64_MAX &&
                AROSRestrictedSpaceFaultIdentity(&r->domain->space,frame->far,&physical,&generation,&reservation) &&
                (fault!=0x21 || !physical)) {
                r->fault_physical=physical;r->fault_page_generation=generation;r->fault_reservation=reservation;
                r->fault=*frame;r->fault_active=1;++r->fault_generation;
                frame->elr=r->fault_handler;frame->x[0]=r->fault.far;
                frame->x[1]=r->fault.esr;frame->x[2]=r->fault.elr;
                frame->x[3]=r->fault_generation;frame->x[30]=0;
                frame->sp=r->fault_address+r->fault_bytes;r_stack_bounds(r,1);
                return 1;
            }
            if (r_debug_stop(r,execution,frame)) return 1;
            if (r_deliver_exception(r,frame)) return 1;
            r_finish(r,execution,frame,-2);
            return 1;
        }
        /* A program built before API 38 submits its request and collects
         * the reply in the same buffer. For a served thread that pair is
         * one call: the submit is the call it stands for, and the collect
         * that follows finds its reply already there. */
        if (r->serve_buffer && svc==AROS_RESTRICTED_SUBMIT &&
            frame->x[0]==frame->x[1] && frame->x[2]==256 && !frame->x[3]) svc=AROS_RESTRICTED_CALL;
        if (r->serve_buffer && svc==AROS_RESTRICTED_COLLECT) {
            ++r->result.replies;frame->x[0]=1;return 1;
        }
        if (r->exception_active && svc!=AROS_RESTRICTED_RESTORE_CONTEXT && svc!=AROS_RESTRICTED_EXIT) {
            ++r->result.denied;frame->x[0]=UINT64_MAX;return 1;
        }
        switch (svc) {
        case AROS_RESTRICTED_RESTORE_CONTEXT:
            if (!r_restore_context(r,frame)) {
                ++r->result.rejected;frame->x[0]=0;
            } else if (r->exception_active) {
                r->exception_active=0;r_track_stack(r,frame->sp);
            }
            return 1;
        case AROS_RESTRICTED_EXCEPTION_HANDLER:
            if (r->fault_active || (frame->x[0] && !r_capture_code(r,frame->x[0],&r->exception_code))) {
                ++r->result.rejected;frame->x[0]=0;
            } else { r->exception_handler=frame->x[0];frame->x[0]=1; }
            return 1;
        case AROS_RESTRICTED_THREAD_INFO:
            frame->x[0]=r->service.channel;frame->x[1]=r->wake_generation;
            frame->x[2]=cpu;return 1;
        case AROS_RESTRICTED_CLOCK:
            frame->x[0]=r_wait_clock();return 1;
        case AROS_RESTRICTED_WAIT:
        case AROS_RESTRICTED_WAIT_FOR: {
            if (frame->x[0]!=r->wake_generation || r->wake_generation==UINT64_MAX || r->wait_pending) {
                frame->x[0]=0;return 1;
            }
            uint64_t deadline=0;
            if (svc==AROS_RESTRICTED_WAIT_FOR) {
                if (!frame->x[1]) { frame->x[0]=AROS_RESTRICTED_WAIT_EXPIRED;return 1; }
                uint64_t now=r_wait_clock();
                if (now==UINT64_MAX || frame->x[1]>UINT64_MAX-now) { frame->x[0]=0;return 1; }
                deadline=now+frame->x[1];
            }
            frame->x[0]=AROSAppleRestrictedSetWait(task,context,1);
            if (!frame->x[0]) return 1;
            r->wait_pending=1;r->wait_result=0;r->wait_deadline=deadline;
            if (deadline) __atomic_add_fetch(&timed_waiters,1,__ATOMIC_RELEASE);
            return 2;
        }
        case AROS_RESTRICTED_WAKE:
            frame->x[0]=r_wake_channel(r->domain,frame->x[0]);return 1;
        case AROS_RESTRICTED_YIELD:
            ++r->result.yields;
            frame->x[0] = 0;
            return 2; /* Schedule only after releasing the coordinator. */
        case AROS_RESTRICTED_SUBMIT: {
            struct RestrictedCodeIdentity entry;
            /* Bind pending entry to executable backing. Active callbacks may
             * use the worker but cannot recursively install another callback. */
            if (frame->x[3] && (r->callback_active || r->fault_active || !r_capture_code(r,frame->x[3],&entry))) {
                ++r->result.rejected;frame->x[0]=0;return 1;
            }
            if (AROSRestrictedServiceSubmit(&r->service,frame->x[0],frame->x[1],frame->x[2]))
                {
                r->callback_pc=frame->x[3];
                if (r->callback_pc) r->callback_entry=entry;
                frame->x[0]=r->service.request.token;
            }
            else { ++r->result.rejected;frame->x[0]=0; }
            return 1;
        }
        case AROS_RESTRICTED_COLLECT:
            if (AROSRestrictedServiceCollect(&r->service,frame->x[0])) {
                ++r->result.replies;frame->x[0]=1;
                if (r->callback_pc) {
                    if (!r_match_code(r,&r->callback_entry) ||
                        !r_capture_code(r,frame->elr,&r->callback_return)) {
                        r_finish(r,execution,frame,-4);return 1;
                    }
                    r->callback=*frame;r->callback_active=1;
                    frame->elr=r->callback_pc;r->callback_pc=0;
                    frame->x[0]=r->seed;frame->x[1]=AROS_RESTRICTED_DATA;
                    frame->x[30]=0;
                }
            } else {
                frame->x[0]=0;return 2;
            }
            return 1;
        case AROS_RESTRICTED_CALLBACK_RETURN:
            if (!r->callback_active || r->fault_active || r->service.request.queue) {
                ++r->result.denied;frame->x[0]=UINT64_MAX;return 1;
            }
            if (frame->x[0] || !r_match_code(r,&r->callback_return)) { r_finish(r,execution,frame,-4);return 1; }
            *frame=r->callback;r->callback_active=0;++r->result.callbacks;
            return 1;
        case AROS_RESTRICTED_FAULT_HANDLER:
            if (r->fault_active || (frame->x[0] && !r_capture_code(r,frame->x[0],&r->handler_code))) {
                ++r->result.rejected;frame->x[0]=0;
            } else { r->fault_handler=frame->x[0];frame->x[0]=1; }
            return 1;
        case AROS_RESTRICTED_PROTECT_DATA:
            if (frame->x[0]!=AROS_RESTRICTED_DATA || frame->x[1]>UINT32_MAX ||
                !AROSRestrictedSpaceProtect(&r->domain->space,(unsigned)frame->x[1])) {
                ++r->result.rejected;frame->x[0]=0;
            } else frame->x[0]=1;
            return 1;
        case AROS_RESTRICTED_PROTECT_MEMORY: {
            uintptr_t address=frame->x[0];unsigned ok=0;
            if (!(address&(R_PAGE-1)) && frame->x[1]<=3)
                for (struct RestrictedAllocation *a=r->domain->allocations;a;a=a->next)
                    if (address>=a->region.mapping.start && address-a->region.mapping.start<a->region.mapping.length) {
                        ok=AROSRestrictedRegionProtect(&a->region,(address-a->region.mapping.start)/R_PAGE,(unsigned)frame->x[1]);break;
                    }
            if (!ok) ++r->result.rejected;
            frame->x[0]=ok;return 1;
        }
        case AROS_RESTRICTED_FAULT_RESUME: {
            uintptr_t physical;uint64_t generation,reservation;
            if (!r->fault_active || frame->x[0]!=r->fault.elr ||
                frame->x[1]!=r->fault_generation || r->service.request.queue ||
                !r_match_code(r,&r->fault_code) || !r_owned_sp(r,r->fault.sp)) {
                ++r->result.denied;frame->x[0]=UINT64_MAX;return 1;
            }
            /* The page is no longer the one the handler repaired: another
             * thread of the domain released, replaced or re-reserved it while
             * this one waited. The saved frame runs the access again, so the
             * CPU checks it against what is mapped now, and a fault that is
             * still due comes back with a new ticket. Refusing instead turned
             * the race into a signal for an access the mapping allowed. A
             * repair that never holds is refused after a few rounds. */
            if (!AROSRestrictedSpacePageIdentity(&r->domain->space,r->fault.far,
                    (r->fault.esr&64) ? KMAP_WRITE : KMAP_READ,&physical,&generation,&reservation) ||
                reservation!=r->fault_reservation ||
                (r->fault_physical ? (physical!=r->fault_physical || generation!=r->fault_page_generation) :
                 (r->fault_page_generation==UINT64_MAX || generation!=r->fault_page_generation+1))) {
                if (r->fault_retry_far!=r->fault.far || r->fault_retry_pc!=r->fault.elr) {
                    r->fault_retry_far=r->fault.far;r->fault_retry_pc=r->fault.elr;r->fault_retry_count=0;
                }
                if (++r->fault_retry_count>R_FAULT_RETRY_LIMIT) {
                    ++r->result.denied;frame->x[0]=UINT64_MAX;return 1;
                }
            } else r->fault_retry_count=0;
            *frame=r->fault;r->fault_active=0;r_track_stack(r,frame->sp);++r->result.resumed;
            return 1;
        }
        case AROS_RESTRICTED_FAULT_DECLINE:
            /* The handler has no repair: the fault becomes a synchronous
             * exception for the registered exception handler. */
            if (!r->fault_active || frame->x[0]!=r->fault.elr ||
                frame->x[1]!=r->fault_generation || r->service.request.queue) {
                ++r->result.denied;frame->x[0]=UINT64_MAX;return 1;
            }
            *frame=r->fault;r->fault_active=0;r_track_stack(r,frame->sp);
            if (r_debug_stop(r,execution,frame)) return 1;
            if (!r_deliver_exception(r,frame)) r_finish(r,execution,frame,-2);
            return 1;
        case AROS_RESTRICTED_CANCEL: {
            unsigned state=r->service.request.state;
            int cancelled=KrnBufferServiceCancel(&r->service.request,r->service.principal,frame->x[0]);
            if (cancelled) {
                r->callback_pc=0;
                if (state!=KS_CANCELLED) ++r->result.cancelled;
            }
            frame->x[0]=cancelled;
            return 1;
        }
        case AROS_RESTRICTED_STATUS:
            frame->x[0]=r->service.request.queue && r->service.request.token==frame->x[0] ?
                r->service.request.state : 0;
            return 2;
        case AROS_RESTRICTED_CALL:
            /* Leave the domain for this task's own native code, as a
             * finished thread does, but keep the thread: its frame waits in
             * the slot for R_SERVE. The task keeps naming the slot, which
             * is how the kernel knows it acts for the domain's controller. */
            if (!r->serve_buffer || r->call_active || r->callback_active || r->callback_pc ||
                r->service.request.queue || r->wait_pending ||
                !AROSRestrictedSpaceCopy(&r->domain->space,frame->x[0],r->serve_buffer,256,KMAP_READ)) {
                ++r->result.denied;frame->x[0]=UINT64_MAX;return 1;
            }
            r->call=*frame;r->call_address=frame->x[0];r->call_active=1;
            r->owner->tc_SPLower=r->native_lower;
            r->owner->tc_SPUpper=r->native_upper;
            r->owner->tc_SPReg=(void *)(uintptr_t)r->native.sp;
            *frame=r->native;
            frame->x[0]=2;
            *execution=(struct AROSAppleTaskExecution){0};
            execution->domain=r;
            return 1;
        case AROS_RESTRICTED_EXIT:
            r_finish(r,execution,frame,(int64_t)frame->x[0]);
            return 1;
        default:
            ++r->result.denied;
            frame->x[0] = UINT64_MAX;
            return 1;
        }
    }
    if (ec == 0x15 && (svc == 0xa532 || svc == 0xa533 || svc == 0xa535)) {
        void *work=(void *)(uintptr_t)frame->x[0];
        frame->x[0]=svc==0xa532 ? AROSRestrictedServiceTake(task,context,work) :
            svc==0xa533 ? AROSRestrictedServiceComplete(task,work) :
            AROSRestrictedServiceState(task,work);
        return 1;
    }
    if (ec==0x15 && svc==0xa536) {
        void *retired=(void *)(uintptr_t)frame->x[0];
        frame->x[0]=AROSRestrictedServiceRetireWorker(retired);
        r_mark_retired_resources(retired);
        return 1;
    }
    if (ec==0x15 && svc==0xa537) {
        frame->x[0]=(uintptr_t)r_claim_reap(context);
        return 1;
    }
    /* A task that handles a call of its served thread acts for the
     * controller that owns the thread's domain: memory, shared objects,
     * domains and threads of that controller are its to manage. The record
     * of an ordinary task names no slot here. */
    void *principal=context;
    trap_context=context;
    if (r && r_slot((uintptr_t)r)==r && r->cookie==R_COOKIE && r->state==R_LIVE &&
        r->owner==task && r->call_active) principal=r->domain->owner_context;
    if (ec==0x15 && svc==R_REAP_REFRESH) {
        frame->x[0]=(uintptr_t)r_refresh_reap_hint();
        return 1;
    }
    if (ec==0x15 && (svc==R_WAKE || svc==R_THREAD_STATE || svc==R_SUSPEND || svc==R_RESUME)) {
        struct RestrictedDomain *d=r_domain_lookup(frame->x[0]);
        uint64_t channel=frame->x[1];
        frame->x[0]=(svc==R_SUSPEND || svc==R_RESUME) ? UINT64_MAX : 0;
        if (d && d->owner_context==principal) {
            if (svc==R_WAKE) frame->x[0]=r_wake_channel(d,channel);
            else if (svc==R_SUSPEND || svc==R_RESUME)
                frame->x[0]=(uint64_t)r_suspend_channel(d,channel,svc==R_RESUME);
            else for (struct Restricted *r=restricted_slots;r;r=r->next)
                if (r->domain==d && r->state==R_LIVE && r->service.channel==channel) {
                    frame->x[0]=AROSAppleRestrictedWaitState(r->owner,r->owner_context);break;
                }
        }
        return 1;
    }
    if (ec==0x15 && svc==R_DEBUG) {
        struct RestrictedDomain *d=r_domain_lookup(frame->x[0]);
        uint64_t channel=frame->x[1],op=frame->x[2],argument=frame->x[3];
        frame->x[0]=0;
        if (d && d->owner_context==principal) frame->x[0]=(uint64_t)r_debug(d,channel,(unsigned)op,argument);
        return 1;
    }
    if (ec==0x15 && svc==0xa56e) {
        struct RestrictedDomain *d=r_domain_lookup(frame->x[0]);
        struct AROSRestrictedWork *work=(void *)(uintptr_t)frame->x[1];
        frame->x[0]=0;
        if (d && d->managed && d->state==R_LIVE && !d->closing && d->owner_context==context)
            frame->x[0]=AROSRestrictedServiceTakeSpace(task,context,&d->space,work);
        return 1;
    }
    if (ec==0x15 && r_table_transfer(frame,principal,svc)) return 1;
    if (ec==0x15 && r_shared_operation(frame,principal,svc)) return 1;
    if (ec==0x15 && r_memory(frame,principal,svc)) return 1;
    if (ec==0x15 && r_copy(frame,principal,svc)) return 1;
    if (ec==0x15 && r_domain_lifecycle(frame,principal,svc)) return 1;
    if (ec==0x15 && r_lifecycle(frame,task,context,svc)) return 1;
    if (ec != 0x15 || (svc != R_PREPARE && svc != AROS_RESTRICTED_ENTER && svc != 0xa534 &&
        svc != R_SERVE)) return 0;
    uint64_t serve_buffer=frame->x[1],serve_mode=frame->x[2],serve_status=frame->x[3];
    frame->x[1] = 0;
    r = r_slot(frame->x[0]);
    frame->x[0] = 0;
    if (!r || r->state!=R_LIVE || r->cookie != R_COOKIE || r->owner != task ||
        frame->entry_ttbr0 != AROSAppleRuntimeLocal()->trusted_ttbr0) {
        if (svc == R_SERVE)
            bug("[RESTRICTED] served rejected identity slot=%p task=%p owner=%p state=%u mode=%llu root=%llx trusted=%llx\n",
                r,task,r ? r->owner : NULL,r ? r->state : 0,(unsigned long long)serve_mode,
                (unsigned long long)frame->entry_ttbr0,
                (unsigned long long)AROSAppleRuntimeLocal()->trusted_ttbr0);
        return 1;
    }
    if (svc == R_SERVE && serve_mode == 0) {
        /* First entry of a served thread: as ENTER, with the buffer. */
        if (!serve_buffer || r->call_active || r->serve_buffer) {
            bug("[RESTRICTED] served rejected start slot=%p buffer=%llx active=%u previous=%p\n",
                r,(unsigned long long)serve_buffer,r->call_active,r->serve_buffer);
            return 1;
        }
        r->serve_buffer=(void *)(uintptr_t)serve_buffer;
    } else if (svc == R_SERVE) {
        if (!r->call_active) {
            bug("[RESTRICTED] served rejected inactive slot=%p mode=%llu\n",r,(unsigned long long)serve_mode);
            return 1;
        }
        if (serve_mode == 2 || r->domain->closing || !r->domain->root) {
            /* The handler ends the thread, or its domain is going away. */
            r->callback_active=0;r->fault_active=0;r->exception_active=0;r->suspend_count=0;
            r->call_active=0;r->debug_stop=0;
            r_clear_wait(r);
            r->result.status=serve_mode == 2 ? (int64_t)serve_status : -5;
            r->result.value=*(uint64_t *)r->domain->data;
            *execution=(struct AROSAppleTaskExecution){0};
            frame->x[0]=1;
            return 1;
        }
        /* Held by its controller: not now. The native side yields and asks again. */
        if (r->suspend_count) { frame->x[0]=3;return 1; }
#if defined(__AROSEXEC_SMP__)
        if (TLS_GET(IDNestCnt)>=0 || TLS_GET(TDNestCnt)>=0 || AROSAppleRuntimeLocal()->irq_mask_depth) {
            bug("[RESTRICTED] served rejected masked slot=%p id=%ld td=%ld irq=%lu\n",r,
                (long)TLS_GET(IDNestCnt),(long)TLS_GET(TDNestCnt),
                (unsigned long)AROSAppleRuntimeLocal()->irq_mask_depth);
            return 1;
        }
        r->last_cpu=AROSCPUCurrentIndex();
#else
        if (SysBase->IDNestCnt>=0 || SysBase->TDNestCnt>=0 || AROSAppleRuntimeLocal()->irq_mask_depth) return 1;
#endif
        if (!r->debug_stop &&
            !AROSRestrictedSpaceCopy(&r->domain->space,r->call_address,r->serve_buffer,256,KMAP_WRITE)) {
            /* The thread unmapped its own request: it cannot be continued. */
            r->call_active=0;r_clear_wait(r);
            r->result.status=-6;
            *execution=(struct AROSAppleTaskExecution){0};
            frame->x[0]=1;
            return 1;
        }
        r->native=*frame;
        r->native_lower=task->tc_SPLower;
        r->native_upper=task->tc_SPUpper;
        r->call_active=0;
        if (r->fault_active || r->exception_active) r_stack_bounds(r,1);
        else r_track_stack(r,r->call.sp);
        task->tc_SPReg=(void *)(uintptr_t)r->call.sp;
        AROSAppleRuntimeLocal()->kernel_tls=frame->task_tls;
        *frame=r->call;
        if (!r->debug_stop) frame->x[0]=1;
        execution->root=r->domain->root;
        execution->user_tls=frame->task_tls;
        execution->domain=r;
        frame->return_ttbr0=r->domain->root;
        if (r->debug_stop) {
            /* The debugger resumes the thread where it stopped (mode 3) or
             * hands the exception to the thread's own handler (mode 4). */
            r->debug_stop=0;
            if (serve_mode==4 && !r_deliver_exception(r,frame)) r_finish(r,execution,frame,-2);
        }
        return 1;
    }
    if (svc == 0xa534) {
        frame->x[0]=r_close(r);
        if (frame->x[0]) { r->state=R_REAPING;r->releaser_context=context; }
        else ++r->result.close_waits;
        return 1;
    }
    if (svc == R_PREPARE) {
        frame->x[0] = r_prepare(r,frame->entry_ttbr0);
        frame->x[1] = r->domain->table_shortage;
        return 1;
    }
    /* Inspect live CPU nesting, not the saved task scheduling snapshot. */
#if defined(__AROSEXEC_SMP__)
    int disabled=TLS_GET(IDNestCnt)>=0 || TLS_GET(TDNestCnt)>=0;
    unsigned cpu=AROSCPUCurrentIndex();
#else
    int disabled=SysBase->IDNestCnt>=0 || SysBase->TDNestCnt>=0;
    unsigned cpu=0;
#endif
    if (!r->domain->root || r->domain->closing || disabled ||
        AROSAppleRuntimeLocal()->irq_mask_depth) return 1;
    r->last_cpu=cpu;r->result.cpus=UINT64_C(1)<<cpu;
    r->native = *frame;
    r->native_lower = task->tc_SPLower;
    r->native_upper = task->tc_SPUpper;
    r_stack_bounds(r,0);
    task->tc_SPReg = task->tc_SPUpper;
    execution->root = r->domain->root;
    execution->user_tls = r->tls_address;
    execution->domain = r;
    AROSAppleRuntimeLocal()->kernel_tls = frame->task_tls;
    memset(frame->x,0,sizeof(frame->x));
    memset(frame->simd,0,sizeof(frame->simd));
    frame->x[0] = r->seed;
    frame->x[1] = AROS_RESTRICTED_DATA;
    frame->x[18] = execution->user_tls;
    frame->elr = AROS_RESTRICTED_CODE;
    frame->sp = r->stack_address + r->stack_bytes;
    frame->spsr &= UINT64_C(0xf00003c0);
    frame->task_tls = execution->user_tls;
    frame->return_ttbr0 = r->domain->root;
    return 1;
}
/* Exception entry already masks local IRQ/FIQ. Do not use the native IRQ
 * nesting API here: admission must inspect the interrupted task's real mask
 * depth, and generic Exec IRQ SVCs must never enter this coordinator lock.
 * Lock order is PageMapLock -> coordinator -> scheduler/task locks. No native
 * page allocator, blocking Exec operation or PageMutationBegin while held. */
static uint32_t coordinator_lock;
static void r_coordinate(void)
{
    for (;;) {
#if defined(__AROSEXEC_SMP__)
        /* A mapping owner may hold PageMapLock and wait for this lock. A
         * masked contender must acknowledge its rendezvous instead of
         * preventing the owner from making progress. */
        AROSAppleMemoryRendezvousPoll();
#endif
        if (!__atomic_exchange_n(&coordinator_lock,1,__ATOMIC_ACQUIRE)) break;
        __asm__ volatile("yield" ::: "memory");
    }
}
static void r_uncoordinate(void)
{
    __atomic_store_n(&coordinator_lock,0,__ATOMIC_RELEASE);
}
static uint64_t r_alias_no_allocate(void *context,unsigned bits)
{ (void)context;(void)bits;return 0; }
static void r_alias_sync(void *context,uint64_t address,size_t bytes)
{
    /* Native identity alias, not an inactive user VA or domain->code. */
    r_clean(context,(const void *)(uintptr_t)address,bytes);
    __asm__ volatile("ic ialluis; dsb ish; isb":::"memory");
}
static void r_alias_invalidate(void *context)
{
    /* RemapPrivilegedPage requires an existing leaf and cannot split a block.
     * Every snapshot changes the same native VA under the memory rendezvous.
     * Invalidate that VA on all PEs/ASIDs, retaining unrelated translations. */
    uint64_t argument=(*(const uintptr_t *)context>>12)&UINT64_C(0xfffffffffff);
    __asm__ volatile("dsb ishst; tlbi vaae1is,%0; dsb ish; isb"::"r"(argument):"memory");
}
int AROSAppleRestrictedProtectAliasesLocked(uintptr_t address,uint64_t tcr,unsigned permissions)
{
    /* The arena is pre-split in every snapshot; no table allocation is needed.
     * Other native mapping regions require their own mirror/split protocol. */
    if (!AROSApplePageArenaContains(address)) return 1;
    unsigned policy=permissions & (AARCH64_PROTECT_READ|AARCH64_PROTECT_WRITE|AARCH64_PROTECT_EXEC);
    /* A writable user/JIT source is implicitly EL1-XN, as in root cloning. */
    if (policy & AARCH64_PROTECT_WRITE) policy&=~AARCH64_PROTECT_EXEC;
    const struct AArch64ProtectionOps ops={&address,r_resolve,r_alias_no_allocate,r_clean,r_alias_invalidate,r_alias_sync,NULL};
    int ok=1;
    for (struct RestrictedDomain *d=restricted_domains;d;d=d->next) {
        /* REAPING roots have no executing contexts, and native teardown can
         * already be freeing their table chunks outside the coordinator. */
        if (d->state==R_REAPING || !d->root) continue;
        if (!AArch64RemapPrivilegedPage(&ops,d->root,tcr,address,address,policy)) { ok=0;break; }
    }
    return ok;
}
int AROSAppleRestrictedProtectAliases(uintptr_t address,uint64_t tcr,unsigned permissions)
{
    if (!AROSApplePageArenaContains(address)) return 1;
    /* Acquire only after remote rendezvous arrival, never start one here. */
    r_coordinate();
    int ok=AROSAppleRestrictedProtectAliasesLocked(address,tcr,permissions);
    r_uncoordinate();return ok;
}
int AROSAppleRestrictedDataAliasesMatch(uintptr_t address,uint64_t tcr,unsigned permissions)
{
    if (!AROSApplePageArenaContains(address) || (permissions&~11U) ||
        !(permissions&AARCH64_PROTECT_READ)) return 0;
    const struct AArch64ProtectionOps ops={.resolve=r_resolve};
    int ok=1;
    r_coordinate();
    for (struct RestrictedDomain *d=restricted_domains;d;d=d->next) {
        if (d->state==R_REAPING || !d->root) continue;
        if (!AArch64DataPageMatches(&ops,d->root,tcr,address,address,permissions&3U)) {
            ok=0;break;
        }
    }
    r_uncoordinate();return ok;
}
static void r_mark_retired_resources(void *retired)
{
    if (!retired) return;
    unsigned orphaned=0;
    for (struct RestrictedShared *b=restricted_shared;b;b=b->next)
        if (b->owner_context==retired && !b->releaser_context) {
            b->owner_context=NULL;b->backing.closing=1;orphaned=1;
        }
    for (struct RestrictedDomain *d=restricted_domains;d;d=d->next)
        if (d->managed && d->state==R_LIVE && d->owner_context==retired) {
            d->owner_context=NULL;d->closing=1;AROSRestrictedSpaceStop(&d->space);
            r_cancel_waiters(d);orphaned=1;
        }
    for (struct Restricted *r=restricted_slots;r;r=r->next) {
        if (r->state!=R_LIVE || r->owner_context!=retired) continue;
        r_clear_wait(r);r->suspend_count=0;
        r->owner_context=NULL;r->abandoned=1;++owners_retired;orphaned=1;
        /* Never dereference the removed Task after this hook. Service
         * principal/member identities are opaque until their drain. */
        r->owner=NULL;
        r_close(r);
    }
    if (orphaned) __atomic_store_n(&orphan_reap_pending,1,__ATOMIC_RELEASE);
}
int AROSAppleRestrictedTrap(struct AROSAppleRuntimeFrame *frame)
{
    if (!frame || frame->vector!=8 || (frame->spsr&31)) return 0;
    struct ExceptionContext *context=r_context(r_current());
    if (!context) return 0;
    unsigned ec=(unsigned)(frame->esr>>26)&63,svc=(unsigned)frame->esr&65535;
    if (!AROSAppleTaskExecution(context)->root &&
        (ec!=0x15 || ((svc<AROS_RESTRICTED_ENTER || svc>R_SERVE) &&
         (svc<R_DOMAIN_CLAIM || svc>R_DEBUG)))) return 0;
    r_coordinate();
    int handled=r_trap_locked(frame);
    r_uncoordinate();
    if (handled) AROSAppleDebugStepArm(frame);
    if (handled==2) {
        /* The scheduler saves the outgoing frame and can finish an invalid
         * restricted stack. It must never recursively acquire our lock or
         * carry it while choosing/dispatching another task. */
        core_SysCall(SC_SWITCH,frame);
        cpu_ApplyCurrentTaskMasks(&frame->spsr);
    }
    return handled!=0;
}
static void r_free_allocation(struct RestrictedAllocation *a)
{
    for (size_t i=0;i<a->count;) {
        uintptr_t first=r_backing(a,i);
        size_t pages=1;
        /* The detached allocation owns every backing page. Coalesce adjacent
         * physical pages so restoration shares a bounded SMP rendezvous. */
        if (first) {
            while (pages<64 && pages<a->count-i &&
                    r_backing(a,i+pages)==first+pages*R_PAGE) ++pages;
            if (!AROSApplePageFree((void *)first,pages*R_PAGE))
                for (;;) __asm__ volatile("wfe");
        }
        i+=pages;
    }
    struct RestrictedAllocation *root=a->storage;
    if (a!=root && !AROSApplePageFree(a,a->storage_bytes)) for (;;) __asm__ volatile("wfe");
    if (__atomic_sub_fetch(&root->storage_refs,1,__ATOMIC_ACQ_REL)) return;
    for (size_t c=0;c<root->chunk_count;++c)
        if (root->pages[c] && !AROSApplePageFree(root->pages[c],root->chunk_bytes))
            for (;;) __asm__ volatile("wfe");
    if (!AROSApplePageFree(root,root->storage_bytes)) for (;;) __asm__ volatile("wfe");
}
static void r_free_domain_backing(struct RestrictedDomain *d)
{
    struct RestrictedAllocation *a=d->allocations;
    while (a) {
        struct RestrictedAllocation *next=a->next;r_free_allocation(a);a=next;
    }
    if ((d->code && !AROSApplePageFree(d->code,r_code_storage(d->code_bytes))) ||
        (d->data && !AROSApplePageFree(d->data,R_PAGE)))
        for (;;) __asm__ volatile("wfe");
    struct RestrictedTableChunk *c=d->tables;
    while (c) {
        struct RestrictedTableChunk *next=c->next;
        if (!AROSApplePageFreeRaw(c->pages,c->count*R_PAGE) || !AROSApplePageFree(c,R_PAGE))
            for (;;) __asm__ volatile("wfe");
        c=next;
    }
}
static void r_release_domain(struct RestrictedDomain *d)
{
    size_t storage=d->storage_bytes;
    r_free_domain_backing(d);
    register uintptr_t argument __asm__("x0")=(uintptr_t)d;
    __asm__ volatile("svc #0xa564":"+r"(argument)::"x1","memory","cc");
    if (!argument || !AROSApplePageFree(d,storage)) for (;;) __asm__ volatile("wfe");
}
static void r_release(struct Restricted *r)
{
    size_t thread_storage=r->storage_bytes;
    /* Native stack only, with scheduling excluded. Local root return completed
     * before this call; worker/domain pins have drained. */
    if ((r->stack && !AROSApplePageFree(r->stack,r->stack_bytes)) ||
        (r->fault_stack && !AROSApplePageFree(r->fault_stack,r->fault_bytes)) ||
        (r->tls && !AROSApplePageFree(r->tls,R_PAGE)))
        for (;;) __asm__ volatile("wfe");
    struct RestrictedDomain *domain=r->domain->managed ? NULL : r->domain;
    size_t domain_storage=domain ? domain->storage_bytes : 0;
    if (domain) r_free_domain_backing(domain);
    register uintptr_t released __asm__("x0")=(uintptr_t)r;
    __asm__ volatile("svc #0xa53a":"+r"(released)::"memory","cc");
    if (!released || (domain && !AROSApplePageFree(domain,domain_storage)) ||
        !AROSApplePageFree(r,thread_storage)) for (;;) __asm__ volatile("wfe");
}
static uintptr_t r_shared_call(struct RestrictedSharedOperation *p)
{
    register uintptr_t argument __asm__("x0")=(uintptr_t)p;
    __asm__ volatile("svc #0xa568":"+r"(argument)::"memory","cc");
    return argument;
}
static void r_release_shared(struct RestrictedShared *b)
{
    for (size_t i=0;i<b->count;++i)
        if (b->pages[i] && !AROSApplePageFree((void *)b->pages[i],R_PAGE))
            for (;;) __asm__ volatile("wfe");
    struct RestrictedSharedOperation op={.operation=3,.handle=b->handle};
    if (!r_shared_call(&op)) for (;;) __asm__ volatile("wfe");
    if (!AROSApplePageFree(b,b->storage_bytes)) for (;;) __asm__ volatile("wfe");
}
/* With a view ended (pending), unmap it and let the object go on */
static void r_unmap_view(struct RestrictedSharedOperation *ended)
{
    struct RestrictedShared *b=ended->record;
    if (!AROSAppleCodeWindowUnmapForeign(&AROSAppleCodeWindowKernelOps,
            (void *)ended->address,(uint32_t)b->count)) for (;;) __asm__ volatile("wfe");
    struct RestrictedSharedOperation done={.operation=9,.handle=ended->handle};
    if (!r_shared_call(&done)) for (;;) __asm__ volatile("wfe");
}
static void r_reap(void)
{
    if (!__atomic_load_n(&orphan_reap_pending,__ATOMIC_ACQUIRE)) return;
    Forbid();
    for (;;) {
        register uintptr_t ready __asm__("x0")=0;
        __asm__ volatile("svc #0xa537":"+r"(ready)::"memory","cc");
        if (!ready) break;
        r_release((void *)ready);
    }
    for (;;) {
        register uintptr_t ready __asm__("x0")=0;
        __asm__ volatile("svc #0xa565":"+r"(ready)::"x1","memory","cc");
        if (!ready) break;
        r_release_domain((void *)ready);
    }
    /* A retired controller's native views go before its objects can */
    for (;;) {
        struct RestrictedSharedOperation op={.operation=11};
        if (!r_shared_call(&op)) break;
        r_unmap_view(&op);
    }
    for (;;) {
        struct RestrictedSharedOperation op={.operation=4};
        if (!r_shared_call(&op)) break;
        r_release_shared(op.record);
    }
    /* A resource may still be pinned, or another CPU may be releasing it.
     * Only a coordinated scan of all retired records can clear the hint. */
    register uintptr_t pending __asm__("x0")=0;
    __asm__ volatile("svc #0xa570":"+r"(pending)::"memory","cc");
    (void)pending;
    Permit();
}
static void r_lifetime(struct AROSRestrictedLifetime *state)
{
    if (!state) return;
    r_reap();Forbid();
    register uintptr_t argument __asm__("x0")=(uintptr_t)state;
    __asm__ volatile("svc #0xa53b":"+r"(argument)::"memory","cc");
    if (!argument) for (;;) __asm__ volatile("wfe");
    Permit();
}
static struct RestrictedDomain *r_new_domain(void)
{
    size_t storage=(sizeof(struct RestrictedDomain)+R_PAGE-1)&~(size_t)(R_PAGE-1);
    struct RestrictedDomain *d=AROSApplePageAllocate(NULL,storage,MEMF_CLEAR);
    if (d) d->storage_bytes=storage;
    return d;
}
static int r_supply_tables(uint64_t handle,struct RestrictedDomain *d,struct Restricted *r,size_t count)
{
    if (!count || count>SIZE_MAX/R_PAGE) return 0;
    struct RestrictedTableChunk *c=AROSApplePageAllocate(NULL,R_PAGE,MEMF_CLEAR);
    if (!c) return 0;
    c->pages=AROSApplePageTableAllocate(count*R_PAGE);c->count=count;
    if (!c->pages) {
        if (!AROSApplePageFree(c,R_PAGE)) for (;;) __asm__ volatile("wfe");
        return 0;
    }
    struct RestrictedTableTransfer p={handle,d,r,c};
    register uintptr_t argument __asm__("x0")=(uintptr_t)&p;
    __asm__ volatile("svc #0xa569":"+r"(argument)::"memory","cc");
    if (!argument && (!AROSApplePageFreeRaw(c->pages,count*R_PAGE) || !AROSApplePageFree(c,R_PAGE)))
        for (;;) __asm__ volatile("wfe");
    return argument!=0;
}
static uintptr_t r_prepare_native(struct RestrictedDomain *d,struct Restricted *r)
{
    for (;;) {
        uint64_t saved=AROSApplePageMapAcquire();
        register uintptr_t argument __asm__("x0")=r ? (uintptr_t)r : (uintptr_t)d;
        register uintptr_t missing __asm__("x1")=0;
        if (r) __asm__ volatile("svc #0xa531":"+r"(argument),"+r"(missing)::"memory","cc");
        else __asm__ volatile("svc #0xa562":"+r"(argument),"+r"(missing)::"memory","cc");
        AROSAppleMemoryLockRelease(&AROSApplePageMapLock,saved);
        if (argument || !missing || missing==UINTPTR_MAX) return argument;
        if (!r_supply_tables(0,d,r,missing)) return 0;
    }
}
static int r_run_internal(uint64_t handle,const void *code, size_t bytes, uint64_t seed,
    size_t stack_bytes,size_t fault_bytes,
    struct AROSRestrictedResult *result,
    int (*handler)(void *context,unsigned char bytes[256]),void *handler_context)
{
    if ((!handle && (!code || !r_code_storage(bytes))) || !result) return 0;
    /* Validate sizes without consulting the mutable published list. */
    const size_t limit=AROS_RESTRICTED_THREAD_END-AROS_RESTRICTED_STACK;
    if (!stack_bytes || !fault_bytes || ((stack_bytes|fault_bytes)&(R_PAGE-1)) ||
        stack_bytes>limit || fault_bytes>limit-stack_bytes ||
        stack_bytes+fault_bytes>limit-4*R_PAGE-(AROS_RESTRICTED_THREAD_STRIDE-1)) return 0;
    r_reap();
    /* The page arena does not invoke waiting Exec allocation callbacks.
     * Metadata stays privately owned until its coordinator publication. */
    Forbid();
    struct RestrictedDomain *prepared_domain=handle ? NULL : r_new_domain();
    if (!handle && !prepared_domain) { Permit();return 0; }
    size_t thread_storage=(sizeof(struct Restricted)+R_PAGE-1)&~(size_t)(R_PAGE-1);
    struct Restricted *prepared_thread=AROSApplePageAllocate(NULL,thread_storage,MEMF_CLEAR);
    if (!prepared_thread) {
        if (prepared_domain && !AROSApplePageFree(prepared_domain,prepared_domain->storage_bytes))
            for (;;) __asm__ volatile("wfe");
        Permit();return 0;
    }
    prepared_thread->storage_bytes=thread_storage;
    prepared_thread->stack_bytes=stack_bytes;prepared_thread->fault_bytes=fault_bytes;
    register uintptr_t claim __asm__("x0")=handle;
    register uintptr_t candidate __asm__("x1")=(uintptr_t)prepared_domain;
    register uintptr_t thread __asm__("x2")=(uintptr_t)prepared_thread;
    __asm__ volatile("svc #0xa538":"+r"(claim),"+r"(candidate),"+r"(thread)::"memory","cc");
    struct Restricted *r=(void *)claim;
    if (!r) {
        if (!AROSApplePageFree(prepared_thread,thread_storage)) for (;;) __asm__ volatile("wfe");
        if (prepared_domain && !AROSApplePageFree(prepared_domain,prepared_domain->storage_bytes))
            for (;;) __asm__ volatile("wfe");
        Permit();return 0;
    }
    int ok = 0;
    struct RestrictedPublication publication={.slot=r,.seed=seed,.code_bytes=bytes};
    if (!handle) {
        publication.code = AROSApplePageAllocate(NULL,r_code_storage(bytes),MEMF_CLEAR);
        publication.data = AROSApplePageAllocate(NULL,R_PAGE,MEMF_CLEAR);
    }
    publication.stack = AROSApplePageAllocate(NULL,stack_bytes,MEMF_CLEAR);
    publication.fault_stack = AROSApplePageAllocate(NULL,fault_bytes,MEMF_CLEAR);
    publication.tls = AROSApplePageAllocate(NULL,R_PAGE,MEMF_CLEAR);
    register uintptr_t published __asm__("x0")=(uintptr_t)&publication;
    __asm__ volatile("svc #0xa539":"+r"(published)::"memory","cc");
    if (!published) for (;;) __asm__ volatile("wfe");
    if (!r->domain->code || !r->domain->data || !r->stack || !r->fault_stack || !r->tls) goto prepared;
    if (!handle) {
        memcpy(r->domain->code,code,bytes);
        *(uint64_t *)r->domain->data = seed;
        if (!AROSApplePageProtect(r->domain->code,r_code_storage(bytes),MAP_Readable|MAP_Executable)) goto prepared;
    }
    register uintptr_t argument __asm__("x0")=r_prepare_native(NULL,r);
    if (!argument) goto prepared;
    Permit();
    if (handler) {
        /* The thread runs until it asks (2), is held by its controller (3)
         * or ends; the request and the answer live on this native stack. */
        unsigned char request[256];
        uint64_t mode=0,status=0;
        for (;;) {
            register uintptr_t slot __asm__("x0")=(uintptr_t)r;
            register uintptr_t buffer __asm__("x1")=(uintptr_t)request;
            register uintptr_t how __asm__("x2")=mode;
            register uintptr_t why __asm__("x3")=status;
            __asm__ volatile("svc #0xa53c":"+r"(slot),"+r"(buffer),"+r"(how),"+r"(why)::"memory","cc");
            if (slot==2 || slot==4) {
                /* 4: a debug stop. The handler holds the thread, then word
                 * 0 of the request says resume (0) or deliver (1). */
                int debug_stop=slot==4;
#if defined(__AROSEXEC_SMP__)
                int before_td=TLS_GET(TDNestCnt);
                uint64_t request_head[4];
                memcpy(request_head,request,sizeof(request_head));
#endif
                if (handler(handler_context,request)) mode=debug_stop ? 3+(request[0]&1) : 1;
                else { mode=2;memcpy(&status,request,sizeof(status)); }
#if defined(__AROSEXEC_SMP__)
                int after_td=TLS_GET(TDNestCnt);
                if (before_td>=0 || after_td>=0)
                    bug("[RESTRICTED] handler scheduling slot=%p context=%p td=%ld->%ld request=%llx,%llx,%llx,%llx\n",
                        r,handler_context,(long)before_td,(long)after_td,
                        (unsigned long long)request_head[0],(unsigned long long)request_head[1],
                        (unsigned long long)request_head[2],(unsigned long long)request_head[3]);
#endif
                continue;
            }
            if (slot==3) { krnSysCall(SC_SWITCH);continue; }
            ok = slot != 0;
            break;
        }
        goto cleanup;
    }
    argument = (uintptr_t)r;
    __asm__ volatile("svc #0xa530":"+r"(argument)::"x1","memory","cc");
    ok = argument != 0;
    goto cleanup;
prepared:
    Permit();
cleanup:
    /* Native task context owns retries. A cancelled running worker retains its
     * output lease and the entire root/backing until it acknowledges completion. */
    for (;;) {
        Forbid();
        register uintptr_t closing __asm__("x0")=(uintptr_t)r;
        __asm__ volatile("svc #0xa534":"+r"(closing)::"x1","memory","cc");
        if (closing) break;
        Permit();
        krnSysCall(SC_SWITCH);
    }
    if (ok) {
        r->result.drained=1;
        r->result.worker_retirements=r->service.worker_retirements;
        *result=r->result;
        if (r->snapshot_data) {
            memcpy(result->data,r->domain->data,R_PAGE);result->snapshot_valid=1;
        }
    }
    r_release(r);
    Permit();r_reap();
    return ok;
}

static int r_run(const void *code,size_t bytes,uint64_t seed,struct AROSRestrictedResult *result)
{ return r_run_internal(0,code,bytes,seed,R_PAGE,R_PAGE,result,NULL,NULL); }
static int r_run_domain(uint64_t handle,uint64_t seed,struct AROSRestrictedResult *result)
{ return handle && r_run_internal(handle,NULL,0,seed,R_PAGE,R_PAGE,result,NULL,NULL); }
static int r_run_domain_stacks(uint64_t handle,uint64_t seed,size_t stack_bytes,
    size_t fault_bytes,struct AROSRestrictedResult *result)
{ return handle && r_run_internal(handle,NULL,0,seed,stack_bytes,fault_bytes,result,NULL,NULL); }
static int r_run_served(uint64_t handle,uint64_t seed,size_t stack_bytes,size_t fault_bytes,
    struct AROSRestrictedResult *result,int (*handler)(void *context,unsigned char bytes[256]),void *context)
{ return handle && handler && r_run_internal(handle,NULL,0,seed,stack_bytes,fault_bytes,result,handler,context); }
static uint64_t r_create_domain(const void *code,size_t bytes,uint64_t seed)
{
    if (!code || !r_code_storage(bytes)) return 0;
    r_reap();Forbid();
    struct RestrictedDomain *d=r_new_domain();
    if (!d) { Permit();return 0; }
    register uintptr_t claim __asm__("x0")=(uintptr_t)d;
    __asm__ volatile("svc #0xa560":"+r"(claim)::"x1","memory","cc");
    if (!claim) {
        if (!AROSApplePageFree(d,d->storage_bytes)) for (;;) __asm__ volatile("wfe");
        Permit();return 0;
    }
    struct RestrictedDomainPublication publication={.domain=d,.code_bytes=bytes};
    publication.code=AROSApplePageAllocate(NULL,r_code_storage(bytes),MEMF_CLEAR);
    publication.data=AROSApplePageAllocate(NULL,R_PAGE,MEMF_CLEAR);
    register uintptr_t argument __asm__("x0")=(uintptr_t)&publication;
    __asm__ volatile("svc #0xa561":"+r"(argument)::"x1","memory","cc");
    if (!argument) for (;;) __asm__ volatile("wfe");
    uint64_t handle=0;
    if (d->code && d->data) {
        memcpy(d->code,code,bytes);*(uint64_t *)d->data=seed;
        if (AROSApplePageProtect(d->code,r_code_storage(bytes),MAP_Readable|MAP_Executable)) {
            handle=r_prepare_native(d,NULL);
        }
    }
    if (!handle) {
        argument=(uintptr_t)d;
        __asm__ volatile("svc #0xa566":"+r"(argument)::"x1","memory","cc");
        if (!argument) for (;;) __asm__ volatile("wfe");
        r_release_domain(d);
    }
    Permit();return handle;
}
static int r_close_domain(uint64_t handle)
{
    Forbid();
    register uintptr_t argument __asm__("x0")=handle;
    register uintptr_t status __asm__("x1")=0;
    __asm__ volatile("svc #0xa563":"+r"(argument),"+r"(status)::"memory","cc");
    if (argument) r_release_domain((void *)argument);
    Permit();return (int)(intptr_t)status;
}

static int r_memory_call(struct RestrictedMemoryOperation *p)
{
    int ok;
    for (;;) {
        register uintptr_t argument __asm__("x0")=(uintptr_t)p;
        register uintptr_t missing __asm__("x1")=0;
        __asm__ volatile("svc #0xa567":"+r"(argument),"+r"(missing)::"memory","cc");
        if (!argument && p->chunk_needed) {
            p->chunk_needed=0;
            p->chunk=AROSApplePageAllocate(NULL,p->chunk_bytes,MEMF_CLEAR);
            if (p->chunk) continue;
            ok=0;break;
        }
        if (argument || !missing) { ok=argument!=0;break; }
        if (!r_supply_tables(p->handle,NULL,NULL,missing)) { ok=0;break; }
    }
    /* Storage the handler did not take. */
    if (p->chunk && !AROSApplePageFree(p->chunk,p->chunk_bytes)) for (;;) __asm__ volatile("wfe");
    p->chunk=NULL;return ok;
}
static size_t r_record_size(size_t header,size_t count,size_t element)
{
    if (!header || !element || header>SIZE_MAX-(R_PAGE-1) ||
        count>(SIZE_MAX-(R_PAGE-1)-header)/element) return 0;
    return (header+count*element+R_PAGE-1)&~(size_t)(R_PAGE-1);
}
static int r_reserve_memory(uint64_t handle,uintptr_t address,size_t length)
{
    if (!AROSRestrictedRangeValid(address,length)) return 0;
    size_t count=length/R_PAGE;
    size_t chunk_pages=count<R_CHUNK_PAGES ? count : R_CHUNK_PAGES,chunk_count=(count+chunk_pages-1)/chunk_pages;
    /* The chunk tables; the chunks come later. */
    size_t storage=r_record_size(sizeof(struct RestrictedAllocation),chunk_count,2*sizeof(void *));
    size_t chunk_bytes=r_record_size(sizeof(struct KrnMappingPage),chunk_pages,
        sizeof(struct KrnMappingPage)+sizeof(uintptr_t));
    if (!storage || !chunk_bytes) return 0;
    Forbid();
    struct RestrictedAllocation *a=AROSApplePageAllocate(NULL,storage,MEMF_CLEAR);
    if (!a) { Permit();return 0; }
    a->storage=a;a->storage_views=a->storage_refs=1;
    a->count=count;a->storage_bytes=storage;a->chunk_pages=chunk_pages;a->chunk_count=chunk_count;
    a->chunk_bytes=chunk_bytes;a->pages=(void *)(a+1);a->backing=(void *)(a->pages+chunk_count);
    struct RestrictedMemoryOperation op={.handle=handle,.address=address,.allocation=a};
    int ok=r_memory_call(&op);if (!ok && !AROSApplePageFree(a,storage)) for (;;) __asm__ volatile("wfe");
    Permit();return ok;
}
/* API 34: read+write+execute (7) asks for the D-135 JIT class, admitted only
 * when the kernel has a JIT mode; every other protection keeps W^X. */
static int r_jit_protection(unsigned protection,unsigned *access)
{
    if (protection!=7) { *access=protection;return 1; }
    if (AROSAppleJitMode()==AROS_APPLE_JIT_NONE) return 0;
    *access=KMAP_JIT_CLASS;return 1;
}
/* A cut only changes reservation metadata. Decommit of the removed pages is
 * a separate acknowledged operation; retained PTEs and identities stay live. */
static int r_cut_memory(uint64_t handle,uintptr_t address,size_t length)
{
    if (!AROSRestrictedRangeValid(address,length)) return 0;
    Forbid();
    size_t storage=r_record_size(sizeof(struct RestrictedAllocation),1,1);
    struct RestrictedAllocation *right=AROSApplePageAllocate(NULL,storage,MEMF_CLEAR);
    if (!right) { Permit();return 0; }
    right->storage_bytes=storage;
    struct RestrictedMemoryOperation op={.handle=handle,.address=address,
        .operation=7,.slice=right,.length=length};
    int ok=r_memory_call(&op);
    if (op.slice && !AROSApplePageFree(op.slice,storage)) for (;;) __asm__ volatile("wfe");
    if (ok && op.allocation) r_free_allocation(op.allocation);
    Permit();return ok;
}

static int r_commit_memory(uint64_t handle,uintptr_t address,unsigned protection)
{
    if ((protection&~3U) && protection!=7) return 0;
    if (!r_jit_protection(protection,&protection)) return 0;
    Forbid();void *page=AROSApplePageAllocate(NULL,R_PAGE,MEMF_CLEAR);
    if (!page) { Permit();return 0; }
    struct RestrictedMemoryOperation op={.handle=handle,.address=address,
        .operation=1,.physical=(uintptr_t)page,.protection=protection};
    int ok=r_memory_call(&op);
    if (!ok && !AROSApplePageFree(page,R_PAGE)) for (;;) __asm__ volatile("wfe");
    Permit();return ok;
}
static int r_decommit_memory(uint64_t handle,uintptr_t address)
{
    Forbid();struct RestrictedMemoryOperation op={.handle=handle,.address=address,.operation=2};
    int ok=r_memory_call(&op);
    if (ok && op.physical && !AROSApplePageFree((void *)op.physical,R_PAGE))
        for (;;) __asm__ volatile("wfe");
    Permit();return ok;
}
static int r_protect_memory(uint64_t handle,uintptr_t address,unsigned protection)
{
    if (!r_jit_protection(protection,&protection)) return 0;
    Forbid();struct RestrictedMemoryOperation op={.handle=handle,.address=address,.operation=3,.protection=protection};
    int ok=0;
    uint64_t saved=AROSApplePageMapAcquire();
    uint64_t token=AROSApplePageMutationBegin();
    if (token) {
        publication_context=r_context(r_current());
        if (publication_context) {
            register uintptr_t argument __asm__("x0")=(uintptr_t)&op;
            __asm__ volatile("svc #0xa567":"+r"(argument)::"x1","memory","cc");
            ok=argument!=0;
        }
        publication_context=NULL;
        AROSApplePageMutationEnd(token);
    }
    AROSAppleMemoryLockRelease(&AROSApplePageMapLock,saved);
    Permit();return ok;
}
/* API 36: protect consecutive pages under one rendezvous; the per-page
 * transaction made every page wait for a peer CPU to leave WFI. Stops at
 * the first refused page and returns how many pages changed. */
static size_t r_protect_range(uint64_t handle,uintptr_t address,size_t pages,unsigned protection)
{
    if (!pages || !r_jit_protection(protection,&protection)) return 0;
    size_t done=0;
    Forbid();
    uint64_t saved=AROSApplePageMapAcquire();
    uint64_t token=AROSApplePageMutationBegin();
    if (token) {
        publication_context=r_context(r_current());
        for (;publication_context && done<pages;++done) {
            struct RestrictedMemoryOperation op={.handle=handle,.address=address+done*R_PAGE,.operation=3,.protection=protection};
            register uintptr_t argument __asm__("x0")=(uintptr_t)&op;
            __asm__ volatile("svc #0xa567":"+r"(argument)::"x1","memory","cc");
            if (!argument) break;
        }
        publication_context=NULL;
        AROSApplePageMutationEnd(token);
    }
    AROSAppleMemoryLockRelease(&AROSApplePageMapLock,saved);
    Permit();return done;
}
static int r_release_memory(uint64_t handle,uintptr_t address)
{
    Forbid();struct RestrictedMemoryOperation op={.handle=handle,.address=address,.operation=4};
    int ok=r_memory_call(&op);if (ok) r_free_allocation(op.allocation);
    Permit();return ok;
}

static uint64_t r_create_shared(size_t length)
{
    if (!length || (length&(R_PAGE-1)) || length>AROS_RESTRICTED_DYNAMIC_END-AROS_RESTRICTED_DYNAMIC_BASE) return 0;
    size_t count=length/R_PAGE;
    size_t storage=r_record_size(sizeof(struct RestrictedShared),count,sizeof(uintptr_t));
    if (!storage) return 0;
    Forbid();struct RestrictedShared *b=AROSApplePageAllocate(NULL,storage,MEMF_CLEAR);
    if (!b) { Permit();return 0; }
    b->count=count;b->storage_bytes=storage;b->pages=(void *)(b+1);size_t i;
    for (i=0;i<count;++i) {
        b->pages[i]=(uintptr_t)AROSApplePageAllocate(NULL,R_PAGE,MEMF_CLEAR);
        if (!b->pages[i]) break;
    }
    struct RestrictedSharedOperation op={.operation=0,.record=b};
    uint64_t handle=i==count ? r_shared_call(&op) : 0;
    if (!handle) {
        for (size_t j=0;j<i;++j)
            if (!AROSApplePageFree((void *)b->pages[j],R_PAGE)) for (;;) __asm__ volatile("wfe");
        if (!AROSApplePageFree(b,b->storage_bytes)) for (;;) __asm__ volatile("wfe");
    }
    Permit();return handle;
}
static int r_attach_shared(uint64_t domain,uintptr_t address,uint64_t handle)
{
    Forbid();struct RestrictedSharedOperation op={.operation=1,.handle=handle,.domain=domain,.address=address};
    int ok=r_shared_call(&op)!=0;Permit();return ok;
}
/* API 36: attach object pages [first_page, first_page + reservation pages) */
static int r_attach_shared_at(uint64_t domain,uintptr_t address,uint64_t handle,size_t first_page)
{
    Forbid();struct RestrictedSharedOperation op={.operation=12,.handle=handle,.domain=domain,.address=address,.offset=first_page};
    int ok=r_shared_call(&op)!=0;Permit();return ok;
}
static int r_commit_shared(uint64_t domain,uintptr_t address,unsigned protection)
{
    Forbid();struct RestrictedMemoryOperation op={.handle=domain,.address=address,.operation=5,.protection=protection};
    int ok=r_memory_call(&op);Permit();return ok;
}
static int r_close_shared(uint64_t handle)
{
    Forbid();struct RestrictedSharedOperation op={.operation=2,.handle=handle};
    int ok=r_shared_call(&op)!=0;if (ok) r_release_shared(op.record);
    Permit();return ok;
}

/* API 35: the controller's native view of a shared object, read-write and
 * never executable, in the code window */
static void *r_map_shared(uint64_t handle)
{
    void *address=NULL;
    Forbid();struct RestrictedSharedOperation op={.operation=7,.handle=handle};
    if (r_shared_call(&op)) {
        struct RestrictedShared *b=op.record;
        address=AROSAppleCodeWindowMapForeign(&AROSAppleCodeWindowKernelOps,b->pages,(uint32_t)b->count);
        struct RestrictedSharedOperation done={.operation=address?8:9,.handle=handle,.address=(uintptr_t)address};
        if (!r_shared_call(&done)) for (;;) __asm__ volatile("wfe");
    }
    Permit();return address;
}
static int r_unmap_shared(uint64_t handle)
{
    Forbid();struct RestrictedSharedOperation op={.operation=10,.handle=handle};
    int ok=r_shared_call(&op)!=0;
    if (ok) { op.handle=handle;r_unmap_view(&op); }
    Permit();return ok;
}
static int r_read_shared(uint64_t handle,size_t offset,void *buffer,size_t length)
{
    Forbid();struct RestrictedSharedOperation op={.operation=6,.handle=handle,.offset=offset,.length=length,.buffer=buffer};
    int ok=r_shared_call(&op)!=0;Permit();return ok;
}
static int r_write_shared(uint64_t handle,size_t offset,const void *buffer,size_t length)
{
    Forbid();struct RestrictedSharedOperation op={.operation=5,.handle=handle,.offset=offset,.length=length,.buffer=(void *)buffer};
    int ok=r_shared_call(&op)!=0;Permit();return ok;
}

void AROSAppleRestrictedContextRetired(void *context)
{
    if (!context || !AROSAppleRuntimeLocal()->active || !r_current()) return;
    register uintptr_t argument __asm__("x0")=(uintptr_t)context;
    __asm__ volatile("svc #0xa536":"+r"(argument)::"memory","cc");
    if (!argument) for (;;) __asm__ volatile("wfe");
    r_reap();
}
static int r_take(struct AROSRestrictedWork *work)
{
    r_reap();
    register uintptr_t argument __asm__("x0")=(uintptr_t)work;
    __asm__ volatile("svc #0xa532":"+r"(argument)::"memory","cc");
    return argument!=0;
}
static int r_complete(const struct AROSRestrictedWork *work)
{
    register uintptr_t argument __asm__("x0")=(uintptr_t)work;
    __asm__ volatile("svc #0xa533":"+r"(argument)::"memory","cc");
    int completed=argument!=0;r_reap();
    return completed;
}
static unsigned r_state(const struct AROSRestrictedWork *work)
{
    register uintptr_t argument __asm__("x0")=(uintptr_t)work;
    __asm__ volatile("svc #0xa535":"+r"(argument)::"memory","cc");
    return (unsigned)argument;
}
static int r_wake_thread(uint64_t handle,uint64_t channel)
{
    register uint64_t domain __asm__("x0")=handle;
    register uint64_t thread __asm__("x1")=channel;
    __asm__ volatile("svc #0xa56a":"+r"(domain),"+r"(thread)::"memory","cc");
    return domain!=0;
}
static unsigned r_thread_state(uint64_t handle,uint64_t channel)
{
    register uint64_t domain __asm__("x0")=handle;
    register uint64_t thread __asm__("x1")=channel;
    __asm__ volatile("svc #0xa56b":"+r"(domain),"+r"(thread)::"memory","cc");
    return (unsigned)domain;
}
static int64_t r_suspend_thread(uint64_t handle,uint64_t channel)
{
    register uint64_t domain __asm__("x0")=handle;
    register uint64_t thread __asm__("x1")=channel;
    __asm__ volatile("svc #0xa56c":"+r"(domain),"+r"(thread)::"memory","cc");
    return (int64_t)domain;
}
static int64_t r_resume_thread(uint64_t handle,uint64_t channel)
{
    register uint64_t domain __asm__("x0")=handle;
    register uint64_t thread __asm__("x1")=channel;
    __asm__ volatile("svc #0xa56d":"+r"(domain),"+r"(thread)::"memory","cc");
    return (int64_t)domain;
}
static int r_debug_call(uint64_t handle,uint64_t channel,uint64_t op,uint64_t argument)
{
    register uint64_t domain __asm__("x0")=handle;
    register uint64_t thread __asm__("x1")=channel;
    register uint64_t what __asm__("x2")=op;
    register uint64_t value __asm__("x3")=argument;
    __asm__ volatile("svc #0xa571":"+r"(domain),"+r"(thread),"+r"(what),"+r"(value)::"memory","cc");
    return domain!=0;
}
static int r_debug_domain(uint64_t handle,unsigned flags)
{ return r_debug_call(handle,0,1,flags); }
static int r_debug_context(uint64_t handle,uint64_t channel,struct AROSRestrictedDebugContext *context,int write)
{ return r_debug_call(handle,channel,write ? 3 : 2,(uintptr_t)context); }
static int r_debug_step(uint64_t handle,uint64_t channel,int on)
{ return r_debug_call(handle,channel,4,on!=0); }
static int r_take_domain(uint64_t handle,struct AROSRestrictedWork *work)
{
    r_reap();
    register uint64_t domain __asm__("x0")=handle;
    register uintptr_t request __asm__("x1")=(uintptr_t)work;
    __asm__ volatile("svc #0xa56e":"+r"(domain),"+r"(request)::"memory","cc");
    return domain!=0;
}
static int r_copy_call(uint64_t handle,uintptr_t address,void *buffer,size_t length,unsigned access)
{
    struct RestrictedCopyOperation op={.handle=handle,.address=address,.buffer=buffer,.length=length,.access=access};
    Forbid();
    register uintptr_t argument __asm__("x0")=(uintptr_t)&op;
    __asm__ volatile("svc #0xa56f":"+r"(argument)::"memory","cc");
    Permit();return argument!=0;
}
static int r_read_domain(uint64_t handle,uintptr_t address,void *buffer,size_t length)
{ return r_copy_call(handle,address,buffer,length,KMAP_READ); }
static int r_write_domain(uint64_t handle,uintptr_t address,const void *buffer,size_t length)
{ return r_copy_call(handle,address,(void *)buffer,length,KMAP_WRITE); }
static int r_privatize_memory(uint64_t handle,uintptr_t address)
{
    if (address&(R_PAGE-1)) return 0;
    Forbid();void *page=AROSApplePageAllocate(NULL,R_PAGE,MEMF_CLEAR);
    if (!page) { Permit();return 0; }
    struct RestrictedMemoryOperation op={.handle=handle,.address=address,
        .operation=6,.physical=(uintptr_t)page};
    int ok=r_memory_call(&op);
    if (!ok && !AROSApplePageFree(page,R_PAGE)) for (;;) __asm__ volatile("wfe");
    Permit();return ok;
}
const struct AROSRestrictedAPI AROSRestrictedAPI = {40,sizeof(struct AROSRestrictedAPI),r_run,r_take,r_complete,r_state,r_lifetime,r_create_domain,r_run_domain,r_close_domain,r_reserve_memory,r_commit_memory,r_decommit_memory,r_protect_memory,r_release_memory,r_create_shared,r_attach_shared,r_commit_shared,r_close_shared,r_run_domain_stacks,r_wake_thread,r_thread_state,r_suspend_thread,r_resume_thread,r_read_shared,r_write_shared,r_take_domain,r_map_shared,r_unmap_shared,r_read_domain,r_write_domain,r_attach_shared_at,r_protect_range,r_privatize_memory,r_run_served,r_cut_memory,r_debug_domain,r_debug_context,r_debug_step};
/* One generic mapping implementation in the guest kernel, like the existing
 * page-protection adapter; no host or hardware-specific code is imported. */
#include "../../aarch64-all/kernel/mapping.c"
