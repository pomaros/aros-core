/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef AROS_PLATFORM_SMP_H
#define AROS_PLATFORM_SMP_H
#include "smp.h"
#include <stddef.h>
struct AROSCPUTopology;
struct AROSSMPHandoff { uint64_t sp,ttbr0,ttbr1,tcr,mair,sctlr,cntkctl; };
_Static_assert(sizeof(struct AROSSMPHandoff)==56 &&
    offsetof(struct AROSSMPHandoff,cntkctl)==48, "SMP entry handoff layout");
struct AROSSMPCPUState { uint32_t ready,stop; uint64_t ticks; };
/* Shared ownership/readiness state; platform admission publishes using the
 * same release/acquire protocol as the common scheduler and stop path. */
struct AROSSMPSharedState {
    uint32_t online_extra;
    uint64_t boot_ticks;
    struct AROSSMPCPUState cpu[AROS_APPLE_SMP_MAX_CPUS];
};
extern struct AROSSMPSharedState AROSSMPState;
extern struct AROSSMPHandoff AROSAppleSMPHandoff;
extern uint32_t AROSAppleSMPStartingCPU;
extern uint64_t AROSAppleSMPEntryTraces[AROS_APPLE_SMP_MAX_CPUS][16];
#define AROS_SMP_SCHEDULER_IPI 13U
int AROSPlatformSMPIPIReady(void);
void AROSPlatformSMPSendIPI(uint64_t affinity);
void AROSPlatformSMPAfterTimerArm(void);
int AROSPlatformSMPKernelOwned(void);
void AROSPlatformSMPOff(unsigned cpu);
int AROSPlatformSMPSecondaryInit(void);
int AROSPlatformSMPEarlyOnline(void);
void AROSPlatformSMPEnableInterrupts(void);
unsigned AROSPlatformSMPAdmissionLimit(void);
int AROSPlatformSMPPrepare(void);
unsigned AROSPlatformSMPStartMethod(const struct AROSCPUTopology *topology);
int AROSPlatformSMPEligible(const struct AROSCPUTopology *topology, unsigned cpu);
void AROSPlatformSMPCleanEntry(void);
int AROSPlatformSMPStartCPU(const struct AROSCPUTopology *topology, unsigned cpu);
int AROSPlatformSMPReady(unsigned cpu, uint64_t until);
void AROSPlatformSMPStartComplete(void);
int AROSPlatformSMPStopped(unsigned cpu);
void AROSSMPBootTick(void);
#endif
