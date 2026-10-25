/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include "platform_smp.h"
#include "cpu_monitor.h"
#include "vm_irq.h"
#include "gic.h"
static unsigned psci_hvc;
static int64_t smp_psci(uint64_t fn,uint64_t a,uint64_t b,uint64_t c)
{
 register uint64_t x0 __asm__("x0")=fn,x1 __asm__("x1")=a;
 register uint64_t x2 __asm__("x2")=b,x3 __asm__("x3")=c;
#define PSCI_CALL(insn) __asm__ volatile(insn : "+r"(x0),"+r"(x1),"+r"(x2),"+r"(x3) :: \
 "x4","x5","x6","x7","x8","x9","x10","x11","x12","x13","x14","x15","x16","x17","memory")
 if(psci_hvc) PSCI_CALL("hvc #0"); else PSCI_CALL("smc #0");
 return (int64_t)x0;
}
int AROSPlatformSMPIPIReady(void) { return AROSVMKernelBootController()!=0; }
void AROSPlatformSMPSendIPI(uint64_t affinity)
{ AROSGICSendIPI(AROSVMKernelBootController(),AROS_SMP_SCHEDULER_IPI,affinity); }
int AROSPlatformSMPKernelOwned(void) { return 0; }
void AROSPlatformSMPAfterTimerArm(void) { }
void AROSPlatformSMPOff(unsigned cpu)
{ (void)cpu; (void)smp_psci(UINT64_C(0x84000002),0,0,0); }
int AROSPlatformSMPSecondaryInit(void) { return AROSVMKernelInitSecondary(); }
int AROSPlatformSMPEarlyOnline(void) { return 1; }
void AROSPlatformSMPEnableInterrupts(void)
{ __asm__ volatile("msr daifclr,#2":::"memory"); }
unsigned AROSPlatformSMPAdmissionLimit(void) { return 2; }
int AROSPlatformSMPPrepare(void) { return 1; }
unsigned AROSPlatformSMPStartMethod(const struct AROSCPUTopology *t)
{
 struct AROSGIC *gic=AROSVMKernelBootController();
 return t->psci_available && gic && gic->version==3 ? AROS_CPU_START_PSCI : 0;
}
int AROSPlatformSMPEligible(const struct AROSCPUTopology *t, unsigned cpu)
{ (void)t; (void)cpu; return 1; }
void AROSPlatformSMPCleanEntry(void) { }
extern void AROSAppleSMPEntry(void);
int AROSPlatformSMPStartCPU(const struct AROSCPUTopology *t, unsigned cpu)
{
 psci_hvc=t->psci_hvc;
 return smp_psci(UINT64_C(0xc4000003),t->cpu[cpu].affinity,
     (uintptr_t)AROSAppleSMPEntry,(uintptr_t)&AROSAppleSMPHandoff)==0;
}
int AROSPlatformSMPReady(unsigned cpu, uint64_t until)
{ (void)until; return __atomic_load_n(&AROSSMPState.cpu[cpu].ready,__ATOMIC_ACQUIRE)==1; }
void AROSPlatformSMPStartComplete(void) { }
int AROSPlatformSMPStopped(unsigned cpu)
{ return smp_psci(UINT64_C(0xc4000004),AROSCPUMonitorTopology()->cpu[cpu].affinity,0,0)==1; }
