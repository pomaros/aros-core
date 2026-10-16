/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef AROS_PCI_ECAM_ACCESS_H
#define AROS_PCI_ECAM_ACCESS_H
#include <stdint.h>
#include <stddef.h>

/* Platform code supplies an already mapped, non-posted Device aperture and
 * serializes accesses where required. No DMA or interrupt grant is implied.
 * The mapping starts at first_bus, as specified by the generic ECAM binding. */
struct AROSPCIECAM {
    volatile uint8_t *mapping;
    uint64_t bytes;
    unsigned first_bus, last_bus;
};
static inline volatile void *AROSPCIECAMAddress(const struct AROSPCIECAM *h,
    unsigned bus, unsigned device, unsigned function, unsigned reg, unsigned width)
{
    uint64_t offset;
    if (!h || !h->mapping || h->first_bus > h->last_bus || h->last_bus > 255 ||
        bus < h->first_bus || bus > h->last_bus || device > 31 || function > 7 ||
        (width != 1 && width != 2 && width != 4) || reg >= 4096 ||
        (reg & (width - 1)) || width > 4096 - reg ||
        ((uintptr_t)h->mapping & 0xfffff)) return NULL;
    offset = ((uint64_t)(bus - h->first_bus) << 20) |
        ((uint64_t)device << 15) | ((uint64_t)function << 12) | reg;
    if (offset > h->bytes || width > h->bytes - offset ||
        offset > UINTPTR_MAX - (uintptr_t)h->mapping ||
        width - 1 > UINTPTR_MAX - ((uintptr_t)h->mapping + offset)) return NULL;
    return h->mapping + offset;
}
/* Invalid reads return the conventional absent-function value. Writes are
 * refused. Native-width stores deliberately never read-modify-write adjacent
 * status bits (which may be write-one-to-clear). ECAM is little endian. */
static inline uint32_t AROSPCIECAMRead(const struct AROSPCIECAM *h,
    unsigned bus, unsigned device, unsigned function, unsigned reg, unsigned width)
{
    volatile void *p = AROSPCIECAMAddress(h, bus, device, function, reg, width);
    uint32_t value;
    if (!p) return UINT32_MAX;
    if (width == 1) return *(volatile uint8_t *)p;
    if (width == 2) {
        value = *(volatile uint16_t *)p;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        value = __builtin_bswap16(value);
#endif
    } else {
        value = *(volatile uint32_t *)p;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        value = __builtin_bswap32(value);
#endif
    }
    return value;
}
static inline int AROSPCIECAMWrite(const struct AROSPCIECAM *h,
    unsigned bus, unsigned device, unsigned function, unsigned reg, unsigned width,
    uint32_t value)
{
    volatile void *p = AROSPCIECAMAddress(h, bus, device, function, reg, width);
    if (!p) return 0;
    if (width == 1) *(volatile uint8_t *)p = (uint8_t)value;
    else if (width == 2) {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        value = __builtin_bswap16((uint16_t)value);
#endif
        *(volatile uint16_t *)p = (uint16_t)value;
    } else {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        value = __builtin_bswap32(value);
#endif
        *(volatile uint32_t *)p = value;
    }
    return 1;
}
#endif
