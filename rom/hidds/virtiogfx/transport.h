/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef VIRTIOGFX_TRANSPORT_H
#define VIRTIOGFX_TRANSPORT_H
#include <exec/types.h>
#include <exec/libraries.h>
#include <oop/oop.h>
struct VirtioGPU;
struct VirtioGPUSurface { APTR pixels;ULONG width,height,stride; };
/* Task context with interrupts and scheduling enabled. Caller retains the
 * OOP/PCI provider while a context exists.
 * On failure *out is NULL after complete unwind, or a quarantined context
 * whose DMA must remain alive until a later acknowledged reset. */
BOOL VirtioGPUOpen(struct Library *oop,OOP_Object *device,struct VirtioGPU **out);
BOOL VirtioGPUGetSurface(struct VirtioGPU *,struct VirtioGPUSurface *);
/* Caller serializes framebuffer writes with Present and all users with Close. */
BOOL VirtioGPUPresent(struct VirtioGPU *,ULONG x,ULONG y,ULONG width,ULONG height);
/* 0: quarantine retained; 1: clean close; 2: reset recovered a command failure.
 * Any nonzero result consumes/frees the context. No caller may use it again. */
ULONG VirtioGPUClose(struct VirtioGPU *);
#endif
