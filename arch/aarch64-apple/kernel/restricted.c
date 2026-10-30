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
#include "../../aarch64-all/kernel/mapping.h"

#define R_PAGE 4096U
#define R_TABLES 1024U
#define R_PREPARE 0xa531
#define R_COOKIE UINT64_C(0x7265737472696374)
struct Restricted {
    uint64_t cookie;
    struct Task *owner;
    void *pool, *code, *data, *stack;
    void *native_lower, *native_upper;
    unsigned used;
    uint64_t root, seed;
    struct AROSAppleRuntimeFrame native;
    struct AROSRestrictedResult result;
    struct AROSRestrictedService service;
};
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
    struct Restricted *r = opaque;
    if (bits != 12 || r->used == R_TABLES) return 0;
    return (uintptr_t)r->pool + R_PAGE * r->used++;
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
    struct Restricted *r = opaque;
    /* The new root is not installed while constructing its sole executable
     * mapping. Clean the owned native backing, not the inactive guest VA. */
    (void)va;
    r_clean(opaque, r->code, bytes);
    __asm__ volatile("ic iallu; dsb ish; isb":::"memory");
}
/* Source tables are frozen by the native caller's mapping lock. Every table
 * allocation belongs to one retained slab, including a partial failure. */
static int r_prepare(struct Restricted *r, uint64_t source)
{
    uint64_t tcr, root = 0;
    struct AArch64ProtectionOps ops = {r,r_resolve,r_allocate,r_clean,r_invalidate,r_sync,0};
    __asm__ volatile("mrs %0,tcr_el1":"=r"(tcr));
    if (((tcr >> 14) & 3) ||
        !AArch64ClonePrivilegedRoot(&ops,source,tcr,R_TABLES,&root)) return 0;
    unsigned rw = AARCH64_PROTECT_READ|AARCH64_PROTECT_WRITE|AARCH64_PROTECT_USER;
    if (!AArch64MapPage(&ops,root,tcr,AROS_RESTRICTED_CODE,(uintptr_t)r->code,
            AARCH64_PROTECT_READ|AARCH64_PROTECT_EXEC|AARCH64_PROTECT_USER,1) ||
        !AArch64MapPage(&ops,root,tcr,AROS_RESTRICTED_STACK,(uintptr_t)r->stack,rw,1)) return 0;
    r->root = root;
    return AROSRestrictedServiceInit(&r->service,r->owner,root,tcr,(uintptr_t)r->data,&ops);
}
static void r_finish(struct Restricted *r, struct AROSAppleTaskExecution *execution,
    struct AROSAppleRuntimeFrame *frame, int64_t status)
{
    r->result.status = status;
    r->result.value = *(uint64_t *)r->data;
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
    if (frame->sp <= AROS_RESTRICTED_STACK ||
        frame->sp > AROS_RESTRICTED_STACK + R_PAGE || (frame->sp & 15)) {
        struct Restricted *r = execution->domain;
        if (!r || r->cookie != R_COOKIE || r->owner != task)
            for (;;) __asm__ volatile("wfe");
        r_finish(r,execution,frame,-3);
    }
}
int AROSAppleRestrictedTrap(struct AROSAppleRuntimeFrame *frame)
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
            r->root != execution->root || frame->entry_ttbr0 != r->root)
            for (;;) __asm__ volatile("wfe"); /* Fail closed on kernel corruption. */
        if (ec != 0x15) {
            r_finish(r,execution,frame,-2);
            return 1;
        }
        switch (svc) {
        case AROS_RESTRICTED_YIELD:
            ++r->result.yields;
            frame->x[0] = 0;
            core_SysCall(SC_SWITCH,frame);
            cpu_ApplyCurrentTaskMasks(&frame->spsr);
            return 1;
        case AROS_RESTRICTED_SUBMIT:
            if (AROSRestrictedServiceSubmit(&r->service,frame->x[0],frame->x[1],frame->x[2]))
                frame->x[0]=r->service.request.token;
            else { ++r->result.rejected;frame->x[0]=0; }
            return 1;
        case AROS_RESTRICTED_COLLECT:
            if (AROSRestrictedServiceCollect(&r->service,frame->x[0])) {
                ++r->result.replies;frame->x[0]=1;
            } else {
                frame->x[0]=0;core_SysCall(SC_SWITCH,frame);
                cpu_ApplyCurrentTaskMasks(&frame->spsr);
            }
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
    if (ec == 0x15 && (svc == 0xa532 || svc == 0xa533)) {
        void *work=(void *)(uintptr_t)frame->x[0];
        frame->x[0]=svc==0xa532 ? AROSRestrictedServiceTake(task,work) :
            AROSRestrictedServiceComplete(task,work);
        return 1;
    }
    if (ec != 0x15 || (svc != R_PREPARE && svc != AROS_RESTRICTED_ENTER && svc != 0xa534)) return 0;
    frame->x[1] = 0;
#if defined(__AROSEXEC_SMP__)
    /* Admission stays disabled until task removal and root reclamation are
     * connected to the two-CPU quiescence contract. */
    frame->x[0] = 0;
    return 1;
#else
    r = (void *)(uintptr_t)frame->x[0];
    frame->x[0] = 0;
    if (!r || r->cookie != R_COOKIE || r->owner != task ||
        frame->entry_ttbr0 != AROSAppleRuntimeLocal()->trusted_ttbr0) return 1;
    if (svc == 0xa534) {
        frame->x[0]=AROSRestrictedServiceClose(&r->service);
        return 1;
    }
    if (svc == R_PREPARE) {
        frame->x[0] = r_prepare(r,frame->entry_ttbr0);
        return 1;
    }
    /* The task fields are saved scheduling snapshots. Admission must inspect
     * the live UP Exec nesting state, as the native interrupt path does. */
    if (!r->root || SysBase->IDNestCnt >= 0 || SysBase->TDNestCnt >= 0 ||
        AROSAppleRuntimeLocal()->irq_mask_depth) return 1;
    r->native = *frame;
    r->native_lower = task->tc_SPLower;
    r->native_upper = task->tc_SPUpper;
    task->tc_SPLower = (void *)(uintptr_t)AROS_RESTRICTED_STACK;
    task->tc_SPUpper = (void *)(uintptr_t)(AROS_RESTRICTED_STACK + R_PAGE);
    task->tc_SPReg = task->tc_SPUpper;
    execution->root = r->root;
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
    frame->return_ttbr0 = r->root;
    return 1;
#endif
}
static int r_run(const void *code, size_t bytes, uint64_t seed,
    struct AROSRestrictedResult *result)
{
#if defined(__AROSEXEC_SMP__)
    (void)code; (void)bytes; (void)seed; (void)result;
    return 0;
#else
    if (!code || !bytes || bytes > R_PAGE || !result) return 0;
    struct Restricted *r = AllocMem(sizeof(*r),MEMF_PUBLIC|MEMF_CLEAR);
    if (!r) return 0;
    int ok = 0;
    r->cookie = R_COOKIE; r->owner = FindTask(NULL); r->seed = seed;
    r->pool = AROSApplePageTableAllocate(R_TABLES * R_PAGE);
    r->code = AROSApplePageAllocate(NULL,R_PAGE,MEMF_CLEAR);
    r->data = AROSApplePageAllocate(NULL,R_PAGE,MEMF_CLEAR);
    r->stack = AROSApplePageAllocate(NULL,R_PAGE,MEMF_CLEAR);
    if (!r->pool || !r->code || !r->data || !r->stack) goto cleanup;
    memcpy(r->code,code,bytes);
    *(uint64_t *)r->data = seed;
    if (!AROSApplePageProtect(r->code,R_PAGE,MAP_Readable|MAP_Executable)) goto cleanup;
    uint64_t saved = AROSApplePageMapAcquire();
    register uintptr_t argument __asm__("x0") = (uintptr_t)r;
    __asm__ volatile("svc #0xa531":"+r"(argument)::"x1","memory","cc");
    AROSAppleMemoryLockRelease(&AROSApplePageMapLock,saved);
    if (!argument) goto cleanup;
    argument = (uintptr_t)r;
    __asm__ volatile("svc #0xa530":"+r"(argument)::"x1","memory","cc");
    ok = argument != 0;
    if (ok) *result = r->result;
cleanup:
    /* Native task context owns retries. A cancelled running worker retains its
     * output lease and the entire root/backing until it acknowledges completion. */
    for (;;) {
        register uintptr_t closing __asm__("x0")=(uintptr_t)r;
        __asm__ volatile("svc #0xa534":"+r"(closing)::"x1","memory","cc");
        if (closing) break;
        krnSysCall(SC_SWITCH);
    }
    /* Single-CPU admission only. The return path has flushed all local
     * translations before these pages can be recycled. */
    if (r->code && !AROSApplePageFree(r->code,R_PAGE)) ok = 0;
    if (r->data && !AROSApplePageFree(r->data,R_PAGE)) ok = 0;
    if (r->stack && !AROSApplePageFree(r->stack,R_PAGE)) ok = 0;
    if (r->pool && !AROSApplePageFreeRaw(r->pool,R_TABLES*R_PAGE)) ok = 0;
    r->cookie = 0;
    FreeMem(r,sizeof(*r));
    return ok;
#endif
}
static int r_take(struct AROSRestrictedWork *work)
{
    register uintptr_t argument __asm__("x0")=(uintptr_t)work;
    __asm__ volatile("svc #0xa532":"+r"(argument)::"memory","cc");
    return argument!=0;
}
static int r_complete(const struct AROSRestrictedWork *work)
{
    register uintptr_t argument __asm__("x0")=(uintptr_t)work;
    __asm__ volatile("svc #0xa533":"+r"(argument)::"memory","cc");
    return argument!=0;
}
const struct AROSRestrictedAPI AROSRestrictedAPI = {2,sizeof(struct AROSRestrictedAPI),r_run,r_take,r_complete};
/* One generic mapping implementation in the guest kernel, like the existing
 * page-protection adapter; no host or hardware-specific code is imported. */
#include "../../aarch64-all/kernel/mapping.c"
