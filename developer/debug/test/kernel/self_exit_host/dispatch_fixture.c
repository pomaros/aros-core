/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "self_exit.h"
typedef void *APTR;
#define __AROSEXEC_SMP__ 1
#define ECF_FPU 1
#define TASK_SPSR_ALLOWED UINT64_MAX
#define AROS_CPU_BUSY 1
#define TLS_GET(field) (current)
struct ExceptionContext {
    uint64_t x[29], fp, lr, sp, pc, cpsr, Flags;
    void *fpuContext;
};
struct IntETask { int iet_CpuNumber; };
struct Task { int tc_IDNestCnt; void *tc_SPReg; struct IntETask et; struct ExceptionContext ctx; };
typedef struct { uint64_t x[31], sp, elr, spsr, simd[66]; } regs_t;
static struct Task *current, *destination;
static struct AppleSelfExit slot;
static struct { unsigned idle_active; } local;
static uint64_t AROSAppleKernelLastDispatchSPSR;
static unsigned phase, published, dispatches, wake_service;
static struct Task *service;
static int base;
static void dispatch_contract_halt(void) { abort(); }
static struct Task *core_Dispatch(void)
{
    assert(phase==0 || (phase==3 && current==0));
    assert(++dispatches<=2);
    phase=1; current=destination; return destination;
}
static void enter_idle(regs_t *regs) { assert(phase==1); phase=3; current=0; regs->sp=0; local.idle_active=1; }
#define AROSAppleRuntimeLocal() (&local)
#define GetIntETask(task) (&(task)->et)
static int GetCPUNumber(void) { return 0; }
static void AROSCPUCurrentState(int state) { assert(state==AROS_CPU_BUSY); }
static struct ExceptionContext *task_context(struct Task *task) { return &task->ctx; }
static void cpu_ApplyTaskMasks(uint64_t *mask, int nesting) { (void)mask; (void)nesting; }
static void AROSAppleJitRestore(struct Task *task) { assert(task==destination && phase==1); phase=2; }
static void AROSAppleTaskTLSRestore(struct ExceptionContext *ctx) { assert(ctx==&destination->ctx && phase==2); phase=3; }
static int AROSAppleSelfExitDetached(void *departing) { assert(phase==3); return AppleSelfExitDetached(&slot,0,departing); }
/* ACTUAL_DISPATCH_BODY */
static void publish(void *task, void *context)
{
    assert(context==&base && phase==3 && current==destination);
    assert(!current || (current->tc_SPReg==(void *)(uintptr_t)current->ctx.sp));
    ++published;
    free(task);
    if (wake_service) destination=service;
}
int main(void)
{
    uint64_t simd[66]={0};
    struct Task next={0};
    regs_t regs={0};
    next.ctx.sp=4096; next.ctx.pc=8192; next.ctx.Flags=ECF_FPU; next.ctx.fpuContext=simd;
    service=&next;
    /* 0: normal destination, 1: no eligible local service, 2: local service
     * becomes ready only inside publication, 3: ordinary empty-slot idle. */
    const unsigned order[]={2,0,1,3};
    for (unsigned which=0; which<4; ++which) {
        unsigned mode=order[which];
        unsigned before=published;
        current=mode==3 ? 0 : calloc(1,sizeof(*current));
        if (mode!=3) {
            assert(current);
            assert(AppleSelfExitQueue(&slot,0,current,&base,publish));
        }
        destination=mode==0 ? &next : 0;
        phase=0; dispatches=0; wake_service=mode==2;
        cpu_Dispatch(&regs);
        assert(published==before+(mode!=3));
        if (mode==1 || mode==3) assert(local.idle_active && !current);
        else assert(current==&next && !local.idle_active && regs.sp==4096 && regs.elr==8192);
        assert(dispatches==((mode==1 || mode==2) ? 2U : 1U));
    }
    puts("PASS actual dispatch normal/idle/ready-after-publication/empty bounds");
}
