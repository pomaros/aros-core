/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Materialize the existing OpenFirmware node API from a retained FDT. */
#include <aros/macros.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include "of_intern.h"
#include "early_fdt.h"
#include <libfdt.h>

/* This tree owns its metadata only. Names and values borrow retained FDT
 * storage, just as the Raspberry Pi boot tree does. Depth is reader-bounded. */
static void free_tree(of_node_t *node)
{
    of_node_t *child = (of_node_t *)node->on_children.mlh_Head;
    of_property_t *prop = (of_property_t *)node->on_properties.mlh_Head;
    while (child->on_node.mln_Succ) {
        of_node_t *next = (of_node_t *)child->on_node.mln_Succ;
        free_tree(child); child = next;
    }
    while (prop->op_node.mln_Succ) {
        of_property_t *next = (of_property_t *)prop->op_node.mln_Succ;
        FreeVec(prop); prop = next;
    }
    FreeVec(node);
}

of_node_t *OFBuildFDT(const void *blob)
{
    struct AROSEarlyFDT tree;
    of_node_t *parents[AROS_FDT_MAX_DEPTH + 1], *root = NULL;
    int offset = -1, depth = -1;
    uint32_t properties = 0;
    /* The boot tag is a trusted retained pointer, not an application pointer.
     * The reader validates the full declared blob before constructing nodes. */
    if (!blob || fdt_magic(blob) != FDT_MAGIC ||
        !AROSEarlyFDTOpen(&tree, blob, fdt_totalsize(blob))) return NULL;
    while ((offset = AROSEarlyFDTNext(&tree, offset, &depth)) >= 0 && depth >= 0) {
        int bytes, property;
        of_node_t *node = AllocVec(sizeof(*node), MEMF_ANY | MEMF_CLEAR);
        if (!node) goto fail;
        NEWLIST(&node->on_children); NEWLIST(&node->on_properties);
        node->on_name = (char *)fdt_get_name(blob, offset, &bytes);
        if (!node->on_name || bytes < 0) { FreeVec(node); goto fail; }
        if (depth) ADDTAIL(&parents[depth-1]->on_children, node);
        else root = node;
        parents[depth] = node;
        fdt_for_each_property_offset(property, blob, offset) {
            const char *name;
            const void *value = fdt_getprop_by_offset(blob, property, &name, &bytes);
            if (!value || bytes < 0 || ++properties > 16384) goto fail;
            of_property_t *prop = AllocVec(sizeof(*prop), MEMF_ANY | MEMF_CLEAR);
            if (!prop) goto fail;
            prop->op_name = (char *)name; prop->op_length = (uint32_t)bytes;
            prop->op_value = (void *)value;
            ADDTAIL(&node->on_properties, prop);
        }
        if (property != -FDT_ERR_NOTFOUND) goto fail;
    }
    return root;
fail:
    if (root) free_tree(root);
    return NULL;
}
