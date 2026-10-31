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
#define R_PREPARE 0xa531
#define R_CLAIM 0xa538
#define R_PUBLISH 0xa539
#define R_RELEASED 0xa53a
#define R_LIFETIME 0xa53b
#define R_COOKIE UINT64_C(0x7265737472696374)
enum RestrictedState { R_FREE, R_ALLOCATING, R_LIVE, R_REAPING };
/* Address-space allocation lifetime is separate from an execution context.
 * The legacy run wrapper creates one domain; explicit attachment follows here,
 * rather than making a thread's native continuation own shared root storage. */
struct RestrictedDomain {
    enum RestrictedState state;
    void *pool, *code, *data;
    unsigned used, threads;
    uint64_t root;
    size_t code_bytes;
    struct AROSRestrictedSpace space;
};
struct Restricted {
    struct RestrictedDomain *domain;
    enum RestrictedState state;
    void *releaser_context;
    uint64_t cookie;
    struct Task *owner;
    void *owner_context;
    unsigned abandoned;
    void *stack, *fault_stack;
    void *native_lower, *native_upper;
    unsigned last_cpu;
    uint64_t seed;
    uint64_t callback_pc;
    unsigned callback_active, fault_active;
    uint64_t fault_handler, fault_generation;
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
static struct Restricted restricted_slots[AROS_RESTRICTED_CHANNELS];
static struct RestrictedDomain restricted_domains[KBUF_SPACES];
struct RestrictedPublication {
    struct Restricted *slot;
    void *pool, *code, *data, *stack, *fault_stack;
    uint64_t seed;
    size_t code_bytes;
};
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
        frame->x[0]=0;
        struct RestrictedDomain *domain=NULL;
        for (unsigned i=0;i<KBUF_SPACES;++i)
            if (restricted_domains[i].state==R_FREE) { domain=&restricted_domains[i];break; }
        if (!domain) return 1;
        for (unsigned i=0;i<AROS_RESTRICTED_CHANNELS;++i) {
            struct Restricted *r=&restricted_slots[i];
            if (r->state!=R_FREE) continue;
            domain->state=R_ALLOCATING;domain->threads=1;r->domain=domain;
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
        r->domain->pool=p->pool;r->domain->code=p->code;r->domain->data=p->data;r->stack=p->stack;r->fault_stack=p->fault_stack;
        r->seed=p->seed;r->domain->code_bytes=p->code_bytes;r->cookie=R_COOKIE;
        r->domain->state=R_LIVE;r->state=R_LIVE;frame->x[0]=1;
        return 1;
    }
    if (svc==R_RELEASED) {
        struct Restricted *r=r_slot(frame->x[0]);
        frame->x[0]=0;
        if (!r || r->state!=R_REAPING || r->releaser_context!=context) return 1;
        if (!r->domain || r->domain->state!=R_REAPING || r->domain->threads!=1) return 1;
        if (r->abandoned) ++owners_reaped;
        memset(r->domain,0,sizeof(*r->domain));
        memset(r,0,sizeof(*r));frame->x[0]=1;
        return 1;
    }
    if (svc==R_LIFETIME) {
        struct AROSRestrictedLifetime *state=(void *)(uintptr_t)frame->x[0];
        frame->x[0]=0;if (!state) return 1;
        *state=(struct AROSRestrictedLifetime){.retired=owners_retired,.reaped=owners_reaped};
        for (unsigned i=0;i<AROS_RESTRICTED_CHANNELS;++i) {
            struct Restricted *r=&restricted_slots[i];
            if (r->state==R_FREE) continue;
            ++state->live;state->pending+=r->abandoned!=0;
            /* ALLOCATING counts as live even before its backing is published.
             * REAPING retains its accounting until the release acknowledgement. */
            state->pages+=(r->stack!=NULL)+(r->fault_stack!=NULL);
        }
        for (unsigned i=0;i<KBUF_SPACES;++i) {
            struct RestrictedDomain *d=&restricted_domains[i];
            if (d->state!=R_FREE)
                state->pages+=(d->pool ? R_TABLES : 0)+(d->code!=NULL)+(d->data!=NULL);
        }
        frame->x[0]=1;return 1;
    }
    return 0;
}
static int r_close(struct Restricted *r)
{
    if (!r->domain || r->domain->threads!=1) return 0;
    AROSRestrictedSpaceStop(&r->domain->space);
    if (!AROSRestrictedServiceClose(&r->service) || !AROSRestrictedSpaceClose(&r->domain->space)) return 0;
    r->domain->state=R_REAPING;
    return 1;
}
static struct Restricted *r_claim_reap(struct ExceptionContext *context)
{
    for (unsigned i=0;i<AROS_RESTRICTED_CHANNELS;++i) {
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
    uint64_t base=r->fault_active ? AROS_RESTRICTED_FAULT_STACK : AROS_RESTRICTED_STACK;
    return sp>base && sp<=base+R_PAGE && !(sp&15);
}
static void r_stack_bounds(struct Restricted *r, int fault)
{
    uintptr_t base=fault ? AROS_RESTRICTED_FAULT_STACK : AROS_RESTRICTED_STACK;
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
static int r_prepare(struct Restricted *r, uint64_t source)
{
    uint64_t tcr, root = 0;
    struct AArch64ProtectionOps ops = {r->domain,r_resolve,r_allocate,r_clean,r_invalidate,r_sync,0};
    __asm__ volatile("mrs %0,tcr_el1":"=r"(tcr));
    if (((tcr >> 14) & 3) ||
        !AArch64ClonePrivilegedRoot(&ops,source,tcr,R_TABLES,&root)) return 0;
    unsigned rw = AARCH64_PROTECT_READ|AARCH64_PROTECT_WRITE|AARCH64_PROTECT_USER;
    if (!AArch64MapPage(&ops,root,tcr,AROS_RESTRICTED_CODE,(uintptr_t)r->domain->code,
            AARCH64_PROTECT_READ|AARCH64_PROTECT_EXEC|AARCH64_PROTECT_USER,1) ||
        !AArch64MapPage(&ops,root,tcr,AROS_RESTRICTED_STACK,(uintptr_t)r->stack,rw,1) ||
        !AArch64MapPage(&ops,root,tcr,AROS_RESTRICTED_FAULT_STACK,(uintptr_t)r->fault_stack,rw,1)) return 0;
    r->domain->root = root;
    return AROSRestrictedSpaceInit(&r->domain->space,(uintptr_t)r->domain,root,tcr,(uintptr_t)r->domain->data,&ops) &&
        AROSRestrictedServiceInit(&r->service,&r->domain->space,(uintptr_t)r->owner);
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
    if (!r_stack_sp(r,frame->sp)) {
        /* Only restricted frames reach this lock: native Wait may carry its
         * task lock through cpu_SaveContext. Restricted code cannot call Wait.
         * The current frame has not been published to another CPU yet. */
        r_coordinate();
        r_finish(r,execution,frame,-3);
        r_uncoordinate();
    }
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
#if defined(__AROSEXEC_SMP__)
        unsigned cpu=AROSCPUCurrentIndex();
#else
        unsigned cpu=0;
#endif
        r->result.cpus|=UINT64_C(1)<<cpu;
        if (cpu!=r->last_cpu) { ++r->result.migrations;r->last_cpu=cpu; }
        if (ec != 0x15) {
            uint64_t physical;
            /* Deliver only an owned data-page write-permission fault. The
             * continuation and original registers never come from user memory. */
            if (ec==0x24 && (frame->esr&63)==15 && (frame->esr&64) &&
                frame->far>=AROS_RESTRICTED_DATA && frame->far-AROS_RESTRICTED_DATA<R_PAGE &&
                r_code_pc(r,frame->elr) && r_stack_sp(r,frame->sp) &&
                r->fault_handler && !r->fault_active && r->fault_generation!=UINT64_MAX &&
                AArch64UserPagePhysical(&r->domain->space.hardware,r->domain->root,r->domain->space.tcr,
                    AROS_RESTRICTED_DATA,AARCH64_PROTECT_READ,1,&physical) && physical==(uintptr_t)r->domain->data) {
                r->fault=*frame;r->fault_active=1;++r->fault_generation;
                frame->elr=r->fault_handler;frame->x[0]=r->fault.far;
                frame->x[1]=r->fault.esr;frame->x[2]=r->fault.elr;
                frame->x[3]=r->fault_generation;frame->x[30]=0;
                frame->sp=AROS_RESTRICTED_FAULT_STACK+R_PAGE;r_stack_bounds(r,1);
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
        case AROS_RESTRICTED_FAULT_RESUME: {
            uint64_t physical;
            if (!r->fault_active || frame->x[0]!=r->fault.elr ||
                frame->x[1]!=r->fault_generation || r->service.request.queue ||
                !r_code_pc(r,r->fault.elr) ||
                r->fault.sp<=AROS_RESTRICTED_STACK || r->fault.sp>AROS_RESTRICTED_STACK+R_PAGE ||
                (r->fault.sp&15) ||
                !AArch64UserPagePhysical(&r->domain->space.hardware,r->domain->root,r->domain->space.tcr,
                    AROS_RESTRICTED_DATA,AARCH64_PROTECT_WRITE,1,&physical) || physical!=(uintptr_t)r->domain->data) {
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
        for (unsigned i=0;i<AROS_RESTRICTED_CHANNELS;++i) {
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
    if (!r->domain->root || disabled ||
        AROSAppleRuntimeLocal()->irq_mask_depth) return 1;
    r->last_cpu=cpu;r->result.cpus=UINT64_C(1)<<cpu;
    r->native = *frame;
    r->native_lower = task->tc_SPLower;
    r->native_upper = task->tc_SPUpper;
    task->tc_SPLower = (void *)(uintptr_t)AROS_RESTRICTED_STACK;
    task->tc_SPUpper = (void *)(uintptr_t)(AROS_RESTRICTED_STACK + R_PAGE);
    task->tc_SPReg = task->tc_SPUpper;
    execution->root = r->domain->root;
    execution->user_tls = AROS_RESTRICTED_DATA + 128;
    execution->domain = r;
    AROSAppleRuntimeLocal()->kernel_tls = frame->task_tls;
    memset(frame->x,0,sizeof(frame->x));
    memset(frame->simd,0,sizeof(frame->simd));
    frame->x[0] = r->seed;
    frame->x[1] = AROS_RESTRICTED_DATA;
    frame->x[18] = execution->user_tls;
    frame->elr = AROS_RESTRICTED_CODE;
    frame->sp = AROS_RESTRICTED_STACK + R_PAGE;
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
        (ec!=0x15 || svc<AROS_RESTRICTED_ENTER || svc>R_LIFETIME)) return 0;
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
static void r_release(struct Restricted *r)
{
    /* Native stack only, with scheduling excluded. Local root return completed
     * before this call; worker/domain pins have drained. */
    if ((r->domain->code && !AROSApplePageFree(r->domain->code,R_PAGE)) ||
        (r->domain->data && !AROSApplePageFree(r->domain->data,R_PAGE)) ||
        (r->stack && !AROSApplePageFree(r->stack,R_PAGE)) ||
        (r->fault_stack && !AROSApplePageFree(r->fault_stack,R_PAGE)) ||
        (r->domain->pool && !AROSApplePageFreeRaw(r->domain->pool,R_TABLES*R_PAGE)))
        for (;;) __asm__ volatile("wfe");
    register uintptr_t released __asm__("x0")=(uintptr_t)r;
    __asm__ volatile("svc #0xa53a":"+r"(released)::"memory","cc");
    if (!released) for (;;) __asm__ volatile("wfe");
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
static int r_run(const void *code, size_t bytes, uint64_t seed,
    struct AROSRestrictedResult *result)
{
    if (!code || !bytes || bytes > R_PAGE || !result) return 0;
    r_reap();
    /* Page arena allocation/protection never waits. The static control slot
     * avoids Exec AllocMem's low-memory callbacks, which may temporarily Wait.
     * Keep allocation and publication indivisible against external UP RemTask. */
    Forbid();
    register uintptr_t claim __asm__("x0")=0;
    __asm__ volatile("svc #0xa538":"+r"(claim)::"memory","cc");
    struct Restricted *r=(void *)claim;
    if (!r) { Permit();return 0; }
    int ok = 0;
    struct RestrictedPublication publication={.slot=r,.seed=seed,.code_bytes=bytes};
    publication.pool = AROSApplePageTableAllocate(R_TABLES * R_PAGE);
    publication.code = AROSApplePageAllocate(NULL,R_PAGE,MEMF_CLEAR);
    publication.data = AROSApplePageAllocate(NULL,R_PAGE,MEMF_CLEAR);
    publication.stack = AROSApplePageAllocate(NULL,R_PAGE,MEMF_CLEAR);
    publication.fault_stack = AROSApplePageAllocate(NULL,R_PAGE,MEMF_CLEAR);
    register uintptr_t published __asm__("x0")=(uintptr_t)&publication;
    __asm__ volatile("svc #0xa539":"+r"(published)::"memory","cc");
    if (!published) for (;;) __asm__ volatile("wfe");
    if (!r->domain->pool || !r->domain->code || !r->domain->data || !r->stack || !r->fault_stack) goto prepared;
    memcpy(r->domain->code,code,bytes);
    *(uint64_t *)r->domain->data = seed;
    if (!AROSApplePageProtect(r->domain->code,R_PAGE,MAP_Readable|MAP_Executable)) goto prepared;
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
        memcpy(result->data,r->domain->data,R_PAGE);
    }
    r_release(r);
    Permit();
    return ok;
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
const struct AROSRestrictedAPI AROSRestrictedAPI = {10,sizeof(struct AROSRestrictedAPI),r_run,r_take,r_complete,r_state,r_lifetime};
/* One generic mapping implementation in the guest kernel, like the existing
 * page-protection adapter; no host or hardware-specific code is imported. */
#include "../../aarch64-all/kernel/mapping.c"
