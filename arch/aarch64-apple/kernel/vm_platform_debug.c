/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include "platform_debug.h"
/* VM output is provided by the common FDT-selected PL011/virtio console.
 * Native hardware evidence channels and device identity have no VM binding. */
void AROSPlatformDebugInitialize(uintptr_t base, uint64_t size)
{ (void)base; (void)size; }
int AROSPlatformDebugSelectEvidenceChannel(const void *fdt, uint64_t size)
{ (void)fdt; (void)size; return 0; }
int AROSPlatformDebugPutRaw(int chr) { (void)chr; return 0; }
void AROSPlatformDebugTouchSink(void) { }
int AROSPlatformIdentity(uint32_t id, intptr_t *value)
{ (void)id; (void)value; return 0; }
