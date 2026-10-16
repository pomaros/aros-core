/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#ifndef AROS_EARLY_FDT_H
#define AROS_EARLY_FDT_H
#include <stddef.h>
#include <stdint.h>
#define AROS_FDT_MAX_BYTES (2U * 1024U * 1024U)
#define AROS_FDT_MAX_DEPTH 32
#define AROS_FDT_MAX_NODES 4096
#define AROS_FDT_MAX_PATH 256
#define AROS_FDT_MAX_IRQ_CELLS 4
/* Blob is immutable and remains owned by the caller. No allocation/global state. */
struct AROSEarlyFDT
{
    const void *blob;
    uint32_t bytes, nodes;
};
struct AROSFDTIRQ
{
    int controller;
    uint32_t count, cells[AROS_FDT_MAX_IRQ_CELLS];
};
int AROSEarlyFDTOpen(struct AROSEarlyFDT *, const void *, size_t);
int AROSEarlyFDTNext(const struct AROSEarlyFDT *, int node, int *depth);
int AROSEarlyFDTPath(const struct AROSEarlyFDT *, int node, char *, size_t);
int AROSEarlyFDTFindPath(const struct AROSEarlyFDT *, const char *);
const void *AROSEarlyFDTProperty(const struct AROSEarlyFDT *, int, const char *, int *);
int AROSEarlyFDTCompatible(const struct AROSEarlyFDT *, int, const char *);
int AROSEarlyFDTEnabled(const struct AROSEarlyFDT *, int);
/* Raw reg is in the parent bus address space. Translated reg walks ranges.
 * Scalar CPU/MMIO cells (0..2) are supported; PCI 3-cell encodings belong to
 * the PCI bus binding and are explicitly refused here. Missing ranges is not
 * an identity mapping. Zero-size CPU reg is valid only for RawReg. */
int AROSEarlyFDTRawReg(const struct AROSEarlyFDT *, int, unsigned, uint64_t *, uint64_t *);
int AROSEarlyFDTReg(const struct AROSEarlyFDT *, int, unsigned, uint64_t *, uint64_t *);
/* Resolves aliases and strips optional baud/options from chosen stdout-path. */
int AROSEarlyFDTStdout(const struct AROSEarlyFDT *);
/* Resolve interrupts[-extended] to supplier + uninterpreted cells. The
 * interrupt-controller binding, not this reader, decodes SPI/PPI numbers. */
int AROSEarlyFDTInterrupt(const struct AROSEarlyFDT *, int, unsigned, struct AROSFDTIRQ *);
int AROSEarlyFDTInterruptNamed(const struct AROSEarlyFDT *, int, const char *, struct AROSFDTIRQ *);
#endif
