/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef AROS_APPLE_SMP_H
#define AROS_APPLE_SMP_H
#include <stdint.h>
#define AROS_APPLE_SMP_MAX_CPUS 32
uint32_t AROSAppleSMPOnlineMask(void);
void AROSAppleSMPSendIPI(uint32_t message, uint32_t data, uint32_t mask);
uint64_t AROSAppleSMPTimeUS(void);
/* Cooperate with mapping rendezvous while waiting with local IRQs masked. */
void AROSAppleMemoryRendezvousPoll(void);
void AROSAppleDispatchTransition(unsigned busy);
int AROSAppleSMPHandleIPI(uint32_t id);
int AROSAppleSMPTick(void);
uint64_t AROSAppleSMPTimerTicks(void);
uint64_t AROSAppleSMPBootTicks(void);
int AROSAppleSMPPrepare(void);
uintptr_t AROSAppleSMPStart(void);
uintptr_t AROSAppleSMPStop(void);
void AROSAppleSMPCheckStop(void);
struct AROSAppleRuntimeFrame;
int AROSAppleSMPHandleFIQ(struct AROSAppleRuntimeFrame *);
int AROSAppleSMPKernelOwned(void);
int AROSAppleSMPNativeFIQReady(void);
int AROSAppleSMPIsSecondary(void);
struct timerequest;
int AROSAppleSMPWorkProof(struct timerequest *timer);
#endif
