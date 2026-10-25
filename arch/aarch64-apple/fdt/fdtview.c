/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Bounded read-only index of a flattened device tree.
 *
 * Written from the Devicetree Specification (flattened format, v17). The
 * structure block is walked once: every token, name and property must lie
 * inside its block, property names inside the strings block, and the node
 * nesting must balance. Lookups afterwards only read what was validated.
 */

#include <aros/apple/fdtview.h>

#define FDT_MAGIC UINT32_C(0xd00dfeed)
#define FDT_BEGIN_NODE UINT32_C(1)
#define FDT_END_NODE UINT32_C(2)
#define FDT_PROP UINT32_C(3)
#define FDT_NOP UINT32_C(4)
#define FDT_END UINT32_C(9)
#define FDT_HEADER_SIZE UINT32_C(40)

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
        ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint32_t align4(uint32_t value)
{
    return (value + 3U) & ~3U;
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

/* Length of a NUL-terminated string inside [start, end), or -1. */
static int32_t bounded_len(const uint8_t *base, uint32_t start, uint32_t end)
{
    uint32_t cursor;

    for (cursor = start; cursor < end; ++cursor)
        if (!base[cursor])
            return (int32_t)(cursor - start);
    return -1;
}

enum AROSAppleFDTViewStatus AROSAppleFDTViewOpen(
    struct AROSAppleFDTView *view, const void *blob, size_t available,
    struct AROSAppleFDTViewNode *nodes, uint32_t capacity)
{
    const uint8_t *data = blob;
    uint32_t total, off_struct, off_strings, size_struct, size_strings, version;
    uint32_t cursor, depth = 0, stack[AROS_APPLE_FDTVIEW_MAX_DEPTH];
    int ended = 0;

    if (!view || !data || !nodes || !capacity)
        return AROS_APPLE_FDTVIEW_ARGUMENT;
    view->count = 0;
    if (available < FDT_HEADER_SIZE || be32(data) != FDT_MAGIC)
        return AROS_APPLE_FDTVIEW_MALFORMED;
    total = be32(data + 4);
    off_struct = be32(data + 8);
    off_strings = be32(data + 12);
    version = be32(data + 20);
    size_strings = be32(data + 32);
    size_struct = be32(data + 36);
    if (total > available || total > AROS_APPLE_FDTVIEW_MAX_SIZE || version < 17 ||
            off_struct > total || size_struct > total - off_struct ||
            off_strings > total || size_strings > total - off_strings ||
            (off_struct & 3U) || (size_struct & 3U) || size_struct < 8)
        return AROS_APPLE_FDTVIEW_MALFORMED;

    view->data = data;
    view->structure = data + off_struct;
    view->strings = data + off_strings;
    view->total_size = total;
    view->structure_size = size_struct;
    view->strings_size = size_strings;
    view->nodes = nodes;
    view->capacity = capacity;

    for (cursor = 0; cursor + 4 <= size_struct && !ended;) {
        uint32_t token = be32(view->structure + cursor);

        cursor += 4;
        switch (token) {
        case FDT_BEGIN_NODE: {
            int32_t len = bounded_len(view->structure, cursor, size_struct);
            struct AROSAppleFDTViewNode *node;

            if (len < 0 || depth >= AROS_APPLE_FDTVIEW_MAX_DEPTH)
                return AROS_APPLE_FDTVIEW_MALFORMED;
            if (view->count == capacity)
                return AROS_APPLE_FDTVIEW_CAPACITY;
            if (!depth && view->count)
                return AROS_APPLE_FDTVIEW_MALFORMED;    /* a second root */
            node = &nodes[view->count];
            node->name = cursor;
            node->parent = depth ? stack[depth - 1] : AROS_APPLE_FDTVIEW_NONE;
            node->depth = depth;
            node->phandle = 0;
            cursor = align4(cursor + (uint32_t)len + 1U);
            node->begin = cursor;
            stack[depth++] = view->count++;
            break;
        }
        case FDT_END_NODE:
            if (!depth)
                return AROS_APPLE_FDTVIEW_MALFORMED;
            --depth;
            break;
        case FDT_PROP: {
            uint32_t len, nameoff;

            if (!depth || cursor + 8 > size_struct)
                return AROS_APPLE_FDTVIEW_MALFORMED;
            len = be32(view->structure + cursor);
            nameoff = be32(view->structure + cursor + 4);
            cursor += 8;
            if (len > size_struct - cursor || nameoff >= size_strings ||
                    bounded_len(view->strings, nameoff, size_strings) < 0)
                return AROS_APPLE_FDTVIEW_MALFORMED;
            if (len == 4 && (str_eq((const char *)view->strings + nameoff, "phandle") ||
                    str_eq((const char *)view->strings + nameoff, "linux,phandle")))
                nodes[stack[depth - 1]].phandle = be32(view->structure + cursor);
            cursor = align4(cursor + len);
            break;
        }
        case FDT_NOP:
            break;
        case FDT_END:
            ended = 1;
            break;
        default:
            return AROS_APPLE_FDTVIEW_MALFORMED;
        }
    }
    if (!ended || depth || !view->count) {
        view->count = 0;
        return AROS_APPLE_FDTVIEW_MALFORMED;
    }
    return AROS_APPLE_FDTVIEW_OK;
}

const char *AROSAppleFDTViewName(const struct AROSAppleFDTView *view, uint32_t node)
{
    if (!view || node >= view->count)
        return "";
    return (const char *)view->structure + view->nodes[node].name;
}

enum AROSAppleFDTViewStatus AROSAppleFDTViewProperty(
    const struct AROSAppleFDTView *view, uint32_t node, const char *name,
    const uint8_t **value, uint32_t *length)
{
    uint32_t cursor, depth = 0;

    if (!view || node >= view->count || !name)
        return AROS_APPLE_FDTVIEW_ARGUMENT;
    /* Properties come before the first child; the walk is already validated. */
    for (cursor = view->nodes[node].begin; cursor + 4 <= view->structure_size;) {
        uint32_t token = be32(view->structure + cursor);

        cursor += 4;
        if (token == FDT_PROP) {
            uint32_t len = be32(view->structure + cursor);
            uint32_t nameoff = be32(view->structure + cursor + 4);

            cursor += 8;
            if (!depth && str_eq((const char *)view->strings + nameoff, name)) {
                if (value)
                    *value = view->structure + cursor;
                if (length)
                    *length = len;
                return AROS_APPLE_FDTVIEW_OK;
            }
            cursor = align4(cursor + len);
        } else if (token == FDT_NOP) {
            continue;
        } else {
            break;      /* a child or the end of this node */
        }
    }
    return AROS_APPLE_FDTVIEW_NOT_FOUND;
}

int AROSAppleFDTViewHasProperty(
    const struct AROSAppleFDTView *view, uint32_t node, const char *name)
{
    return AROSAppleFDTViewProperty(view, node, name, NULL, NULL) == AROS_APPLE_FDTVIEW_OK;
}

uint32_t AROSAppleFDTViewCellCount(
    const struct AROSAppleFDTView *view, uint32_t node, const char *name)
{
    uint32_t length = 0;

    if (AROSAppleFDTViewProperty(view, node, name, NULL, &length) != AROS_APPLE_FDTVIEW_OK)
        return 0;
    return length / 4U;
}

enum AROSAppleFDTViewStatus AROSAppleFDTViewCell(
    const struct AROSAppleFDTView *view, uint32_t node, const char *name,
    uint32_t index, uint32_t *value)
{
    const uint8_t *data;
    uint32_t length;
    enum AROSAppleFDTViewStatus status;

    if (!value)
        return AROS_APPLE_FDTVIEW_ARGUMENT;
    status = AROSAppleFDTViewProperty(view, node, name, &data, &length);
    if (status != AROS_APPLE_FDTVIEW_OK)
        return status;
    if ((length & 3U) || index >= length / 4U)
        return AROS_APPLE_FDTVIEW_RANGE;
    *value = be32(data + 4U * index);
    return AROS_APPLE_FDTVIEW_OK;
}

enum AROSAppleFDTViewStatus AROSAppleFDTViewStringIndex(
    const struct AROSAppleFDTView *view, uint32_t node, const char *name,
    const char *string, uint32_t *index)
{
    const uint8_t *data;
    uint32_t length, cursor = 0, which = 0;
    enum AROSAppleFDTViewStatus status;

    status = AROSAppleFDTViewProperty(view, node, name, &data, &length);
    if (status != AROS_APPLE_FDTVIEW_OK)
        return status;
    while (cursor < length) {
        int32_t len = bounded_len(data, cursor, length);

        if (len < 0)
            return AROS_APPLE_FDTVIEW_MALFORMED;
        if (str_eq((const char *)data + cursor, string)) {
            if (index)
                *index = which;
            return AROS_APPLE_FDTVIEW_OK;
        }
        cursor += (uint32_t)len + 1U;
        ++which;
    }
    return AROS_APPLE_FDTVIEW_NOT_FOUND;
}

int AROSAppleFDTViewCompatible(
    const struct AROSAppleFDTView *view, uint32_t node, const char *compatible)
{
    return AROSAppleFDTViewStringIndex(view, node, "compatible", compatible, NULL) ==
        AROS_APPLE_FDTVIEW_OK;
}

int AROSAppleFDTViewEnabled(const struct AROSAppleFDTView *view, uint32_t node)
{
    const uint8_t *data;
    uint32_t length;

    if (AROSAppleFDTViewProperty(view, node, "status", &data, &length) != AROS_APPLE_FDTVIEW_OK)
        return 1;
    if (!length || data[length - 1])
        return 0;
    return str_eq((const char *)data, "okay") || str_eq((const char *)data, "ok");
}

uint32_t AROSAppleFDTViewNextCompatible(
    const struct AROSAppleFDTView *view, uint32_t after, const char *compatible)
{
    uint32_t node = after == AROS_APPLE_FDTVIEW_NONE ? 0 : after + 1U;

    if (!view)
        return AROS_APPLE_FDTVIEW_NONE;
    for (; node < view->count; ++node)
        if (AROSAppleFDTViewCompatible(view, node, compatible))
            return node;
    return AROS_APPLE_FDTVIEW_NONE;
}

uint32_t AROSAppleFDTViewByPhandle(const struct AROSAppleFDTView *view, uint32_t phandle)
{
    uint32_t node;

    if (!view || !phandle || phandle == AROS_APPLE_FDTVIEW_NONE)
        return AROS_APPLE_FDTVIEW_NONE;
    for (node = 0; node < view->count; ++node)
        if (view->nodes[node].phandle == phandle)
            return node;
    return AROS_APPLE_FDTVIEW_NONE;
}

uint32_t AROSAppleFDTViewChild(
    const struct AROSAppleFDTView *view, uint32_t parent, const char *name)
{
    uint32_t node;

    if (!view || parent >= view->count)
        return AROS_APPLE_FDTVIEW_NONE;
    for (node = parent + 1U; node < view->count; ++node) {
        if (view->nodes[node].depth <= view->nodes[parent].depth)
            break;
        if (view->nodes[node].parent == parent &&
                str_eq(AROSAppleFDTViewName(view, node), name))
            return node;
    }
    return AROS_APPLE_FDTVIEW_NONE;
}

static uint32_t cells_of(const struct AROSAppleFDTView *view, uint32_t node,
    const char *name, uint32_t fallback)
{
    uint32_t value;

    if (node == AROS_APPLE_FDTVIEW_NONE ||
            AROSAppleFDTViewCell(view, node, name, 0, &value) != AROS_APPLE_FDTVIEW_OK)
        return fallback;
    return value;
}

enum AROSAppleFDTViewStatus AROSAppleFDTViewReg(
    const struct AROSAppleFDTView *view, uint32_t node, uint32_t index,
    uint64_t *address, uint64_t *size)
{
    const uint8_t *data;
    uint32_t length, ac, sc, entry, i;
    enum AROSAppleFDTViewStatus status;
    uint64_t a = 0, s = 0;

    if (!view || node >= view->count || !address || !size)
        return AROS_APPLE_FDTVIEW_ARGUMENT;
    ac = cells_of(view, view->nodes[node].parent, "#address-cells", 2);
    sc = cells_of(view, view->nodes[node].parent, "#size-cells", 1);
    if (ac < 1 || ac > 2 || sc > 2)
        return AROS_APPLE_FDTVIEW_MALFORMED;
    status = AROSAppleFDTViewProperty(view, node, "reg", &data, &length);
    if (status != AROS_APPLE_FDTVIEW_OK)
        return status;
    entry = 4U * (ac + sc);
    if (!length || length % entry)
        return AROS_APPLE_FDTVIEW_MALFORMED;
    if (index >= length / entry)
        return AROS_APPLE_FDTVIEW_RANGE;
    data += entry * index;
    for (i = 0; i < ac; ++i)
        a = (a << 32) | be32(data + 4U * i);
    for (i = 0; i < sc; ++i)
        s = (s << 32) | be32(data + 4U * (ac + i));
    *address = a;
    *size = s;
    return AROS_APPLE_FDTVIEW_OK;
}
