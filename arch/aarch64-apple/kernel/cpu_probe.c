/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Isolated secondary-CPU bring-up, NOT an Exec SMP scheduler.
 * The PSCI register ABI follows the upstream bsek CPU_ON implementation;
 * no Raspberry Pi release registers or platform assumptions are reused. */
#include "cpu_monitor.h"
#include "kernel_intern.h"
#include <aros/apple/runtime.h>
#if defined(AROS_VM_GUEST) && AROS_VM_GUEST
#include "gic.h"
#include "vm_irq.h"
static struct AROSGIC secondary_gic;
static struct AROSGIC *boot_gic;
#endif
#define PROBE_REQUEST_SGI 14U
#define PROBE_REPLY_SGI 15U
static uint32_t ipi_sent,ipi_handled,ipi_acked,ipi_fault;
static struct { uint64_t sp,ttbr0,ttbr1,tcr,mair,sctlr; } handoff;
static _Alignas(64) unsigned char stack[16384];
static uint32_t command,ready,probe_cpu=AROS_CPU_MAX;
static uint64_t target;
static unsigned hvc;
extern void AROSCPUProbeEntry(void),AROSCPUProbeEntryEnd(void);
static int64_t psci(uint64_t fn,uint64_t a,uint64_t b,uint64_t c)
{
    register uint64_t x0 __asm__("x0")=fn,x1 __asm__("x1")=a;
    register uint64_t x2 __asm__("x2")=b,x3 __asm__("x3")=c;
#define CALL(insn) __asm__ volatile(insn : "+r"(x0),"+r"(x1),"+r"(x2),"+r"(x3) :: \
    "x4","x5","x6","x7","x8","x9","x10","x11","x12","x13","x14","x15","x16","x17","memory")
    if (hvc) CALL("hvc #0"); else CALL("smc #0");
    return (int64_t)x0;
}
static uint64_t now(void) { uint64_t v;__asm__ volatile("isb\n\tmrs %0,cntvct_el0":"=r"(v));return v; }
static void clean(uintptr_t start,uintptr_t end)
{
    /* 16 is the architectural minimum data-cache line size. */
    for (uintptr_t p=start&~(uintptr_t)15;p<end;p+=16) __asm__ volatile("dc cvac,%0"::"r"(p):"memory");
    __asm__ volatile("dsb sy":::"memory");
}
int AROSCPUProbeHandleIPI(uint32_t id)
{
    if (id!=PROBE_REPLY_SGI || probe_cpu==AROS_CPU_MAX ||
        __atomic_load_n(&ready,__ATOMIC_ACQUIRE)!=1) return 0;
    __atomic_add_fetch(&ipi_acked,1,__ATOMIC_RELAXED);
    return 1;
}
void AROSCPUProbeIPICounters(uint32_t *sent,uint32_t *handled,uint32_t *acked)
{
    *sent=__atomic_load_n(&ipi_sent,__ATOMIC_RELAXED);
    *handled=__atomic_load_n(&ipi_handled,__ATOMIC_ACQUIRE);
    *acked=__atomic_load_n(&ipi_acked,__ATOMIC_RELAXED);
}
/* Private secondary IRQ entry saves all GPRs. No Exec calls or SIMD use. */
void AROSCPUProbeHandleIRQ(void)
{
#if defined(AROS_VM_GUEST) && AROS_VM_GUEST
    AROSCPUCurrentState(AROS_CPU_BUSY);
    uint32_t token=AROSGICAcknowledge(&secondary_gic),id=token&0x3ff;
    if (id<1020) {
        if (id==PROBE_REQUEST_SGI) {
            __atomic_add_fetch(&ipi_handled,1,__ATOMIC_RELEASE);
            if (!AROSGICSendIPI(&secondary_gic,PROBE_REPLY_SGI,boot_gic->affinity))
                __atomic_store_n(&ipi_fault,1,__ATOMIC_RELEASE);
        } else {
            __atomic_store_n(&ipi_fault,id+1,__ATOMIC_RELEASE);
            AROSGICDisable(&secondary_gic,id);
        }
        AROSGICEnd(&secondary_gic,token);
    }
    AROSCPUCurrentState(__atomic_load_n(&command,__ATOMIC_ACQUIRE)==1?AROS_CPU_BUSY:AROS_CPU_IDLE);
#endif
}
static uintptr_t send_request(void)
{
#if defined(AROS_VM_GUEST) && AROS_VM_GUEST
    if (!boot_gic || !AROSGICSendIPI(boot_gic,PROBE_REQUEST_SGI,target)) return 0;
    __atomic_add_fetch(&ipi_sent,1,__ATOMIC_RELAXED);
#endif
    return 1;
}
static __attribute__((noreturn)) void probe_start_failed(void)
{
    __atomic_store_n(&ready,2,__ATOMIC_RELEASE);
    /* Retain the kernel unless AFFINITY_INFO confirms that this CPU is off. */
    (void)psci(UINT64_C(0x84000002),0,0,0);
    for (;;) __asm__ volatile("wfe");
}
void AROSCPUProbeMain(void)
{
    uint64_t actual;__asm__ volatile("mrs %0,mpidr_el1":"=r"(actual));
    if ((actual&AROS_CPU_AFFINITY_MASK)!=target || !AROSCPUBindCurrent()) {
        probe_start_failed();
    }
    struct AROSAppleRuntimeLocal *local=AROSAppleRuntimeLocal();
    if (AROSCPUCurrentIndex()!=probe_cpu || AROSCPUCurrentExecIndex()!=probe_cpu ||
        local->irq_mask_depth || local->active || local->irq_ready ||
        local->fiq_ready || local->interrupt_depth || local->transition_active ||
        local->return_pc || local->proof || local->idle_active) {
        probe_start_failed();
    }
#if defined(AROS_VM_GUEST) && AROS_VM_GUEST
    const void *blob;uint64_t bytes;struct AROSEarlyFDT tree;
    if (!boot_gic || !AROSAppleKernelRetainedDeviceTree(&blob,&bytes) ||
        !AROSEarlyFDTOpen(&tree,blob,bytes) ||
        !AROSGICInitCPU(&secondary_gic,&tree,boot_gic->node) ||
        !AROSGICClearPending(&secondary_gic,PROBE_REQUEST_SGI) ||
        !AROSGICEnable(&secondary_gic,PROBE_REQUEST_SGI,1)) {
        probe_start_failed();
    }
#endif
    AROSCPUCurrentState(AROS_CPU_IDLE);
    __atomic_store_n(&ready,1,__ATOMIC_RELEASE);
#if defined(AROS_VM_GUEST) && AROS_VM_GUEST
    __asm__ volatile("msr daifclr,#2":::"memory");
#endif
    unsigned previous=0;
    for (;;) {
        unsigned cmd=__atomic_load_n(&command,__ATOMIC_ACQUIRE);
        if (cmd==3) {
            __asm__ volatile("msr daifset,#3":::"memory");
#if defined(AROS_VM_GUEST) && AROS_VM_GUEST
            AROSGICDisable(&secondary_gic,PROBE_REQUEST_SGI);
            AROSGICClearPending(&secondary_gic,PROBE_REQUEST_SGI);
#endif
            AROSCPUCurrentState(AROS_CPU_OFFLINE);
            (void)psci(UINT64_C(0x84000002),0,0,0);
            AROSCPUCurrentState(AROS_CPU_IDLE); /* CPU_OFF returned: still executing. */
            for (;;) __asm__ volatile("wfe");
        }
        if (cmd!=previous) {
            __asm__ volatile("msr daifset,#2":::"memory");
            AROSCPUCurrentState(cmd==1?AROS_CPU_BUSY:AROS_CPU_IDLE);previous=cmd;
#if defined(AROS_VM_GUEST) && AROS_VM_GUEST
            __asm__ volatile("msr daifclr,#2":::"memory");
#endif
        }
        if (cmd==1) __asm__ volatile("yield");
        else __asm__ volatile("wfe");
    }
}
uintptr_t AROSCPUProbeStart(void)
{
    const struct AROSCPUTopology *t=AROSCPUMonitorTopology();
    if (AROSAppleKernelChosenDecimal("aros,smp-probe")!=1) return 0;
    if (!AROSCPUMonitorFirmwareValid() || !t->psci_available) return 0;
    for (unsigned i=0;i<t->count;i++)
        if (i!=t->boot_cpu && t->cpu[i].enabled && t->cpu[i].method==AROS_CPU_START_PSCI) {
            probe_cpu=i;target=t->cpu[i].affinity;break;
        }
    if (probe_cpu==AROS_CPU_MAX) return 0;
    struct AROSAppleRuntimeLocal *boot_local=AROSAppleRuntimeLocal();
    uint32_t boot_active=boot_local->active,boot_irq=boot_local->irq_ready;
    uint32_t boot_fiq=boot_local->fiq_ready,boot_depth=boot_local->interrupt_depth;
#if defined(AROS_VM_GUEST) && AROS_VM_GUEST
    boot_gic=AROSVMKernelBootController();
    if (!boot_gic || boot_gic->version!=3) { probe_cpu=AROS_CPU_MAX;return 0; }
#endif
    hvc=t->psci_hvc;
    int64_t version=psci(0x84000000,0,0,0);
    if (version<2) { probe_cpu=AROS_CPU_MAX;return 0; }
    handoff.sp=(uintptr_t)stack+sizeof(stack);
#define SAVE(reg,field) __asm__ volatile("mrs %0," #reg:"=r"(handoff.field))
    SAVE(ttbr0_el1,ttbr0);SAVE(ttbr1_el1,ttbr1);SAVE(tcr_el1,tcr);
    SAVE(mair_el1,mair);SAVE(sctlr_el1,sctlr);
    clean((uintptr_t)&handoff,(uintptr_t)&handoff+sizeof(handoff));
    clean((uintptr_t)AROSCPUProbeEntry,(uintptr_t)AROSCPUProbeEntryEnd);
    int64_t result=psci(UINT64_C(0xc4000003),target,(uintptr_t)AROSCPUProbeEntry,(uintptr_t)&handoff);
    if (result!=0) { probe_cpu=AROS_CPU_MAX;return 0; }
    uint64_t frequency;__asm__ volatile("mrs %0,cntfrq_el0":"=r"(frequency));
    uint64_t end=now()+frequency;
    while (!__atomic_load_n(&ready,__ATOMIC_ACQUIRE) && now()<end) __asm__ volatile("yield");
#if defined(AROS_VM_GUEST) && AROS_VM_GUEST
    if (__atomic_load_n(&ready,__ATOMIC_ACQUIRE)!=1 ||
        !AROSGICClearPending(boot_gic,PROBE_REPLY_SGI) ||
        !AROSGICEnable(boot_gic,PROBE_REPLY_SGI,1) || !send_request()) return 0;
#endif
    return __atomic_load_n(&ready,__ATOMIC_ACQUIRE)==1 &&
        boot_local==AROSAppleRuntimeLocal() && boot_local->active==boot_active &&
        boot_local->irq_ready==boot_irq && boot_local->fiq_ready==boot_fiq &&
        boot_local->interrupt_depth==boot_depth;
}
int AROSCPUProbeCPU(unsigned cpu) { return cpu==probe_cpu; }
void AROSCPUProbeTick(uint64_t seconds)
{
    if (probe_cpu==AROS_CPU_MAX || __atomic_load_n(&ready,__ATOMIC_ACQUIRE)!=1) return;
    __atomic_store_n(&command,seconds%4<2?1:2,__ATOMIC_RELEASE);
    __asm__ volatile("dsb ishst\n\tsev":::"memory");
    if (!AROSAppleRuntimeSupervisor(send_request))
        __atomic_store_n(&ipi_fault,1,__ATOMIC_RELEASE);
}
/* Call from the EL0 hold task, before entering the masked Stop supervisor.
 * Reply IRQs must remain deliverable while the final request completes. */
