/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Licensed under the AROS Public License (APL) Version 1.1.
 *
 * Detached geometry preparation. Every region operation is checked. No
 * live Layer/Region/ClipRect is modified here, including on allocation failure.
 */
#include <proto/exec.h>
#include <proto/graphics.h>
#include "layers_intern.h"
#include "preparedgeometry.h"

static LONG find_layer(struct PreparedLayer *layers, ULONG count,
    const struct Layer *layer)
{
    ULONG i;
    for (i = 0; i < count; ++i)
        if (layers[i].layer == layer) return (LONG)i;
    return -1;
}

static BOOL same_bounds(const struct Rectangle *a, const struct Rectangle *b)
{
    return a->MinX == b->MinX && a->MinY == b->MinY &&
        a->MaxX == b->MaxX && a->MaxY == b->MaxY;
}

static BOOL build_shape(struct PreparedLayer *layers, ULONG count, ULONG index,
    struct LayerGeometryRefusal *refusal, struct LayersBase *LayersBase)
{
    struct PreparedLayer *entry = &layers[index];
    struct PreparedLayerFields *next = &entry->next;
    struct Rectangle local = {0, 0, next->width - 1, next->height - 1};
    LONG parent;
    if (entry->shape_state == 2) return TRUE;
    /* malformed parent cycle */
    if (entry->shape_state) return LayerRefusal(refusal, LGR_PARENT, entry->layer, 1, 0, 0, 0);
    entry->shape_state = 1;
    if (!OrRectRegion(next->shape, &local) ||
        (next->customshape && !AndRegionRegion(next->customshape, next->shape)))
        return LayerRefusalRegion(refusal, entry->layer);
    _TranslateRect(&next->shape->bounds, next->bounds.MinX, next->bounds.MinY);
    if (!SetRegion(next->shape, next->visibleshape)) return LayerRefusalRegion(refusal, entry->layer);
    if (entry->layer->parent)
    {
        parent = find_layer(layers, count, entry->layer->parent);
        if (parent < 0) return LayerRefusal(refusal, LGR_PARENT, entry->layer, 0, 0, 0, 0);
        if (!build_shape(layers, count, (ULONG)parent, refusal, LayersBase)) return FALSE;
        if (!AndRegionRegion(layers[parent].next.visibleshape, next->visibleshape))
            return LayerRefusalRegion(refusal, entry->layer);
    }
    entry->shape_state = 2;
    return TRUE;
}

static void capture_fields(struct Layer *layer, struct PreparedLayerFields *f)
{
    f->bounds = layer->bounds;
    f->width = layer->Width;
    f->height = layer->Height;
    f->flags = layer->Flags;
    f->shape = layer->shape;
    f->visibleshape = layer->visibleshape;
    f->visible = layer->VisibleRegion;
    f->damage = layer->DamageList;
    f->customshape = layer->shaperegion;
    f->clips = layer->ClipRect;
    f->regular = layer->_cliprects;
}

