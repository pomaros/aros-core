/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * This file is distributed under the AROS Public License, version 1.1.
 */
#ifndef PCIUSB_XHCI_MMIO_H
#define PCIUSB_XHCI_MMIO_H

#include <exec/types.h>
#include <aros/macros.h>

/* MMIO needs a single scalar access. In particular, an AArch64 writeback
 * load has no valid instruction syndrome on a stage-two device fault;
 * hypervisors such as HVF cannot emulate it as an ordinary register read. */
static inline UBYTE xhciReadMMIO8(const volatile UBYTE *reg)
{
#if defined(__aarch64__)
    ULONG value;
    __asm__ __volatile__("ldrb %w0, [%1]" : "=r"(value) : "r"(reg) : "memory");
    return (UBYTE)value;
#else
    return *reg;
#endif
}

static inline UWORD xhciReadMMIO16(const volatile UWORD *reg)
{
#if defined(__aarch64__)
    ULONG value;
    __asm__ __volatile__("ldrh %w0, [%1]" : "=r"(value) : "r"(reg) : "memory");
    return AROS_LE2WORD((UWORD)value);
#else
    return AROS_LE2WORD(*reg);
#endif
}

static inline ULONG xhciReadMMIO32(const volatile ULONG *reg)
{
#if defined(__aarch64__)
    ULONG value;
    __asm__ __volatile__("ldr %w0, [%1]" : "=r"(value) : "r"(reg) : "memory");
    return AROS_LE2LONG(value);
#else
    return AROS_LE2LONG(*reg);
#endif
}

static inline void xhciWriteMMIO32(volatile ULONG *reg, ULONG value)
{
#if defined(__aarch64__)
    value = AROS_LONG2LE(value);
    __asm__ __volatile__("str %w0, [%1]" : : "r"(value), "r"(reg) : "memory");
#else
    *reg = AROS_LONG2LE(value);
#endif
}

#endif
