/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: A bounded, read-only index of the final FDT for drivers that follow
 *       phandles between nodes (audio: bus, codec, MCA, DMA, clocks, pins).
 *
 * The blob is validated once and its nodes are recorded in a table the
 * caller provides; nothing is allocated and the blob is never modified.
 * Every lookup is bounded by the blob and the table.
 */

#ifndef AROS_AARCH64_APPLE_FDTVIEW_H
#define AROS_AARCH64_APPLE_FDTVIEW_H

#include <stddef.h>
#include <stdint.h>

#define AROS_APPLE_FDTVIEW_MAX_SIZE (UINT32_C(2) * 1024U * 1024U)
#define AROS_APPLE_FDTVIEW_MAX_DEPTH UINT32_C(32)
#define AROS_APPLE_FDTVIEW_NONE UINT32_C(0xffffffff)

enum AROSAppleFDTViewStatus {
    AROS_APPLE_FDTVIEW_OK = 0,
    AROS_APPLE_FDTVIEW_ARGUMENT,
    AROS_APPLE_FDTVIEW_MALFORMED,
    AROS_APPLE_FDTVIEW_CAPACITY,
    AROS_APPLE_FDTVIEW_NOT_FOUND,
    AROS_APPLE_FDTVIEW_RANGE
};

struct AROSAppleFDTViewNode {
    uint32_t begin;         /* offset of the node's properties in the structure block */
    uint32_t name;          /* offset of its name in the structure block */
    uint32_t parent;        /* index, AROS_APPLE_FDTVIEW_NONE for the root */
    uint32_t phandle;       /* 0 when none */
    uint32_t depth;
};

struct AROSAppleFDTView {
    const uint8_t *data;
    const uint8_t *structure;
    const uint8_t *strings;
    uint32_t total_size;
    uint32_t structure_size;
    uint32_t strings_size;
    struct AROSAppleFDTViewNode *nodes;
    uint32_t count;
    uint32_t capacity;
};

enum AROSAppleFDTViewStatus AROSAppleFDTViewOpen(
    struct AROSAppleFDTView *view, const void *blob, size_t available,
    struct AROSAppleFDTViewNode *nodes, uint32_t capacity);

/* The node's unit name ("i2c@23501c000"); the root is "". */
const char *AROSAppleFDTViewName(const struct AROSAppleFDTView *view, uint32_t node);

enum AROSAppleFDTViewStatus AROSAppleFDTViewProperty(
    const struct AROSAppleFDTView *view, uint32_t node, const char *name,
    const uint8_t **value, uint32_t *length);

/* Big-endian cell `index` of a property; RANGE past its end. */
enum AROSAppleFDTViewStatus AROSAppleFDTViewCell(
    const struct AROSAppleFDTView *view, uint32_t node, const char *name,
    uint32_t index, uint32_t *value);

/* Number of 32-bit cells of a property, 0 when absent. */
uint32_t AROSAppleFDTViewCellCount(
    const struct AROSAppleFDTView *view, uint32_t node, const char *name);

int AROSAppleFDTViewHasProperty(
    const struct AROSAppleFDTView *view, uint32_t node, const char *name);

/* "compatible" contains this exact string. */
int AROSAppleFDTViewCompatible(
    const struct AROSAppleFDTView *view, uint32_t node, const char *compatible);

/* No "status", or "okay"/"ok". */
int AROSAppleFDTViewEnabled(const struct AROSAppleFDTView *view, uint32_t node);

/* Index of the string in a string-list property ("dma-names"). */
enum AROSAppleFDTViewStatus AROSAppleFDTViewStringIndex(
    const struct AROSAppleFDTView *view, uint32_t node, const char *name,
    const char *string, uint32_t *index);

/* Next node after `after` (NONE to start) carrying the compatible string. */
uint32_t AROSAppleFDTViewNextCompatible(
    const struct AROSAppleFDTView *view, uint32_t after, const char *compatible);

uint32_t AROSAppleFDTViewByPhandle(const struct AROSAppleFDTView *view, uint32_t phandle);

/* A direct child by unit name ("cpu" under a dai-link). */
uint32_t AROSAppleFDTViewChild(
    const struct AROSAppleFDTView *view, uint32_t node, const char *name);

/*
 * Entry `index` of the node's "reg", sized by its parent's #address-cells
 * and #size-cells (defaults 2 and 1). The address is the bus address the
 * parent sees; Apple's MMIO parents map 1:1 ("ranges;").
 */
enum AROSAppleFDTViewStatus AROSAppleFDTViewReg(
    const struct AROSAppleFDTView *view, uint32_t node, uint32_t index,
    uint64_t *address, uint64_t *size);

#endif /* AROS_AARCH64_APPLE_FDTVIEW_H */