BOOL PrepareLayerRegions(struct PreparedLayer *layers, ULONG count,
    const struct LayerGeometryRequest *requests, ULONG request_count,
    struct LayerGeometryRefusal *refusal, struct LayersBase *LayersBase)
{
    ULONG i, j;
    for (i = 0; i < count; ++i)
    {
        struct Layer *layer = layers[i].layer;
        struct PreparedLayerFields *next = &layers[i].next;
        BOOL requested = FALSE, resolved = FALSE;
        if (layer->Flags & LAYERUPDATING) return LayerRefusal(refusal, LGR_UPDATING, layer, 0, 0, 0, 0);
        capture_fields(layer, &layers[i].old);
        next->bounds = layer->bounds;
        next->customshape = layer->shaperegion;
        next->flags = layer->Flags;
        for (j = 0; j < request_count; ++j)
            if (requests[j].layer == layer)
            {
                if (requested)
                    return LayerRefusal(refusal, LGR_DUPLICATE_REQUEST, layer, j, 0, 0, 0);
                if (requests[j].flags & ~(LGT_REPLACE_SHAPE | LGT_RESOLVED_SHAPE))
                    return LayerRefusal(refusal, LGR_REQUEST_FLAGS, layer, j, requests[j].flags, 0, 0);
                requested = TRUE;
                next->bounds = requests[j].bounds;
                if (requests[j].flags & LGT_REPLACE_SHAPE)
                    next->customshape = requests[j].shape;
                resolved = (requests[j].flags & LGT_RESOLVED_SHAPE) != 0;
                if (resolved && IL(layer)->shapehook != requests[j].resolved_hook)
                    return LayerRefusal(refusal, LGR_HOOK_MISMATCH, layer, j,
                        IL(layer)->shapehook != NULL, 0, 0);
            }
        {
            LONG width = (LONG)next->bounds.MaxX - next->bounds.MinX + 1;
            LONG height = (LONG)next->bounds.MaxY - next->bounds.MinY + 1;
            if (width <= 0 || height <= 0 || width > 32767 || height > 32767)
                return LayerRefusal(refusal, LGR_BOUNDS, layer, width, height, 0, 0);
            next->width = (WORD)width;
            next->height = (WORD)height;
        }
        if (IL(layer)->shapehook && !resolved &&
            (!same_bounds(&next->bounds, &layer->bounds) ||
             next->customshape != layer->shaperegion))
            return LayerRefusal(refusal, LGR_SHAPE_HOOK, layer, next->width, next->height,
                next->customshape != layer->shaperegion, 0);
        next->shape = NewRegion();
        next->visibleshape = NewRegion();
        next->visible = NewRegion();
        next->damage = NewRegion();
        if (!next->shape || !next->visibleshape || !next->visible || !next->damage ||
            !SetRegion(layer->DamageList, next->damage)) return LayerRefusalRegion(refusal, layer);
        {
            struct Rectangle local = {0, 0, next->width - 1, next->height - 1};
            /* AndRectRegion only clips/removes existing rectangles; unlike
             * region/region algebra it is void and does not allocate. */
            AndRectRegion(next->damage, &local);
        }
    }
    /* Every request must name a pinned member, never a stale external Layer. */
    for (i = 0; i < request_count; ++i)
        if (find_layer(layers, count, requests[i].layer) < 0)
            return LayerRefusal(refusal, LGR_STALE_REQUEST, requests[i].layer, i, 0, 0, 0);
    for (i = 0; i < count; ++i)
        if (!build_shape(layers, count, i, refusal, LayersBase)) return FALSE;
    for (i = 0; i < count; ++i)
    {
        struct Layer *layer = layers[i].layer, *front;
        LONG root = find_layer(layers, count, layer->LayerInfo->check_lp);
        if (root < 0) return LayerRefusal(refusal, LGR_STACK_BITMAP, layer, -1, 0, 0, 0);
        if (!SetRegion(layers[root].next.shape, layers[i].next.visible))
            return LayerRefusalRegion(refusal, layer);
        for (front = layer->front; front; front = front->front)
        {
            LONG index = find_layer(layers, count, front);
            if (index < 0) return LayerRefusal(refusal, LGR_FRONT, layer, 0, 0, 0, 0);
            if (IS_VISIBLE(front) &&
                !ClearRegionRegion(layers[index].next.visibleshape, layers[i].next.visible))
                return LayerRefusalRegion(refusal, layer);
        }
        if (!IS_VISIBLE(layer)) ClearRegion(layers[i].next.visible);
    }
    return TRUE;
}

void ReleasePreparedLayerRegions(struct PreparedLayer *layers, ULONG count,
    BOOL committed, struct LayersBase *LayersBase)
{
    ULONG i;
    for (i = 0; i < count; ++i)
    {
        struct PreparedLayerFields *loser = committed ? &layers[i].old : &layers[i].next;
        if (loser->shape) DisposeRegion(loser->shape);
        if (loser->visibleshape) DisposeRegion(loser->visibleshape);
        if (loser->visible) DisposeRegion(loser->visible);
        if (loser->damage) DisposeRegion(loser->damage);
        loser->shape = loser->visibleshape = loser->visible = loser->damage = NULL;
    }
}

void InstallPreparedLayerFields(struct PreparedLayer *layers, ULONG count,
    BOOL rollback)
{
    ULONG i;
    for (i = 0; i < count; ++i)
    {
        struct Layer *layer = layers[i].layer;
        const struct PreparedLayerFields *f = rollback ? &layers[i].old : &layers[i].next;
        layer->bounds = f->bounds;
        layer->Width = f->width;
        layer->Height = f->height;
        layer->Flags = f->flags;
        layer->shape = f->shape;
        layer->visibleshape = f->visibleshape;
        layer->VisibleRegion = f->visible;
        layer->DamageList = f->damage;
        layer->shaperegion = f->customshape;
        layer->ClipRect = f->clips;
        layer->_cliprects = f->regular;
    }
}
