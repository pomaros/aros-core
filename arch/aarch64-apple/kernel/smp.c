/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include "smp.h"
#include "platform_smp.h"
#include "page_allocator.h"
#include "memory_rendezvous.h"
#include <aros/debug.h>
#include "platform_timer_kernel.h"
#include "cpu_monitor.h"
#include "jit_protect.h"
#include "kernel_cpu.h"
#include "kernel_intr.h"
#include "kernel_ipi.h"
#include <aros/apple/runtime.h>
#include <exec_platform.h>
#include "../exec/smp_api.h"
#include <proto/kernel.h>
extern spinlock_t *Kernel_51_KrnSpinTryLock(spinlock_t *, ULONG, void *);
static void api_spin_init(spinlock_t *lock) { Kernel_49_KrnSpinInit(lock,NULL); }
static spinlock_t *api_spin_lock(spinlock_t *lock,struct Hook *hook,ULONG mode)
{ return Kernel_52_KrnSpinLock(lock,hook,mode,NULL); }
static void api_spin_unlock(spinlock_t *lock) { Kernel_53_KrnSpinUnLock(lock,NULL); }
static spinlock_t *api_spin_trylock(spinlock_t *lock,ULONG mode)
{ return Kernel_51_KrnSpinTryLock(lock,mode,NULL); }
const struct AROSAppleExecSMPAPI AROSAppleExecSMPAPI = {
 3,sizeof(struct AROSAppleExecSMPAPI),
 AROSAppleRuntimeIRQSave,AROSAppleRuntimeIRQRestore,
 api_spin_init,api_spin_lock,api_spin_unlock,
 core_ClaimCallIPI,core_CommitCallIPI,core_AbortCallIPI,core_CancelCallIPIs,
 core_TryClaimCallIPI,
 api_spin_trylock
};
struct AROSSMPSharedState AROSSMPState;
uint64_t AROSAppleSMPBootTicks(void)
{ return __atomic_load_n(&AROSSMPState.boot_ticks,__ATOMIC_RELAXED); }
static uint32_t pending[AROS_APPLE_SMP_MAX_CPUS];
static uint32_t notify[AROS_APPLE_SMP_MAX_CPUS];
#define SCHEDULER_SGI AROS_SMP_SCHEDULER_IPI
uint32_t AROSAppleSMPOnlineMask(void)
{
 const struct AROSCPUTopology *t=AROSCPUMonitorTopology();
 return (t->boot_cpu < AROS_APPLE_SMP_MAX_CPUS ? 1U<<t->boot_cpu : 0)|__atomic_load_n(&AROSSMPState.online_extra,__ATOMIC_ACQUIRE);
}
uint64_t AROSAppleSMPTimeUS(void)
{
 uint64_t ticks,hz;
 __asm__ volatile("isb; mrs %0,cntvct_el0; mrs %1,cntfrq_el0":"=r"(ticks),"=r"(hz));
 return hz ? (ticks/hz)*1000000U+(ticks%hz)*1000000U/hz : 0;
}
/* One owner holds PageMapLock across the rendezvous and mutation. A contender
 * cooperates while waiting, despite masked IRQ/FIQ. The participant executes
 * no allocator, DOS, device, or scheduler operation while stopped. */
