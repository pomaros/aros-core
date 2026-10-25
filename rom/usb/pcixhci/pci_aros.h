#ifndef PCI_AROS_H
#define PCI_AROS_H

/*
 *----------------------------------------------------------------------------
 *             Includes for AROS PCI handling
 *----------------------------------------------------------------------------
 *                   By Chris Hodges <chrisly@platon42.de>

 *
 */

// hmmm, these were PPC specific barriers

#undef SYNC
#ifdef __powerpc__
#define SYNC asm volatile("eieio");
#else
#define SYNC
#endif

#include <aros/io.h>

/* Register accesses go through <asm/io.h>, whose accessors are single
   loads and stores where a CPU needs it (AArch64 under a hypervisor). */
#define READMEM16_LE(rb) AROS_WORD2LE(mmio_inw(rb))
#define READMEM32_LE(rb) AROS_LONG2LE(mmio_inl(rb))
#define	WRITEMEM32_LE(adr, value)	   mmio_outl(AROS_LONG2LE(value), (adr))
#define CONSTWRITEMEM32_LE(adr, value) mmio_outl(AROS_LONG2LE(value), (adr))

#define XHCI_REG(rb, offset) (((UBYTE *) (rb)) + ((ULONG) (offset)))
#define CONSTWRITEREG16_LE(rb, offset, value) mmio_outw(AROS_WORD2LE(value), XHCI_REG(rb, offset))
#define CONSTWRITEREG32_LE(rb, offset, value) mmio_outl(AROS_LONG2LE(value), XHCI_REG(rb, offset))
#define WRITEREG16_LE(rb, offset, value)      mmio_outw(AROS_WORD2LE(value), XHCI_REG(rb, offset))
#define WRITEREG32_LE(rb, offset, value)      mmio_outl(AROS_LONG2LE(value), XHCI_REG(rb, offset))
#define WRITEREG64_LE(rb, offset, value)      mmio_outq(AROS_QUAD2LE(value), XHCI_REG(rb, offset))

#define READREG8(rb, offset)     mmio_inb(XHCI_REG(rb, offset))
#define READREG16_LE(rb, offset) AROS_WORD2LE(mmio_inw(XHCI_REG(rb, offset)))
#define READREG32_LE(rb, offset) AROS_LONG2LE(mmio_inl(XHCI_REG(rb, offset)))
#define READREG64_LE(rb, offset) AROS_QUAD2LE(mmio_inq(XHCI_REG(rb, offset)))

/* One register named by its field in a hardware/usb/xhci.h layout. */
#define XHCI_RD8(reg)         mmio_inb(&(reg))
#define XHCI_RD16(reg)        AROS_LE2WORD(mmio_inw(&(reg)))
#define XHCI_RD32(reg)        AROS_LE2LONG(mmio_inl(&(reg)))
#define XHCI_WR32(reg, value) mmio_outl(AROS_LONG2LE(value), &(reg))

#define READIO16_LE(rb, offset) AROS_WORD2LE(WORDIN((((UBYTE *) (rb)) + ((ULONG) (offset)))))
#define WRITEIO16_LE(rb, offset, value) WORDOUT((((UBYTE *) (rb)) + ((ULONG) (offset))), AROS_WORD2LE(value))
#define WRITEIO32_LE(rb, offset, value) LONGOUT((((UBYTE *) (rb)) + ((ULONG) (offset))), AROS_WORD2LE(value))

#endif // PCI_AROS_H
