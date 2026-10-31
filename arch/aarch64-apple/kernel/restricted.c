/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Experimental restricted-task integration in the shared AArch64 runtime.
 */
#include <aros/config.h>
#include <aros/kernel.h>
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
#define R_TABLES 1024U
#define R_DOMAINS 4U
#define R_THREADS 16U
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
#define R_COOKIE UINT64_C(0x7265737472696374)
enum RestrictedState { R_FREE, R_ALLOCATING, R_LIVE, R_REAPING };
/* Address-space allocation lifetime is separate from an execution context.
 * The legacy run wrapper creates one domain; explicit attachment follows here,
 * rather than making a thread's native continuation own shared root storage. */
struct RestrictedAllocation {
    struct RestrictedAllocation *next;
    struct AROSRestrictedRegion region;
    size_t count;
    struct KrnMappingPage *pages;
    uintptr_t *backing;
};
struct RestrictedDomain {
    enum RestrictedState state;
    void *pool, *code, *data;
    unsigned used, threads, managed, closing;
    void *owner_context, *releaser_context;
    uint64_t root;
    size_t code_bytes, memory_pages;
    struct AROSRestrictedSpace space;
    struct RestrictedAllocation *allocations;
};
struct RestrictedShared {
    struct RestrictedShared *next;
    struct AROSRestrictedSharedBacking backing;
    void *owner_context,*releaser_context;
    uint64_t handle;
    size_t count;
    uintptr_t *pages;
};
static struct RestrictedShared *restricted_shared;
static uint64_t shared_generation;
static void r_shared_lifetime(struct AROSRestrictedLifetime *state)
{
    for (struct RestrictedShared *b=restricted_shared;b;b=b->next) {
        ++state->shared_objects;state->pages+=b->count;
    }
}
struct Restricted {
    struct RestrictedDomain *domain;
    enum RestrictedState state;
    void *releaser_context;
    uint64_t cookie;
    struct Task *owner;
    void *owner_context;
    unsigned abandoned, snapshot_data;
    void *stack, *fault_stack, *tls;
    uint64_t stack_address, fault_address, tls_address;
    void *native_lower, *native_upper;
    unsigned last_cpu;
    uint64_t seed;
    uint64_t callback_pc;
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
static struct Restricted restricted_slots[R_THREADS];
static struct RestrictedDomain restricted_domains[R_DOMAINS];
struct RestrictedPublication {
    struct Restricted *slot;
    void *pool, *code, *data, *stack, *fault_stack, *tls;
    uint64_t seed;
    size_t code_bytes;
};
struct RestrictedDomainPublication {
    struct RestrictedDomain *domain;
    void *pool,*code,*data;
    size_t code_bytes;
};
static int r_prepare_domain(struct RestrictedDomain *,uint64_t);
static int r_unmap_thread(struct Restricted *);
static struct RestrictedDomain *r_domain_slot(uintptr_t address)
{
    uintptr_t start=(uintptr_t)restricted_domains;
    if (address<start || address-start>=sizeof(restricted_domains) ||
        (address-start)%sizeof(struct RestrictedDomain)) return NULL;
    return (void *)address;
}
static struct RestrictedDomain *r_domain_lookup(uint64_t handle)
{
    if (!handle) return NULL;
    for (unsigned i=0;i<R_DOMAINS;++i) {
        struct RestrictedDomain *d=&restricted_domains[i];
        if (d->managed && d->state==R_LIVE && d->space.handle==handle) return d;
    }
    return NULL;
}
static int r_domain_retire(struct RestrictedDomain *d,void *context)
{
    d->closing=1;AROSRestrictedSpaceStop(&d->space);
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
    uintptr_t start=(uintptr_t)restricted_slots;
    if (address<start || address-start>=sizeof(restricted_slots) ||
        (address-start)%sizeof(struct Restricted)) return NULL;
    return (struct Restricted *)address;
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
        if (!handle) for (unsigned i=0;i<R_DOMAINS;++i)
            if (restricted_domains[i].state==R_FREE) { domain=&restricted_domains[i];break; }
        if (!domain) return 1;
        for (unsigned i=0;i<R_THREADS;++i) {
            struct Restricted *r=&restricted_slots[i];
            if (r->state!=R_FREE) continue;
            if (!handle) domain->state=R_ALLOCATING;
            ++domain->threads;r->domain=domain;
            r->stack_address=AROS_RESTRICTED_STACK+i*AROS_RESTRICTED_THREAD_STRIDE;
            r->fault_address=AROS_RESTRICTED_FAULT_STACK+i*AROS_RESTRICTED_THREAD_STRIDE;
            r->tls_address=AROS_RESTRICTED_TLS+i*AROS_RESTRICTED_THREAD_STRIDE;
            r->state=R_ALLOCATING;r->owner=task;r->owner_context=context;
            frame->x[0]=(uintptr_t)r;break;
        }
        return 1;
    }
    if (svc==R_PUBLISH) {
        const struct RestrictedPublication *p=(void *)(uintptr_t)frame->x[0];
        struct Restricted *r=p ? r_slot((uintptr_t)p->slot) : NULL;
        frame->x[0]=0;
        if (!r || r->state!=R_ALLOCATING || r->owner!=task || r->owner_context!=context) return 1;
        if (r->domain->managed) {
            if (p->pool || p->code || p->data || p->code_bytes) return 1;
        } else {
            r->domain->pool=p->pool;r->domain->code=p->code;r->domain->data=p->data;
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
            memset(r->domain,0,sizeof(*r->domain));
        }
        if (r->abandoned) ++owners_reaped;
        memset(r,0,sizeof(*r));frame->x[0]=1;
        return 1;
    }
    if (svc==R_LIFETIME) {
        struct AROSRestrictedLifetime *state=(void *)(uintptr_t)frame->x[0];
        frame->x[0]=0;if (!state) return 1;
        *state=(struct AROSRestrictedLifetime){.retired=owners_retired,.reaped=owners_reaped};
        for (unsigned i=0;i<R_THREADS;++i) {
            struct Restricted *r=&restricted_slots[i];
            if (r->state==R_FREE) continue;
            ++state->live;state->pending+=r->abandoned!=0;
            /* ALLOCATING counts as live even before its backing is published.
             * REAPING retains its accounting until the release acknowledgement. */
            state->pages+=(r->stack!=NULL)+(r->fault_stack!=NULL)+(r->tls!=NULL);
        }
        for (unsigned i=0;i<R_DOMAINS;++i) {
            struct RestrictedDomain *d=&restricted_domains[i];
            if (d->state!=R_FREE) {
                ++state->domains;
                state->pages+=(d->pool ? R_TABLES : 0)+(d->code!=NULL)+(d->data!=NULL)+d->memory_pages;
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
    for (unsigned i=0;i<R_THREADS;++i) {
        struct Restricted *owned=&restricted_slots[i];
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
static int r_stack_sp(const struct Restricted *r, uint64_t sp)
{
    uint64_t base=r->fault_active ? r->fault_address : r->stack_address;
    return sp>base && sp<=base+R_PAGE && !(sp&15);
}
static void r_stack_bounds(struct Restricted *r, int fault)
{
    uintptr_t base=fault ? r->fault_address : r->stack_address;
    r->owner->tc_SPLower=(void *)base;
    r->owner->tc_SPUpper=(void *)(base+R_PAGE);
}
static struct ExceptionContext *r_context(struct Task *task)
{
    return task && (task->tc_Flags & TF_ETASK) && task->tc_UnionETask.tc_ETask ?
        task->tc_UnionETask.tc_ETask->et_RegFrame : NULL;
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
    if (bits != 12 || domain->used == R_TABLES) return 0;
    return (uintptr_t)domain->pool + R_PAGE * domain->used++;
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
static void r_sync(void *opaque, uint64_t va, size_t bytes)
{
    struct RestrictedDomain *domain = opaque;
    /* The new root is not installed while constructing its sole executable
     * mapping. Clean the owned native backing, not the inactive guest VA. */
    (void)va;
    r_clean(opaque, domain->code, bytes);
    __asm__ volatile("ic ialluis; dsb ish; isb":::"memory");
}
/* Source tables are frozen by the native caller's mapping lock. Every table
 * allocation belongs to one retained slab, including a partial failure. */
static int r_prepare_domain(struct RestrictedDomain *d,uint64_t source)
{
    uint64_t tcr,root=0;
    struct AArch64ProtectionOps ops={d,r_resolve,r_allocate,r_clean,r_invalidate,r_sync,0};
    __asm__ volatile("mrs %0,tcr_el1":"=r"(tcr));
    if (d->root || !d->pool || !d->code || !d->data || ((tcr>>14)&3) ||
        !AArch64ClonePrivilegedRoot(&ops,source,tcr,R_TABLES,&root)) return 0;
    d->root=root;
    return AArch64MapPage(&ops,root,tcr,AROS_RESTRICTED_CODE,(uintptr_t)d->code,
        AARCH64_PROTECT_READ|AARCH64_PROTECT_EXEC|AARCH64_PROTECT_USER,1) &&
        AROSRestrictedSpaceInit(&d->space,(uintptr_t)d,root,tcr,(uintptr_t)d->data,&ops);
}
static int r_prepare(struct Restricted *r,uint64_t source)
{
    struct RestrictedDomain *d=r->domain;
    if (d->closing || (!d->managed && !r_prepare_domain(d,source)) || !d->space.handle) return 0;
    unsigned rw=AARCH64_PROTECT_READ|AARCH64_PROTECT_WRITE|AARCH64_PROTECT_USER;
    return AArch64MapPage(&d->space.hardware,d->root,d->space.tcr,r->stack_address,(uintptr_t)r->stack,rw,1) &&
        AArch64MapPage(&d->space.hardware,d->root,d->space.tcr,r->fault_address,(uintptr_t)r->fault_stack,rw,1) &&
        AArch64MapPage(&d->space.hardware,d->root,d->space.tcr,r->tls_address,(uintptr_t)r->tls,rw,1) &&
        AROSRestrictedServiceInit(&r->service,&d->space,(uintptr_t)r->owner);
}
static int r_unmap_thread(struct Restricted *r)
{
    struct RestrictedDomain *d=r->domain;
    if (!d->space.root) return 1; /* Domain construction failed before maps. */
    uintptr_t va[]={r->stack_address,r->fault_address,r->tls_address};
    uintptr_t pa[]={(uintptr_t)r->stack,(uintptr_t)r->fault_stack,(uintptr_t)r->tls};
    for (unsigned i=0;i<3;++i) {
        uint64_t actual;
        if (!AArch64MappingPhysical(&d->space.hardware,d->root,d->space.tcr,va[i],&actual)) continue;
        if (actual!=pa[i] || !AArch64UnmapPage(&d->space.hardware,d->root,d->space.tcr,va[i],pa[i])) return 0;
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
static int r_memory(struct AROSAppleRuntimeFrame *frame,void *context,unsigned svc)
{
    if (svc!=R_MEMORY) return 0;
    struct RestrictedMemoryOperation *p=(void *)(uintptr_t)frame->x[0];
    frame->x[0]=0;if (!p) return 1;
    struct RestrictedDomain *d=r_domain_lookup(p->handle);
    if (!d || d->closing || d->owner_context!=context) return 1;
    if (p->operation==0) {
        struct RestrictedAllocation *a=p->allocation;
        if (a && AROSRestrictedRegionReserve(&d->space,&a->region,p->address,
            a->count*R_PAGE,a->pages,a->backing,a->count)) {
            a->next=d->allocations;d->allocations=a;frame->x[0]=1;
        }
        return 1;
    }
    struct RestrictedAllocation **link=&d->allocations;
    while (*link && !(p->address>=(*link)->region.mapping.start &&
        p->address-(*link)->region.mapping.start<(*link)->region.mapping.length)) link=&(*link)->next;
    struct RestrictedAllocation *a=*link;
    if (!a || (p->address&(R_PAGE-1))) return 1;
    size_t page=(p->address-a->region.mapping.start)/R_PAGE;
    if (p->operation==1) {
        frame->x[0]=AROSRestrictedRegionCommit(&a->region,page,p->physical,p->protection);
        d->memory_pages+=frame->x[0]!=0;
    } else if (p->operation==2) {
        frame->x[0]=AROSRestrictedRegionDecommit(&a->region,page,&p->physical);
        if (frame->x[0] && p->physical) --d->memory_pages;
    } else if (p->operation==3)
        frame->x[0]=AROSRestrictedRegionProtect(&a->region,page,p->protection);
    else if (p->operation==5)
        frame->x[0]=AROSRestrictedRegionCommitShared(&a->region,page,p->protection);
    else if (p->operation==4 && p->address==a->region.mapping.start &&
        AROSRestrictedRegionDetach(&a->region)) {
        for (size_t i=0;i<a->count;++i) d->memory_pages-=a->backing[i]!=0;
        *link=a->next;a->next=NULL;p->allocation=a;frame->x[0]=1;
    }
    return 1;
}
struct RestrictedSharedOperation {
    unsigned operation;
    uint64_t handle,domain;
    uintptr_t address;
    struct RestrictedShared *record;
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
    if (p->operation==1) {
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
        for (unsigned i=0;i<R_DOMAINS;++i) {
            struct RestrictedDomain *d=&restricted_domains[i];
            if (d->state!=R_FREE) continue;
            d->state=R_ALLOCATING;d->managed=1;d->owner_context=context;
            frame->x[0]=(uintptr_t)d;break;
        }
        return 1;
    }
    if (svc==R_DOMAIN_PUBLISH) {
        const struct RestrictedDomainPublication *p=(void *)argument;
        struct RestrictedDomain *d=p ? r_domain_slot((uintptr_t)p->domain) : NULL;
        if (!d || !d->managed || d->state!=R_ALLOCATING || d->owner_context!=context) return 1;
        d->pool=p->pool;d->code=p->code;d->data=p->data;d->code_bytes=p->code_bytes;
        d->state=R_LIVE;frame->x[0]=1;return 1;
    }
    if (svc==R_DOMAIN_REAP) {
        for (unsigned i=0;i<R_DOMAINS;++i) {
            struct RestrictedDomain *d=&restricted_domains[i];
            if (d->managed && d->state==R_LIVE && !d->owner_context &&
                r_domain_retire(d,context)) { frame->x[0]=(uintptr_t)d;break; }
        }
        return 1;
    }
    struct RestrictedDomain *d=svc==R_DOMAIN_CLOSE ? r_domain_lookup(argument) : r_domain_slot(argument);
    if (!d || !d->managed) { frame->x[1]=UINT64_MAX;return 1; }
    if (svc==R_DOMAIN_RELEASED) {
        if (d->state!=R_REAPING || d->releaser_context!=context || d->threads) return 1;
        memset(d,0,sizeof(*d));frame->x[0]=1;return 1;
    }
    if (d->state!=R_LIVE || d->owner_context!=context) { frame->x[1]=UINT64_MAX;return 1; }
    if (svc==R_DOMAIN_PREPARE) {
        if (!d->closing && frame->entry_ttbr0==AROSAppleRuntimeLocal()->trusted_ttbr0 &&
            r_prepare_domain(d,frame->entry_ttbr0)) frame->x[0]=d->space.handle;
    } else if (svc==R_DOMAIN_CLOSE || svc==R_DOMAIN_ABORT) {
        if (r_domain_retire(d,context)) { frame->x[0]=(uintptr_t)d;frame->x[1]=1; }
    }
    return 1;
}
static void r_finish(struct Restricted *r, struct AROSAppleTaskExecution *execution,
    struct AROSAppleRuntimeFrame *frame, int64_t status)
{
    r->callback_active=0;r->fault_active=0;
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
            /* Only owned resident data pages can enter a repair handler. Guards
             * produce level-3 translation faults; RO writes permission faults.
             * No instruction, external, alignment or unowned-address exception
             * supplies a continuation. Bind it to the retained page generation. */
            if (ec==0x24 && (fault==7 || fault==15) && !(frame->esr&(UINT64_C(1)<<10)) &&
                r_code_pc(r,frame->elr) && r_stack_sp(r,frame->sp) &&
                r->fault_handler && !r->fault_active && r->fault_generation!=UINT64_MAX &&
                AROSRestrictedSpacePageIdentity(&r->domain->space,frame->far,0,&physical,&generation,&reservation)) {
                r->fault_physical=physical;r->fault_page_generation=generation;r->fault_reservation=reservation;
                r->fault=*frame;r->fault_active=1;++r->fault_generation;
                frame->elr=r->fault_handler;frame->x[0]=r->fault.far;
                frame->x[1]=r->fault.esr;frame->x[2]=r->fault.elr;
                frame->x[3]=r->fault_generation;frame->x[30]=0;
                frame->sp=r->fault_address+R_PAGE;r_stack_bounds(r,1);
                return 1;
            }
            r_finish(r,execution,frame,-2);
            return 1;
        }
        switch (svc) {
        case AROS_RESTRICTED_YIELD:
            ++r->result.yields;
            frame->x[0] = 0;
            return 2; /* Schedule only after releasing the coordinator. */
        case AROS_RESTRICTED_SUBMIT:
            /* Callback authority is bounded by this immutable executable
             * allocation. An active callback may call the worker again, but
             * cannot recursively install another callback frame. */
            if (frame->x[3] && (r->callback_active || r->fault_active || !r_code_pc(r,frame->x[3]))) {
                ++r->result.rejected;frame->x[0]=0;return 1;
            }
            if (AROSRestrictedServiceSubmit(&r->service,frame->x[0],frame->x[1],frame->x[2]))
                {
                r->callback_pc=frame->x[3];
                frame->x[0]=r->service.request.token;
            }
            else { ++r->result.rejected;frame->x[0]=0; }
            return 1;
        case AROS_RESTRICTED_COLLECT:
            if (AROSRestrictedServiceCollect(&r->service,frame->x[0])) {
                ++r->result.replies;frame->x[0]=1;
                if (r->callback_pc) {
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
            if (frame->x[0]) { r_finish(r,execution,frame,-4);return 1; }
            *frame=r->callback;r->callback_active=0;++r->result.callbacks;
            return 1;
        case AROS_RESTRICTED_FAULT_HANDLER:
            if (r->fault_active || (frame->x[0] && !r_code_pc(r,frame->x[0]))) {
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
                !r_code_pc(r,r->fault.elr) ||
                r->fault.sp<=r->stack_address || r->fault.sp>r->stack_address+R_PAGE ||
                (r->fault.sp&15) ||
                !AROSRestrictedSpacePageIdentity(&r->domain->space,r->fault.far,
                    (r->fault.esr&64) ? KMAP_WRITE : KMAP_READ,&physical,&generation,&reservation) ||
                physical!=r->fault_physical || generation!=r->fault_page_generation || reservation!=r->fault_reservation) {
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
        for (unsigned i=0;i<R_DOMAINS;++i) {
            struct RestrictedDomain *d=&restricted_domains[i];
            if (d->managed && d->state==R_LIVE && d->owner_context==retired) {
                d->owner_context=NULL;d->closing=1;AROSRestrictedSpaceStop(&d->space);
            }
        }
        for (unsigned i=0;i<R_THREADS;++i) {
            struct Restricted *owned=&restricted_slots[i];
            if (owned->state!=R_LIVE || owned->owner_context!=retired) continue;
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
    frame->sp = r->stack_address + R_PAGE;
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
int AROSAppleRestrictedTrap(struct AROSAppleRuntimeFrame *frame)
{
    if (!frame || frame->vector!=8 || (frame->spsr&31)) return 0;
    struct ExceptionContext *context=r_context(r_current());
    if (!context) return 0;
    unsigned ec=(unsigned)(frame->esr>>26)&63,svc=(unsigned)frame->esr&65535;
    if (!AROSAppleTaskExecution(context)->root &&
        (ec!=0x15 || ((svc<AROS_RESTRICTED_ENTER || svc>R_LIFETIME) &&
         (svc<R_DOMAIN_CLAIM || svc>R_SHARED)))) return 0;
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
    FreeVec(a);
}
static void r_free_domain_backing(struct RestrictedDomain *d)
{
    struct RestrictedAllocation *a=d->allocations;
    while (a) {
        struct RestrictedAllocation *next=a->next;r_free_allocation(a);a=next;
    }
    if ((d->code && !AROSApplePageFree(d->code,R_PAGE)) ||
        (d->data && !AROSApplePageFree(d->data,R_PAGE)) ||
        (d->pool && !AROSApplePageFreeRaw(d->pool,R_TABLES*R_PAGE)))
        for (;;) __asm__ volatile("wfe");
}
static void r_release_domain(struct RestrictedDomain *d)
{
    r_free_domain_backing(d);
    register uintptr_t argument __asm__("x0")=(uintptr_t)d;
    __asm__ volatile("svc #0xa564":"+r"(argument)::"x1","memory","cc");
    if (!argument) for (;;) __asm__ volatile("wfe");
}
static void r_release(struct Restricted *r)
{
    /* Native stack only, with scheduling excluded. Local root return completed
     * before this call; worker/domain pins have drained. */
    if ((r->stack && !AROSApplePageFree(r->stack,R_PAGE)) ||
        (r->fault_stack && !AROSApplePageFree(r->fault_stack,R_PAGE)) ||
        (r->tls && !AROSApplePageFree(r->tls,R_PAGE)))
        for (;;) __asm__ volatile("wfe");
    if (!r->domain->managed) r_free_domain_backing(r->domain);
    register uintptr_t released __asm__("x0")=(uintptr_t)r;
    __asm__ volatile("svc #0xa53a":"+r"(released)::"memory","cc");
    if (!released) for (;;) __asm__ volatile("wfe");
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
    FreeVec(b);
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
static int r_run_internal(uint64_t handle,const void *code, size_t bytes, uint64_t seed,
    struct AROSRestrictedResult *result)
{
    if ((!handle && (!code || !bytes || bytes>R_PAGE)) || !result) return 0;
    r_reap();
    /* Page arena allocation/protection never waits. The static control slot
     * avoids Exec AllocMem's low-memory callbacks, which may temporarily Wait.
     * Keep allocation and publication indivisible against external UP RemTask. */
    Forbid();
    register uintptr_t claim __asm__("x0")=handle;
    __asm__ volatile("svc #0xa538":"+r"(claim)::"memory","cc");
    struct Restricted *r=(void *)claim;
    if (!r) { Permit();return 0; }
    int ok = 0;
    struct RestrictedPublication publication={.slot=r,.seed=seed,.code_bytes=bytes};
    if (!handle) {
        publication.pool = AROSApplePageTableAllocate(R_TABLES * R_PAGE);
        publication.code = AROSApplePageAllocate(NULL,R_PAGE,MEMF_CLEAR);
        publication.data = AROSApplePageAllocate(NULL,R_PAGE,MEMF_CLEAR);
    }
    publication.stack = AROSApplePageAllocate(NULL,R_PAGE,MEMF_CLEAR);
    publication.fault_stack = AROSApplePageAllocate(NULL,R_PAGE,MEMF_CLEAR);
    publication.tls = AROSApplePageAllocate(NULL,R_PAGE,MEMF_CLEAR);
    register uintptr_t published __asm__("x0")=(uintptr_t)&publication;
    __asm__ volatile("svc #0xa539":"+r"(published)::"memory","cc");
    if (!published) for (;;) __asm__ volatile("wfe");
    if (!r->domain->pool || !r->domain->code || !r->domain->data || !r->stack || !r->fault_stack || !r->tls) goto prepared;
    if (!handle) {
        memcpy(r->domain->code,code,bytes);
        *(uint64_t *)r->domain->data = seed;
        if (!AROSApplePageProtect(r->domain->code,R_PAGE,MAP_Readable|MAP_Executable)) goto prepared;
    }
    uint64_t saved = AROSApplePageMapAcquire();
    register uintptr_t argument __asm__("x0") = (uintptr_t)r;
    __asm__ volatile("svc #0xa531":"+r"(argument)::"x1","memory","cc");
    AROSAppleMemoryLockRelease(&AROSApplePageMapLock,saved);
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
{ return r_run_internal(0,code,bytes,seed,result); }
static int r_run_domain(uint64_t handle,uint64_t seed,struct AROSRestrictedResult *result)
{ return handle && r_run_internal(handle,NULL,0,seed,result); }
static uint64_t r_create_domain(const void *code,size_t bytes,uint64_t seed)
{
    if (!code || !bytes || bytes>R_PAGE) return 0;
    r_reap();Forbid();
    register uintptr_t claim __asm__("x0")=0;
    __asm__ volatile("svc #0xa560":"+r"(claim)::"x1","memory","cc");
    struct RestrictedDomain *d=(void *)claim;
    if (!d) { Permit();return 0; }
    struct RestrictedDomainPublication publication={.domain=d,.code_bytes=bytes};
    publication.pool=AROSApplePageTableAllocate(R_TABLES*R_PAGE);
    publication.code=AROSApplePageAllocate(NULL,R_PAGE,MEMF_CLEAR);
    publication.data=AROSApplePageAllocate(NULL,R_PAGE,MEMF_CLEAR);
    register uintptr_t argument __asm__("x0")=(uintptr_t)&publication;
    __asm__ volatile("svc #0xa561":"+r"(argument)::"x1","memory","cc");
    if (!argument) for (;;) __asm__ volatile("wfe");
    uint64_t handle=0;
    if (d->pool && d->code && d->data) {
        memcpy(d->code,code,bytes);*(uint64_t *)d->data=seed;
        if (AROSApplePageProtect(d->code,R_PAGE,MAP_Readable|MAP_Executable)) {
            uint64_t saved=AROSApplePageMapAcquire();argument=(uintptr_t)d;
            __asm__ volatile("svc #0xa562":"+r"(argument)::"x1","memory","cc");
            AROSAppleMemoryLockRelease(&AROSApplePageMapLock,saved);handle=argument;
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
    register uintptr_t argument __asm__("x0")=(uintptr_t)p;
    __asm__ volatile("svc #0xa567":"+r"(argument)::"memory","cc");
    return argument!=0;
}
static int r_reserve_memory(uint64_t handle,uintptr_t address,size_t length)
{
    if (!length || (length&(R_PAGE-1)) || address<AROS_RESTRICTED_DYNAMIC_BASE ||
        address>=AROS_RESTRICTED_DYNAMIC_END || length>AROS_RESTRICTED_DYNAMIC_END-address) return 0;
    size_t count=length/R_PAGE;
    if (count>(SIZE_MAX-sizeof(struct RestrictedAllocation))/(sizeof(struct KrnMappingPage)+sizeof(uintptr_t))) return 0;
    Forbid();
    struct RestrictedAllocation *a=AllocVec(sizeof(*a)+count*(sizeof(*a->pages)+sizeof(*a->backing)),MEMF_CLEAR);
    if (!a) { Permit();return 0; }
    a->count=count;a->pages=(void *)(a+1);a->backing=(void *)(a->pages+count);
    struct RestrictedMemoryOperation op={.handle=handle,.address=address,.allocation=a};
    int ok=r_memory_call(&op);if (!ok) FreeVec(a);
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
    int ok=r_memory_call(&op);Permit();return ok;
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
    if (count>(SIZE_MAX-sizeof(struct RestrictedShared))/sizeof(uintptr_t)) return 0;
    Forbid();struct RestrictedShared *b=AllocVec(sizeof(*b)+count*sizeof(uintptr_t),MEMF_CLEAR);
    if (!b) { Permit();return 0; }
    b->count=count;b->pages=(void *)(b+1);size_t i;
    for (i=0;i<count;++i) {
        b->pages[i]=(uintptr_t)AROSApplePageAllocate(NULL,R_PAGE,MEMF_CLEAR);
        if (!b->pages[i]) break;
    }
    struct RestrictedSharedOperation op={.operation=0,.record=b};
    uint64_t handle=i==count ? r_shared_call(&op) : 0;
    if (!handle) {
        for (size_t j=0;j<i;++j)
            if (!AROSApplePageFree((void *)b->pages[j],R_PAGE)) for (;;) __asm__ volatile("wfe");
        FreeVec(b);
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
const struct AROSRestrictedAPI AROSRestrictedAPI = {16,sizeof(struct AROSRestrictedAPI),r_run,r_take,r_complete,r_state,r_lifetime,r_create_domain,r_run_domain,r_close_domain,r_reserve_memory,r_commit_memory,r_decommit_memory,r_protect_memory,r_release_memory,r_create_shared,r_attach_shared,r_commit_shared,r_close_shared};
/* One generic mapping implementation in the guest kernel, like the existing
 * page-protection adapter; no host or hardware-specific code is imported. */
#include "../../aarch64-all/kernel/mapping.c"
