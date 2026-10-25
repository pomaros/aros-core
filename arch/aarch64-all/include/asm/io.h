#ifndef AROS_AARCH64_IO_H
#define AROS_AARCH64_IO_H

/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: Device register access on AArch64.

    Every access is one plain load or store of its own width, with no
    writeback and no pairing. A volatile dereference does not guarantee that:
    the compiler may emit LDR with pre- or post-index writeback, or LDP/STP,
    and a hypervisor (Hypervisor.framework, KVM) can only emulate an MMIO
    access whose syndrome describes a single register transfer (ESR ISV=1).
    Such an access stops a virtual machine; on hardware it works, so the
    defect only shows in a VM.
*/

#include <exec/types.h>

static inline UBYTE __aarch64_mmio_in8(const volatile void *address)
{
    ULONG value;

    __asm__ volatile("ldrb %w0, [%1]" : "=r"(value) : "r"(address) : "memory");
    return (UBYTE)value;
}

static inline UWORD __aarch64_mmio_in16(const volatile void *address)
{
    ULONG value;

    __asm__ volatile("ldrh %w0, [%1]" : "=r"(value) : "r"(address) : "memory");
    return (UWORD)value;
}

static inline ULONG __aarch64_mmio_in32(const volatile void *address)
{
    ULONG value;

    __asm__ volatile("ldr %w0, [%1]" : "=r"(value) : "r"(address) : "memory");
    return value;
}

static inline UQUAD __aarch64_mmio_in64(const volatile void *address)
{
    UQUAD value;

    __asm__ volatile("ldr %x0, [%1]" : "=r"(value) : "r"(address) : "memory");
    return value;
}

static inline void __aarch64_mmio_out8(UBYTE value, volatile void *address)
{
    __asm__ volatile("strb %w0, [%1]" :: "r"((ULONG)value), "r"(address) : "memory");
}

static inline void __aarch64_mmio_out16(UWORD value, volatile void *address)
{
    __asm__ volatile("strh %w0, [%1]" :: "r"((ULONG)value), "r"(address) : "memory");
}

static inline void __aarch64_mmio_out32(ULONG value, volatile void *address)
{
    __asm__ volatile("str %w0, [%1]" :: "r"(value), "r"(address) : "memory");
}

static inline void __aarch64_mmio_out64(UQUAD value, volatile void *address)
{
    __asm__ volatile("str %x0, [%1]" :: "r"(value), "r"(address) : "memory");
}

/* AArch64 has no port space: port I/O is memory-mapped I/O. */
#define inb(address) __aarch64_mmio_in8((const volatile void *)(address))
#define inw(address) __aarch64_mmio_in16((const volatile void *)(address))
#define inl(address) __aarch64_mmio_in32((const volatile void *)(address))

#define outb(value, address) __aarch64_mmio_out8((value), (volatile void *)(address))
#define outw(value, address) __aarch64_mmio_out16((value), (volatile void *)(address))
#define outl(value, address) __aarch64_mmio_out32((value), (volatile void *)(address))

#define HAVE_MMIO_IO
#define mmio_inb(address) __aarch64_mmio_in8((const volatile void *)(address))
#define mmio_inw(address) __aarch64_mmio_in16((const volatile void *)(address))
#define mmio_inl(address) __aarch64_mmio_in32((const volatile void *)(address))
#define mmio_inq(address) __aarch64_mmio_in64((const volatile void *)(address))

#define mmio_outb(value, address) __aarch64_mmio_out8((value), (volatile void *)(address))
#define mmio_outw(value, address) __aarch64_mmio_out16((value), (volatile void *)(address))
#define mmio_outl(value, address) __aarch64_mmio_out32((value), (volatile void *)(address))
#define mmio_outq(value, address) __aarch64_mmio_out64((value), (volatile void *)(address))

#endif /* AROS_AARCH64_IO_H */
