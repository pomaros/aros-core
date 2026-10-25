/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef AROS_PLATFORM_IRQ_H
#define AROS_PLATFORM_IRQ_H
#include <stddef.h>
#include <stdint.h>
struct KernelBase;
struct TagItem;
struct AROSAppleRuntimeFrame;
/* Exactly one platform implementation is linked into kernel.resource. */
int AROSPlatformHandleIRQ(struct AROSAppleRuntimeFrame *frame);
int AROSPlatformHandleFIQ(struct AROSAppleRuntimeFrame *frame);
int AROSPlatformFIQRequired(void);
int AROSPlatformHandleService(uint64_t service, struct AROSAppleRuntimeFrame *frame);
void AROSPlatformTimerDeliveryReady(int ready);
int AROSPlatformTimerInitialize(const void *fdt, size_t size, struct KernelBase *base);
uint32_t AROSPlatformModifyIRQ(uint32_t irq, struct TagItem *attribs, struct KernelBase *base);
void AROSPlatformEnableIRQ(uint32_t source, struct KernelBase *base);
void AROSPlatformDisableIRQ(uint32_t source, struct KernelBase *base);
int AROSPlatformIRQAllowed(uint32_t source, struct KernelBase *base);
intptr_t AROSPlatformIRQAttribute(uint32_t id);
#endif