int AROSCPUProbeDrain(void)
{
    uint64_t frequency;
    __asm__ volatile("mrs %0,cntfrq_el0":"=r"(frequency));
    uint64_t end=now()+frequency;
    do {
        uint32_t sent,handled,acked;
        AROSCPUProbeIPICounters(&sent,&handled,&acked);
        if (sent==handled && sent==acked) return 1;
        __asm__ volatile("yield":::"memory");
    } while (now()<end);
    __atomic_store_n(&ipi_fault,1,__ATOMIC_RELEASE);
    return 0;
}
uintptr_t AROSCPUProbeStop(void)
{
    if (probe_cpu==AROS_CPU_MAX) return 1;
    __atomic_store_n(&command,3,__ATOMIC_RELEASE);
    __asm__ volatile("dsb ishst\n\tsev":::"memory");
    uint64_t frequency;__asm__ volatile("mrs %0,cntfrq_el0":"=r"(frequency));
    uint64_t end=now()+frequency;
    while (now()<end) if (psci(UINT64_C(0xc4000004),target,0,0)==1) {
#if defined(AROS_VM_GUEST) && AROS_VM_GUEST
        AROSGICDisable(boot_gic,PROBE_REPLY_SGI);
        AROSGICClearPending(boot_gic,PROBE_REPLY_SGI);
#endif
        probe_cpu=AROS_CPU_MAX;return 1;
    }
    /* Keep the kernel mapped if firmware has not stopped the secondary. */
    return 0;
}

int AROSCPUProbeIPIPassed(void)
{
    uint32_t sent,handled,acked;AROSCPUProbeIPICounters(&sent,&handled,&acked);
    return !__atomic_load_n(&ipi_fault,__ATOMIC_ACQUIRE) && sent>=3 && sent==handled && sent==acked;
}
