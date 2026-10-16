/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef AROS_VM_IRQ_H
#define AROS_VM_IRQ_H
#include <stddef.h>
#include <stdint.h>
struct KernelBase;
struct AROSGIC;
struct AROSGIC *AROSVMKernelBootController(void);
struct AROSAppleRuntimeFrame;
int AROSVMKernelTimerInitialize(const void *, size_t, struct KernelBase *);
int AROSVMKernelHandleIRQ(struct AROSAppleRuntimeFrame *);
int AROSVMKernelTimerFire(void);
void AROSVMKernelEnableIRQ(uint32_t, struct KernelBase *);
void AROSVMKernelDisableIRQ(uint32_t, struct KernelBase *);
int AROSVMKernelConfigureIRQ(uint32_t, uint32_t);
int AROSVMKernelIRQAllowed(uint32_t, struct KernelBase *);
uintptr_t AROSVMKernelProbePrepare(void);
uintptr_t AROSVMKernelProbeFinish(void);
uintptr_t AROSVMKernelProbeIRQ(void);
int AROSVMKernelInitSecondary(void);
#endif