#define AROS_APPLE_MEMORY_IPI (1U << 21)
#define MEMORY_RENDEZVOUS_TIMEOUT_US UINT64_C(100000)
static struct AROSAppleMemoryRendezvous memory_rendezvous;
static uintptr_t memory_participate(void)
{
 unsigned cpu=AROSCPUCurrentIndex();
 uint64_t token;
 while((token=AROSAppleMemoryRendezvousRequest(&memory_rendezvous,cpu))) {
  __asm__ volatile("dsb ish" ::: "memory");
  if(!AROSAppleMemoryRendezvousEnter(&memory_rendezvous,cpu,token)) continue;
  while(AROSAppleMemoryRendezvousHeld(&memory_rendezvous,token))
   __asm__ volatile("yield" ::: "memory");
  /* The owner completes broadcast TLBI and code-cache maintenance before
   * release. Each resumed PE also needs its own context synchronization. */
  __asm__ volatile("dsb ish; isb" ::: "memory");
  AROSAppleMemoryRendezvousLeave(&memory_rendezvous,cpu,token);
 }
 return 1;
}
uint64_t AROSApplePageMapAcquire(void)
{
 uint64_t saved=AROSAppleRuntimeIRQSave();
 for(;;) {
  if(AROSAppleMemoryRendezvousRequest(&memory_rendezvous,AROSCPUCurrentIndex()))
   AROSAppleRuntimeSupervisor(memory_participate);
  if(!__atomic_exchange_n(&AROSApplePageMapLock.held,1,__ATOMIC_ACQUIRE))
   return saved;
  __asm__ volatile("yield" ::: "memory");
 }
}
uint64_t AROSApplePageMutationBegin(void)
{
 unsigned cpu=AROSCPUCurrentIndex();
 if(cpu>=32) return 0;
 uint32_t mask=AROSAppleSMPOnlineMask() & ~(UINT32_C(1)<<cpu);
 uint64_t token=AROSAppleMemoryRendezvousBegin(&memory_rendezvous,mask);
 if(!token) return 0;
 if(mask) AROSAppleSMPSendIPI(AROS_APPLE_MEMORY_IPI,0,mask);
 uint64_t start=AROSAppleSMPTimeUS();
 while(!AROSAppleMemoryRendezvousAll(memory_rendezvous.arrived,token)) {
  if(AROSAppleSMPTimeUS()-start>=MEMORY_RENDEZVOUS_TIMEOUT_US) {
   AROSAppleMemoryRendezvousEnd(&memory_rendezvous);
   bug("[SMP-MMU] rendezvous arrival timeout; no mutation\n");
   return 0;
  }
  __asm__ volatile("yield" ::: "memory");
 }
 return token;
}
void AROSApplePageMutationEnd(uint64_t token)
{
 AROSAppleMemoryRendezvousRelease(&memory_rendezvous);
 uint64_t start=AROSAppleSMPTimeUS();
 while(!AROSAppleMemoryRendezvousAll(memory_rendezvous.departed,token)) {
  if(AROSAppleSMPTimeUS()-start>=MEMORY_RENDEZVOUS_TIMEOUT_US) {
   /* Mutation already happened. Reusing the operation or claiming success
    * before remote synchronization would be unsafe. Retain locks/evidence. */
   bug("[SMP-MMU] fatal rendezvous departure timeout token=%llx\n",(unsigned long long)token);
   for(;;) __asm__ volatile("yield" ::: "memory");
  }
  __asm__ volatile("yield" ::: "memory");
 }
 AROSAppleMemoryRendezvousEnd(&memory_rendezvous);
}
static uintptr_t notify_ipi(void)
{
 unsigned src=AROSCPUCurrentIndex();
 const struct AROSCPUTopology *t=AROSCPUMonitorTopology();
 if (src>=AROS_APPLE_SMP_MAX_CPUS || !AROSPlatformSMPIPIReady()) return 0;
 uint32_t mask=notify[src];
 for(unsigned cpu=0;cpu<t->count && cpu<AROS_APPLE_SMP_MAX_CPUS;cpu++)
  if(mask&(1U<<cpu)) AROSPlatformSMPSendIPI(t->cpu[cpu].affinity);
 return 1;
}
void AROSAppleSMPSendIPI(uint32_t message,uint32_t data,uint32_t mask)
{
 (void)data;
 uint64_t saved=AROSAppleRuntimeIRQSave();
 unsigned src=AROSCPUCurrentIndex();
 if(src>=AROS_APPLE_SMP_MAX_CPUS) {
  AROSAppleRuntimeIRQRestore(saved);
  return;
 }
 mask &= AROSAppleSMPOnlineMask();
 for(unsigned cpu=0;cpu<AROS_APPLE_SMP_MAX_CPUS;cpu++)
  if(mask&(1U<<cpu)) __atomic_fetch_or(&pending[cpu],message&0x0fffffffU,__ATOMIC_RELEASE);
 notify[src]=mask;
 (void)AROSAppleRuntimeSupervisor(notify_ipi);
 AROSAppleRuntimeIRQRestore(saved);
}
/* Set by a sleep with every task frozen (aros/apple/idle.h, kernel_cpu.c):
 * a secondary core then lets its tick lapse and waits for an IPI. */
