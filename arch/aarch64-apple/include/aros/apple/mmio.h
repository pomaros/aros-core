/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1
 * Desc: Private resource-side accesses to FDT-authorized device windows.
 */
#ifndef AROS_APPLE_MMIO_H
#define AROS_APPLE_MMIO_H
#include <stdint.h>
#include <proto/exec.h>
_Static_assert(sizeof(IPTR) == 8, "Apple supervisor accesses require a 64-bit return value");

/* One request per translation unit. The owning resource must hold its
 * semaphore across each call; these are not interrupt-context operations.
 * Callers validate mapped ranges and power ownership before entering here.
 * No resource lock, allocation or delay runs inside the supervisor callback. */
static struct {
    uint64_t address, value;
    uint32_t width, write, length;
    volatile uint8_t *destination;
    const volatile uint8_t *source;
} apple_mmio_request;

static IPTR apple_mmio_supervised(void)
{
    uint64_t value = 0;
    __asm__ volatile("dsb osh" ::: "memory");
    if (!apple_mmio_request.width) {
        for (uint32_t i = 0; i < apple_mmio_request.length; ++i)
            apple_mmio_request.destination[i] = apple_mmio_request.source[i];
    } else if (apple_mmio_request.write) {
        if (apple_mmio_request.width == 4)
            *(volatile uint32_t *)(uintptr_t)apple_mmio_request.address = apple_mmio_request.value;
        else
            *(volatile uint64_t *)(uintptr_t)apple_mmio_request.address = apple_mmio_request.value;
    } else if (apple_mmio_request.width == 4) {
        value = *(const volatile uint32_t *)(uintptr_t)apple_mmio_request.address;
    } else {
        value = *(const volatile uint64_t *)(uintptr_t)apple_mmio_request.address;
    }
    __asm__ volatile("dsb osh" ::: "memory");
    return (IPTR)value;
}

static inline uint64_t apple_mmio_read(uint64_t address, uint32_t width)
{
    apple_mmio_request.address = address;
    apple_mmio_request.width = width;
    apple_mmio_request.write = 0;
    return (uint64_t)Supervisor((void *)apple_mmio_supervised);
}
static inline void apple_mmio_write(uint64_t address, uint32_t width, uint64_t value)
{
    apple_mmio_request.address = address;
    apple_mmio_request.width = width;
    apple_mmio_request.write = 1;
    apple_mmio_request.value = value;
    (void)Supervisor((void *)apple_mmio_supervised);
}
static inline uint32_t AROSAppleMMIORead32(uint64_t address)
{
    return (uint32_t)apple_mmio_read(address, 4);
}
static inline uint64_t AROSAppleMMIORead64(uint64_t address)
{
    return apple_mmio_read(address, 8);
}
static inline void AROSAppleMMIOWrite32(uint64_t address, uint32_t value)
{
    apple_mmio_write(address, 4, value);
}
static inline void AROSAppleMMIOWrite64(uint64_t address, uint64_t value)
{
    apple_mmio_write(address, 8, value);
}
static inline void AROSAppleMMIOCopy(void *destination, const void *source, uint32_t length)
{
    /* Bound each interrupt-masked copy to 256 bytes. */
    volatile uint8_t *out = destination;
    const volatile uint8_t *in = source;
    while (length) {
        uint32_t chunk = length > 256 ? 256 : length;
        apple_mmio_request.width = 0;
        apple_mmio_request.length = chunk;
        apple_mmio_request.destination = out;
        apple_mmio_request.source = in;
        (void)Supervisor((void *)apple_mmio_supervised);
        out += chunk; in += chunk; length -= chunk;
    }
}
#endif
