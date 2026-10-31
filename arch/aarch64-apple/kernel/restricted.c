/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Experimental restricted-task integration in the shared AArch64 runtime.
 */
#include <aros/config.h>
#include <aros/kernel.h>
#include <aros/apple/startup.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <proto/exec.h>
#include <string.h>
#include "restricted.h"
#include "restricted_service.h"
#include "task_tls.h"
#include "kernel_cpu.h"
#include "kernel_intr.h"
#include "kernel_syscall.h"
#include "page_allocator.h"
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
#define R_COOKIE UINT64_C(0x7265737472696374)
enum RestrictedState { R_FREE, R_ALLOCATING, R_LIVE, R_REAPING };
/* Address-space allocation lifetime is separate from an execution context.
 * The legacy run wrapper creates one domain; explicit attachment follows here,
 * rather than making a thread's native continuation own shared root storage. */
struct RestrictedAllocation {
    struct RestrictedAllocation *next;
    struct AROSRestrictedRegion region;
    size_t count,storage_bytes;
    struct KrnMappingPage *pages;
    uintptr_t *backing;
};
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
    unsigned threads, managed, closing;
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
    size_t storage_bytes;
    struct RestrictedDomain *domain;
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
    unsigned callback_active, fault_active;
    uint64_t fault_handler, fault_generation, fault_page_generation, fault_reservation;
    uintptr_t fault_physical;
    struct AROSAppleRuntimeFrame fault;
    struct AROSAppleRuntimeFrame callback;
    struct AROSAppleRuntimeFrame native;
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
static uint64_t owners_retired, owners_reaped;
static void r_reap(void);
static void r_coordinate(void);
static void r_uncoordinate(void);
static struct Restricted *r_slot(uintptr_t address)
{
    for (struct Restricted *r=restricted_slots;r;r=r->next)
        if ((uintptr_t)r==address) return r;
    return NULL;
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
    const size_t limit=AROS_RESTRICTED_DYNAMIC_BASE-AROS_RESTRICTED_STACK;
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
    struct Restricted **link=&restricted_slots;
    while (*link && *link!=r) link=&(*link)->next;
    if (*link) *link=r->next;
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
        struct Restricted **link=&restricted_slots;
        while (*link) link=&(*link)->next;
        *link=r;frame->x[0]=(uintptr_t)r;return 1;
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
static int r_stack_sp(const struct Restricted *r, uint64_t sp)
{
    uint64_t base=r->fault_active ? r->fault_address : r->stack_address;
    size_t bytes=r->fault_active ? r->fault_bytes : r->stack_bytes;
    return sp>base && sp<=base+bytes && !(sp&15);
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
        !r_stack_sp(r,context.sp) || !r_capture_code(r,context.pc,&code)) return 0;
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
        if (!AROSAppleRestrictedSetSuspend(r->owner,r->owner_context,
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
static void r_invalidate(void *unused)
{ (void)unused; __asm__ volatile("dsb ish; tlbi vmalle1is; dsb ish; isb":::"memory"); }
static const void *r_execution_alias(struct RestrictedDomain *d,uint64_t va,size_t bytes)
{
    if (!bytes) return NULL;
    if (va>=AROS_RESTRICTED_CODE) {
        size_t storage=r_code_storage(d->code_bytes);
        if (!d->code || va-AROS_RESTRICTED_CODE>=storage ||
            bytes>storage-(va-AROS_RESTRICTED_CODE)) return NULL;
        return (const unsigned char *)d->code+(va-AROS_RESTRICTED_CODE);
    }
    for (struct RestrictedAllocation *a=d->allocations;a;a=a->next) {
        if (va<a->region.mapping.start || va-a->region.mapping.start>=a->region.mapping.length) continue;
        size_t offset=va-a->region.mapping.start,page=offset/R_PAGE,in_page=offset%R_PAGE;
        if (page>=a->count || bytes>R_PAGE-in_page ||
            !a->backing[page] || a->pages[page].physical!=a->backing[page] ||
            (a->region.shared && (page>=a->region.shared->count ||
             a->region.shared->pages[page]!=a->backing[page]))) return NULL;
        return (const unsigned char *)a->backing[page]+in_page;
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
            if (!AArch64MapPage(&d->space.hardware,d->root,d->space.tcr,
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
    struct RestrictedAllocation *allocation;
};
/* Set only by the native publication wrapper while PageMapLock and the
 * remote rendezvous are held. Scalar payload SVCs cannot set this identity. */
static void *publication_context;
static int r_publish_native(void *context,uintptr_t physical,unsigned protection)
{ (void)context;return AROSApplePageProtectArenaLocked(physical,protection); }
static const struct AROSRestrictedPublicationOps publication_ops={NULL,r_publish_native};
/* Only evict an inherited low leaf in this domain's private table clone.
 * Owned pages (including guards) are handled by RegionCommit itself. No native
 * physical backing is released here; the trusted identity map remains intact. */
static int r_prepare_low_page(struct RestrictedDomain *d,
    struct RestrictedAllocation *a, size_t page, uintptr_t address)
{
    if (address >= AROS_RESTRICTED_LOW_END || a->backing[page]) return 1;
    if (!AROSAppleKernelTransitionRangeAvailable(address, R_PAGE)) return 0;
    uint64_t physical;
    if (!AArch64MappingPhysical(&d->space.hardware,d->root,d->space.tcr,address,&physical))
        return 1; /* Missing or unsupported mapping: MapPage still fails closed. */
    return AArch64UnmapPrivilegedPage(&d->space.hardware,d->root,
        d->space.tcr,address,physical);
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
        if (a && (p->address >= AROS_RESTRICTED_LOW_END ||
            AROSAppleKernelTransitionRangeAvailable(p->address,a->count*R_PAGE)) &&
            AROSRestrictedRegionReserve(&d->space,&a->region,p->address,
            a->count*R_PAGE,a->pages,a->backing,a->count)) {
            a->next=d->allocations;d->allocations=a;d->memory_pages+=a->storage_bytes/R_PAGE;
            d->memory_metadata_pages+=a->storage_bytes/R_PAGE;frame->x[0]=1;
        }
        return 1;
    }
    struct RestrictedAllocation **link=&d->allocations;
    while (*link && !(p->address>=(*link)->region.mapping.start &&
        p->address-(*link)->region.mapping.start<(*link)->region.mapping.length)) link=&(*link)->next;
    struct RestrictedAllocation *a=*link;
    if (!a || (p->address&(R_PAGE-1))) return 1;
    size_t page=(p->address-a->region.mapping.start)/R_PAGE;
    if ((p->operation==1 || p->operation==5) && !r_tables_need(d,3)) {
        frame->x[1]=d->table_shortage;return 1;
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
        if ((p->protection&KMAP_EXEC) || a->region.publication_pending ||
            (a->pages[page].protection&KMAP_EXEC)) {
            if (publication_context && publication_context==context)
                frame->x[0]=AROSRestrictedRegionPublish(&a->region,page,p->protection,&publication_ops);
        } else frame->x[0]=AROSRestrictedRegionProtect(&a->region,page,p->protection);
    } else if (p->operation==5)
        frame->x[0]=AROSRestrictedRegionCommitShared(&a->region,page,p->protection);
    else if (p->operation==4 && p->address==a->region.mapping.start &&
        AROSRestrictedRegionDetach(&a->region)) {
        for (size_t i=0;i<a->count;++i) d->memory_pages-=a->backing[i]!=0;
        d->memory_pages-=a->storage_bytes/R_PAGE;d->memory_metadata_pages-=a->storage_bytes/R_PAGE;
        *link=a->next;a->next=NULL;p->allocation=a;frame->x[0]=1;
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
    } else {
        while (*link && (*link)->handle!=p->handle) link=&(*link)->next;
    }
    struct RestrictedShared *b=*link;
    if (!b) return 1;
    if (p->operation==3) {
        if (b->releaser_context==context && b->backing.pages==NULL) {
            *link=b->next;frame->x[0]=1;
        }
        return 1;
    }
    if (b->releaser_context || (p->operation!=4 && b->owner_context!=context)) return 1;
    if (p->operation==5 || p->operation==6) {
        frame->x[0]=p->operation==5 ?
            AROSRestrictedSharedWrite(&b->backing,p->offset,p->buffer,p->length) :
            AROSRestrictedSharedRead(&b->backing,p->offset,p->buffer,p->length);
    } else if (p->operation==1) {
        struct RestrictedDomain *d=r_domain_lookup(p->domain);
        if (!d || d->closing || d->owner_context!=context) return 1;
        for (struct RestrictedAllocation *a=d->allocations;a;a=a->next)
            if (a->region.mapping.start==p->address) {
                frame->x[0]=AROSRestrictedRegionAttachShared(&a->region,&b->backing);break;
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
    r->callback_active=0;r->fault_active=0;r->suspend_count=0;
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
    else r_deliver_wait(r,frame);
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
            /* Owned resident or reserved data pages can enter a repair handler.
             * Missing tables/pages produce translation faults; RO writes permission faults.
             * No instruction, external, alignment or unowned-address exception
             * supplies a continuation. Bind it to the retained page generation. */
            if (ec==0x24 && ((fault>=4 && fault<=7) || fault==15) && !(frame->esr&(UINT64_C(1)<<10)) &&
                r_capture_code(r,frame->elr,&r->fault_code) && r_stack_sp(r,frame->sp) &&
                r->fault_handler && r_match_code(r,&r->handler_code) &&
                !r->fault_active && r->fault_generation!=UINT64_MAX &&
                AROSRestrictedSpaceFaultIdentity(&r->domain->space,frame->far,&physical,&generation,&reservation)) {
                r->fault_physical=physical;r->fault_page_generation=generation;r->fault_reservation=reservation;
                r->fault=*frame;r->fault_active=1;++r->fault_generation;
                frame->elr=r->fault_handler;frame->x[0]=r->fault.far;
                frame->x[1]=r->fault.esr;frame->x[2]=r->fault.elr;
                frame->x[3]=r->fault_generation;frame->x[30]=0;
                frame->sp=r->fault_address+r->fault_bytes;r_stack_bounds(r,1);
                return 1;
            }
            r_finish(r,execution,frame,-2);
            return 1;
        }
        switch (svc) {
        case AROS_RESTRICTED_RESTORE_CONTEXT:
            if (!r_restore_context(r,frame)) {
                ++r->result.rejected;frame->x[0]=0;
            }
            return 1;
        case AROS_RESTRICTED_THREAD_INFO:
            frame->x[0]=r->service.channel;frame->x[1]=r->wake_generation;
            return 1;
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
                !r_match_code(r,&r->fault_code) ||
                r->fault.sp<=r->stack_address || r->fault.sp>r->stack_address+r->stack_bytes ||
                (r->fault.sp&15) ||
                !AROSRestrictedSpacePageIdentity(&r->domain->space,r->fault.far,
                    (r->fault.esr&64) ? KMAP_WRITE : KMAP_READ,&physical,&generation,&reservation) ||
                reservation!=r->fault_reservation ||
                (r->fault_physical ? (physical!=r->fault_physical || generation!=r->fault_page_generation) :
                 (r->fault_page_generation==UINT64_MAX || generation!=r->fault_page_generation+1))) {
                ++r->result.denied;frame->x[0]=UINT64_MAX;return 1;
            }
            *frame=r->fault;r->fault_active=0;r_stack_bounds(r,0);++r->result.resumed;
            return 1;
        }
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
        for (struct RestrictedShared *b=restricted_shared;b;b=b->next)
            if (b->owner_context==retired && !b->releaser_context) {
                b->owner_context=NULL;b->backing.closing=1;
            }
        for (struct RestrictedDomain *d=restricted_domains;d;d=d->next) {
            if (d->managed && d->state==R_LIVE && d->owner_context==retired) {
                d->owner_context=NULL;d->closing=1;AROSRestrictedSpaceStop(&d->space);
                r_cancel_waiters(d);
            }
        }
        for (struct Restricted *owned=restricted_slots;owned;owned=owned->next) {
            if (owned->state!=R_LIVE || owned->owner_context!=retired) continue;
            r_clear_wait(owned);owned->suspend_count=0;
            owned->owner_context=NULL;owned->abandoned=1;++owners_retired;
            /* Never dereference the removed Task after this hook. Service
             * principal/member identities are opaque until their drain. */
            owned->owner=NULL;
            r_close(owned);
        }
        return 1;
    }
    if (ec==0x15 && svc==0xa537) {
        frame->x[0]=(uintptr_t)r_claim_reap(context);
        return 1;
    }
    if (ec==0x15 && (svc==R_WAKE || svc==R_THREAD_STATE || svc==R_SUSPEND || svc==R_RESUME)) {
        struct RestrictedDomain *d=r_domain_lookup(frame->x[0]);
        uint64_t channel=frame->x[1];
        frame->x[0]=(svc==R_SUSPEND || svc==R_RESUME) ? UINT64_MAX : 0;
        if (d && d->owner_context==context) {
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
    if (ec==0x15 && r_table_transfer(frame,context,svc)) return 1;
    if (ec==0x15 && r_shared_operation(frame,context,svc)) return 1;
    if (ec==0x15 && r_memory(frame,context,svc)) return 1;
    if (ec==0x15 && r_domain_lifecycle(frame,context,svc)) return 1;
    if (ec==0x15 && r_lifecycle(frame,task,context,svc)) return 1;
    if (ec != 0x15 || (svc != R_PREPARE && svc != AROS_RESTRICTED_ENTER && svc != 0xa534)) return 0;
    frame->x[1] = 0;
    r = r_slot(frame->x[0]);
    frame->x[0] = 0;
    if (!r || r->state!=R_LIVE || r->cookie != R_COOKIE || r->owner != task ||
        frame->entry_ttbr0 != AROSAppleRuntimeLocal()->trusted_ttbr0) return 1;
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
int AROSAppleRestrictedProtectAliasesLocked(uintptr_t address,uint64_t tcr,unsigned permissions)
{
    /* The arena is pre-split in every snapshot; no table allocation is needed.
     * Other native mapping regions require their own mirror/split protocol. */
    if (!AROSApplePageArenaContains(address)) return 1;
    unsigned policy=permissions & (AARCH64_PROTECT_READ|AARCH64_PROTECT_WRITE|AARCH64_PROTECT_EXEC);
    /* A writable user/JIT source is implicitly EL1-XN, as in root cloning. */
    if (policy & AARCH64_PROTECT_WRITE) policy&=~AARCH64_PROTECT_EXEC;
    const struct AArch64ProtectionOps ops={NULL,r_resolve,r_alias_no_allocate,r_clean,r_invalidate,r_alias_sync,NULL};
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
int AROSAppleRestrictedTrap(struct AROSAppleRuntimeFrame *frame)
{
    if (!frame || frame->vector!=8 || (frame->spsr&31)) return 0;
    struct ExceptionContext *context=r_context(r_current());
    if (!context) return 0;
    unsigned ec=(unsigned)(frame->esr>>26)&63,svc=(unsigned)frame->esr&65535;
    if (!AROSAppleTaskExecution(context)->root &&
        (ec!=0x15 || ((svc<AROS_RESTRICTED_ENTER || svc>R_LIFETIME) &&
         (svc<R_DOMAIN_CLAIM || svc>R_RESUME)))) return 0;
    r_coordinate();
    int handled=r_trap_locked(frame);
    r_uncoordinate();
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
    for (size_t i=0;i<a->count;++i)
        if (a->backing[i] && !AROSApplePageFree((void *)a->backing[i],R_PAGE))
            for (;;) __asm__ volatile("wfe");
    if (!AROSApplePageFree(a,a->storage_bytes)) for (;;) __asm__ volatile("wfe");
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
static void r_reap(void)
{
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
    for (;;) {
        struct RestrictedSharedOperation op={.operation=4};
        if (!r_shared_call(&op)) break;
        r_release_shared(op.record);
    }
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
    struct AROSRestrictedResult *result)
{
    if ((!handle && (!code || !r_code_storage(bytes))) || !result) return 0;
    /* Validate sizes without consulting the mutable published list. */
    const size_t limit=AROS_RESTRICTED_DYNAMIC_BASE-AROS_RESTRICTED_STACK;
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
{ return r_run_internal(0,code,bytes,seed,R_PAGE,R_PAGE,result); }
static int r_run_domain(uint64_t handle,uint64_t seed,struct AROSRestrictedResult *result)
{ return handle && r_run_internal(handle,NULL,0,seed,R_PAGE,R_PAGE,result); }
static int r_run_domain_stacks(uint64_t handle,uint64_t seed,size_t stack_bytes,
    size_t fault_bytes,struct AROSRestrictedResult *result)
{ return handle && r_run_internal(handle,NULL,0,seed,stack_bytes,fault_bytes,result); }
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
    for (;;) {
        register uintptr_t argument __asm__("x0")=(uintptr_t)p;
        register uintptr_t missing __asm__("x1")=0;
        __asm__ volatile("svc #0xa567":"+r"(argument),"+r"(missing)::"memory","cc");
        if (argument || !missing) return argument!=0;
        if (!r_supply_tables(p->handle,NULL,NULL,missing)) return 0;
    }
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
    size_t storage=r_record_size(sizeof(struct RestrictedAllocation),count,sizeof(struct KrnMappingPage)+sizeof(uintptr_t));
    if (!storage) return 0;
    Forbid();
    struct RestrictedAllocation *a=AROSApplePageAllocate(NULL,storage,MEMF_CLEAR);
    if (!a) { Permit();return 0; }
    a->count=count;a->storage_bytes=storage;a->pages=(void *)(a+1);a->backing=(void *)(a->pages+count);
    struct RestrictedMemoryOperation op={.handle=handle,.address=address,.allocation=a};
    int ok=r_memory_call(&op);if (!ok && !AROSApplePageFree(a,storage)) for (;;) __asm__ volatile("wfe");
    Permit();return ok;
}
static int r_commit_memory(uint64_t handle,uintptr_t address,unsigned protection)
{
    if (protection&~3U) return 0;
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
const struct AROSRestrictedAPI AROSRestrictedAPI = {31,sizeof(struct AROSRestrictedAPI),r_run,r_take,r_complete,r_state,r_lifetime,r_create_domain,r_run_domain,r_close_domain,r_reserve_memory,r_commit_memory,r_decommit_memory,r_protect_memory,r_release_memory,r_create_shared,r_attach_shared,r_commit_shared,r_close_shared,r_run_domain_stacks,r_wake_thread,r_thread_state,r_suspend_thread,r_resume_thread,r_read_shared,r_write_shared};
/* One generic mapping implementation in the guest kernel, like the existing
 * page-protection adapter; no host or hardware-specific code is imported. */
#include "../../aarch64-all/kernel/mapping.c"