extern volatile uint32_t AROSAppleKernelTicksHeld;
static uint32_t ap_tick_lapsed[AROS_APPLE_SMP_MAX_CPUS];
static void ap_timer_arm(void);
int AROSAppleSMPHandleIPI(uint32_t id)
{
 extern void handle_ipi(uint32_t,uint32_t);
 if(id!=SCHEDULER_SGI) return 0;
 unsigned cpu=AROSCPUCurrentIndex();
 if(cpu>=AROS_APPLE_SMP_MAX_CPUS) return 1;
 uint32_t message=__atomic_exchange_n(&pending[cpu],0,__ATOMIC_ACQ_REL);
 /* The first IPI after a held sleep restarts this core's tick. */
 if(ap_tick_lapsed[cpu] && !__atomic_load_n(&AROSAppleKernelTicksHeld,__ATOMIC_ACQUIRE)) {
  ap_tick_lapsed[cpu]=0;
  ap_timer_arm();
 }
 if(message & AROS_APPLE_MEMORY_IPI) memory_participate();
 if(message & AROS_APPLE_TIMER_IPI) {
  EXEC_BLOCK_DISPATCH_INC;
  AROSAppleKernelTimerNotify();
  EXEC_BLOCK_DISPATCH_DEC;
 }
 handle_ipi(message & ~(AROS_APPLE_TIMER_IPI|AROS_APPLE_MEMORY_IPI),0);
 return 1;
}

/* Sequential admission: the startup handoff is reused only after readiness.
 * Runtime stacks, stop/readiness state and exception evidence are per CPU. */
