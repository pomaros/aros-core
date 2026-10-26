/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#define __AROSEXEC_SMP__ 1
/* ACTUAL_SOURCE_CONSTANTS */
#define AROS_CPU_IDLE 0
#define AROS_CPU_BUSY 1
#define APPLE_EXIT_REFUSED 0
#define APPLE_EXIT_PUBLISHED 2
struct ExceptionContext { uint64_t x[29],fp,lr,sp,pc,cpsr,Flags; void *fpuContext; };
struct ETask { void *et_RegFrame; };
struct IntETask { int iet_CpuNumber; };
struct Task {
    unsigned tc_Flags; int tc_IDNestCnt; void *tc_SPReg;
    union { struct ETask *tc_ETask; } tc_UnionETask;
    struct IntETask et;
};
typedef void *APTR;
typedef struct { uint64_t x[31],sp,elr,spsr,simd[66]; } regs_t;
static struct Task *current,*destination;
static struct { unsigned irq_ready,fiq_ready,idle_active; } local;
static int idnest,tdnest,jit_mode=AROS_APPLE_JIT_SPRR;
static uintptr_t tpidr;
static unsigned idle_callbacks,writer_callbacks,neutral_writes;
static uint64_t perm_el0_write=0xaaaa,perm_el0_exec=0xbbbb,hardware_table;
static uint64_t AROSAppleKernelIdleMode,AROSAppleKernelIdleEntries,AROSAppleKernelLastDispatchSPSR;
#define TLS_GET(field) (current)
#define TLS_SET(field,value) TLS_SET_##field(value)
#define TLS_SET_ThisTask(value) (current=(value))
#define TLS_SET_IDNestCnt(value) (idnest=(value))
#define TLS_SET_TDNestCnt(value) (tdnest=(value))
#define AROSAppleRuntimeLocal() (&local)
#define GetIntETask(task) (&(task)->et)
static void AROSAArch64WriteTaskPointer(uintptr_t x) { tpidr=x; }
static void AROSPlatformJitRestore(int mode, int writable)
{
    assert(mode==AROS_APPLE_JIT_SPRR);
    uint64_t table=writable ? perm_el0_write : perm_el0_exec;
    if (table==perm_el0_exec) { assert(tpidr==0); ++neutral_writes; }
    hardware_table=table;
}
/* ACTUAL_CONTEXT_AND_JIT */
static int AROSPlatformDefaultIdleMode(void) { return 1; }
static uint64_t AROSPlatformIdleEntry(unsigned mode) { assert(mode==1); return 0x9876; }
static void AROSCPUCurrentState(int state)
{
    if (state==AROS_CPU_IDLE) {
        assert(tpidr==0 && current==NULL && idnest==-1 && tdnest==-1);
        assert(hardware_table==perm_el0_exec);
    } else assert(state==AROS_CPU_BUSY);
}
/* ACTUAL_IDLE_PREFIX */
static void dispatch_contract_halt(void) { abort(); }
static struct Task *core_Dispatch(void) { current=destination; return destination; }
static int GetCPUNumber(void) { return 0; }
static struct ExceptionContext *task_context(struct Task *t) { return context_of(t); }
static void cpu_ApplyTaskMasks(uint64_t *m,int n) { (void)m;(void)n; }
static void AROSAppleTaskTLSRestore(struct ExceptionContext *ctx)
{
    assert(current && ctx==context_of(current));
    assert(hardware_table==perm_el0_write);
    tpidr=0x5678;
}
static int AROSAppleSelfExitDetached(void *departing)
{
    (void)departing;
    if (local.idle_active) {
        assert(hardware_table==perm_el0_exec && tpidr==0);
        ++idle_callbacks;
    } else {
        assert(hardware_table==perm_el0_write && tpidr==0x5678);
        ++writer_callbacks;
    }
    return 1;
}
/* ACTUAL_DISPATCH */
int main(void)
{
    uint64_t simd[66]={0};
    struct ExceptionContext ctx={.sp=4096,.pc=8192,.Flags=ECF_FPU|ECF_JITWRITE,.fpuContext=simd};
    struct ETask et={.et_RegFrame=&ctx};
    struct Task writer={.tc_Flags=TF_ETASK,.tc_UnionETask={.tc_ETask=&et}};
    regs_t regs={0};
    current=&writer; destination=NULL; hardware_table=perm_el0_write; tpidr=0x1234;
    cpu_Dispatch(&regs);
    assert(idle_callbacks==1 && neutral_writes==1 && local.idle_active);
    assert(ctx.Flags & ECF_JITWRITE);
    destination=&writer;
    cpu_Dispatch(&regs);
    assert(writer_callbacks==1 && !local.idle_active && current==&writer);
    assert(ctx.Flags & ECF_JITWRITE);
    assert(regs.sp==4096 && regs.elr==8192 && hardware_table==perm_el0_write);
    /* Actual JitRestore leaves non-SPRR modes unchanged; no fake permission write. */
    const int non_sprr_modes[]={AROS_APPLE_JIT_NONE,AROS_APPLE_JIT_RWX};
    for (unsigned i=0;i<sizeof(non_sprr_modes)/sizeof(non_sprr_modes[0]);++i) {
        jit_mode=non_sprr_modes[i];
        hardware_table=0xcccc; AROSAppleJitRestore(NULL);
        assert(hardware_table==0xcccc && neutral_writes==1);
    }
    puts("PASS actual idle-prefix / dispatcher / JitRestore control flow; platform callback shim only");
}