struct AROSSMPHandoff AROSAppleSMPHandoff;
#define ap_handoff AROSAppleSMPHandoff
static _Alignas(64) unsigned char ap_stack[AROS_APPLE_SMP_MAX_CPUS][65536];
_Alignas(128) uint64_t AROSAppleSMPEntryTraces[AROS_APPLE_SMP_MAX_CPUS][16];
uint32_t AROSAppleSMPStartingCPU;
#define ap_cpu AROSAppleSMPStartingCPU
#define AROSAppleSMPEntryTrace AROSAppleSMPEntryTraces[ap_cpu]
int AROSAppleSMPKernelOwned(void) { return AROSPlatformSMPKernelOwned(); }
#define ap_ready AROSSMPState.cpu[ap_cpu].ready
int AROSAppleSMPNativeFIQReady(void)
{
 return __atomic_load_n(&AROSSMPState.online_extra,__ATOMIC_ACQUIRE)!=0;
}
int AROSAppleSMPIsSecondary(void)
{
 unsigned cpu=AROSCPUCurrentIndex();
 return cpu<AROS_APPLE_SMP_MAX_CPUS && cpu!=AROSCPUMonitorTopology()->boot_cpu;
}
uint64_t AROSAppleSMPTimerTicks(void)
{
 uint64_t total=0;
 for(unsigned i=0;i<AROS_APPLE_SMP_MAX_CPUS;i++)
  total+=__atomic_load_n(&AROSSMPState.cpu[i].ticks,__ATOMIC_RELAXED);
 return total;
}
extern void AROSAppleSMPEntry(void),AROSAppleSMPEntryEnd(void);
extern void AROSAppleKernelIdleLoop(void);
static void ap_timer_arm(void)
{
 uint64_t hz; __asm__ volatile("mrs %0,cntfrq_el0":"=r"(hz));
 if (__atomic_load_n(&AROSAppleKernelTicksHeld,__ATOMIC_ACQUIRE)) {
  unsigned cpu=AROSCPUCurrentIndex();
  __asm__ volatile("msr cntv_ctl_el0,xzr; isb":::"memory");
  if(cpu<AROS_APPLE_SMP_MAX_CPUS) ap_tick_lapsed[cpu]=1;
  return;
 }
 uint64_t interval=hz/100U;
 __asm__ volatile("msr cntv_tval_el0,%0; msr cntv_ctl_el0,%1; isb"::"r"(interval),"r"(UINT64_C(1)):"memory");
 AROSPlatformSMPAfterTimerArm();
}
static void scheduler_tick(void)
{
 if (TLS_GET(ThisTask)) {
  UWORD left=TLS_GET(Elapsed);
  if(left) --left;
  TLS_SET(Elapsed,left);
  if(!left) TLS_SET(ScheduleFlags,TLS_GET(ScheduleFlags)|TLSSF_Quantum|TLSSF_Switch);
 }
}
int AROSAppleSMPTick(void)
{
 ap_timer_arm();
 __atomic_add_fetch(&AROSSMPState.cpu[AROSCPUCurrentIndex()].ticks,1,__ATOMIC_RELAXED);
 scheduler_tick();
 return 1;
}
void AROSSMPBootTick(void)
{
 __atomic_add_fetch(&AROSSMPState.boot_ticks,1,__ATOMIC_RELAXED);
 scheduler_tick();
}
static __attribute__((noreturn)) void ap_off(void)
{
 unsigned cpu=AROSCPUCurrentIndex();
 __asm__ volatile("msr daifset,#3; msr cntv_ctl_el0,xzr; isb":::"memory");
 AROSCPUCurrentState(AROS_CPU_OFFLINE);
 AROSPlatformSMPOff(cpu);
 /* CPU_OFF must not return: keep the image owned if it does. */
 __atomic_store_n(&AROSSMPState.cpu[cpu].ready,3,__ATOMIC_RELEASE);
 for(;;) __asm__ volatile("wfe");
}
void AROSAppleSMPCheckStop(void)
{
 unsigned cpu=AROSCPUCurrentIndex();
 if(AROSAppleSMPIsSecondary() && __atomic_load_n(&AROSSMPState.cpu[cpu].stop,__ATOMIC_ACQUIRE) &&
    AROSAppleRuntimeLocal()->idle_active && !TLS_GET(ThisTask)) ap_off();
}
void AROSAppleSMPMain(void)
{
 __atomic_store_n(&AROSAppleSMPEntryTrace[0],3,__ATOMIC_RELEASE);
 if(!AROSCPUBindCurrent() || AROSCPUCurrentIndex()!=ap_cpu
    || !AROSPlatformSMPSecondaryInit()
    ) {
  __atomic_store_n(&ap_ready,2,__ATOMIC_RELEASE);
  ap_off();
 }
 unsigned cpu=AROSCPUCurrentIndex();
 struct AROSAppleRuntimeLocal *local=AROSAppleRuntimeLocal();
 AROSAppleJitProtectInitCPU();
 local->active=1;
 local->idle_active=1;
 TLS_SET(ThisTask,NULL);
 TLS_SET(IDNestCnt,-1);
 TLS_SET(TDNestCnt,-1);
 ap_timer_arm();
 AROSCPUCurrentState(AROS_CPU_IDLE);
 if (AROSPlatformSMPEarlyOnline())
  __atomic_fetch_or(&AROSSMPState.online_extra,1U<<cpu,__ATOMIC_RELEASE);
 __atomic_store_n(&AROSSMPState.cpu[cpu].ready,1,__ATOMIC_RELEASE);
 AROSPlatformSMPEnableInterrupts();
 AROSAppleKernelIdleLoop();
 __builtin_unreachable();
}
static unsigned admission_limit(void)
{ return AROSPlatformSMPAdmissionLimit(); }
int AROSAppleSMPPrepare(void)
{ return AROSPlatformSMPPrepare(); }
static uintptr_t start_one(void)
{
 const struct AROSCPUTopology *t=AROSCPUMonitorTopology();
 if(!AROSCPUMonitorFirmwareValid() || t->boot_cpu>=AROS_APPLE_SMP_MAX_CPUS) return 0;
 unsigned method=AROSPlatformSMPStartMethod(t);
 if (!method) return 0;
 ap_cpu=AROS_APPLE_SMP_MAX_CPUS;
 for(unsigned i=0;i<t->count && i<AROS_APPLE_SMP_MAX_CPUS;i++)
  if(!(AROSAppleSMPOnlineMask()&(1U<<i)) && i!=t->boot_cpu && t->cpu[i].enabled && t->cpu[i].method==method &&
     AROSPlatformSMPEligible(t,i)) {
   ap_cpu=i;break;
  }
 if(ap_cpu==AROS_APPLE_SMP_MAX_CPUS) return 0;
 ap_handoff.sp=(uintptr_t)ap_stack[ap_cpu]+sizeof(ap_stack[ap_cpu]);
#define SAVE_SMP(reg,field) __asm__ volatile("mrs %0," #reg:"=r"(ap_handoff.field))
 SAVE_SMP(ttbr0_el1,ttbr0);SAVE_SMP(ttbr1_el1,ttbr1);SAVE_SMP(tcr_el1,tcr);
 SAVE_SMP(mair_el1,mair);SAVE_SMP(sctlr_el1,sctlr);SAVE_SMP(cntkctl_el1,cntkctl);
 for(uintptr_t p=(uintptr_t)&ap_handoff&~(uintptr_t)15;p<(uintptr_t)(&ap_handoff+1);p+=16)
  __asm__ volatile("dc cvac,%0"::"r"(p):"memory");
 for(uintptr_t p=(uintptr_t)AROSAppleSMPEntry&~(uintptr_t)15;p<(uintptr_t)AROSAppleSMPEntryEnd;p+=16)
  __asm__ volatile("dc cvac,%0"::"r"(p):"memory");
 AROSPlatformSMPCleanEntry();
 __asm__ volatile("dsb sy":::"memory");
 if(!AROSPlatformSMPStartCPU(t,ap_cpu)) return 0;
 uint64_t until=AROSAppleSMPTimeUS()+1000000U;
 while(!__atomic_load_n(&ap_ready,__ATOMIC_ACQUIRE) && AROSAppleSMPTimeUS()<until)
  __asm__ volatile("yield");
 return AROSPlatformSMPReady(ap_cpu,until);
}
uintptr_t AROSAppleSMPStart(void)
{
 while(__builtin_popcount(AROSAppleSMPOnlineMask())<admission_limit())
  if(!start_one()) return 0;
 AROSPlatformSMPStartComplete();
 extern int AROSAppleKernelDebugPutDirect(int);
 const char *s="[SMP] online mask=";
 while(*s) AROSAppleKernelDebugPutDirect(*s++);
 uint32_t mask=AROSAppleSMPOnlineMask();
 for(int shift=28;shift>=0;shift-=4)
  AROSAppleKernelDebugPutDirect("0123456789abcdef"[(mask>>shift)&15]);
 AROSAppleKernelDebugPutDirect('\n');
 return 1;
}
uintptr_t AROSAppleSMPStop(void)
{
 uint32_t mask=AROSAppleSMPOnlineMask() & ~(1U<<AROSCPUMonitorTopology()->boot_cpu);
 for(unsigned cpu=0;cpu<AROS_APPLE_SMP_MAX_CPUS;cpu++)
  if(mask&(1U<<cpu)) __atomic_store_n(&AROSSMPState.cpu[cpu].stop,1,__ATOMIC_RELEASE);
 AROSAppleSMPSendIPI(0,0,mask);
 uint64_t until=AROSAppleSMPTimeUS()+1000000U;
 do {
  uint32_t left=mask;
  for(unsigned cpu=0;cpu<AROS_APPLE_SMP_MAX_CPUS;cpu++) if(mask&(1U<<cpu)) {
   if(AROSPlatformSMPStopped(cpu)) {
    __atomic_fetch_and(&AROSSMPState.online_extra,~(1U<<cpu),__ATOMIC_RELEASE);
    left&=~(1U<<cpu);
   }
  }
  if(!left) return 1;
  __asm__ volatile("yield");
 } while(AROSAppleSMPTimeUS()<until);
 return 0;
}
