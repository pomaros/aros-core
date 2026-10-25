#ifndef HYPERLAYERS_PREPAREDGEOMETRY_H
#define HYPERLAYERS_PREPAREDGEOMETRY_H
/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <graphics/layertransaction.h>
#include <graphics/regions.h>

/* Explicit install record: never copy a public Layer containing semaphores,
 * client-owned RastPorts, topology or hooks over another Layer. */
struct PreparedLayerFields
{
    struct Rectangle bounds;
    WORD width, height;
    UWORD flags;
    struct Region *shape, *visibleshape, *visible, *damage;
    struct Region *customshape; /* borrowed */
    struct ClipRect *clips, *regular;
};

struct PreparedLayer
{
    struct Layer *layer;
    struct PreparedLayerFields old, next;
    UBYTE shape_state;
    struct PreparedPixel *content;
    ULONG stride;
    struct PreparedPixels *super_pixels; /* shared per unique SuperBitMap */
    BOOL owns_super_pixels;
    struct Region *valid, *backfill;
};

/* Record the refusing check; a later blame adds the layer when the refusing
 * step (a pixel lease) did not know it. NULL records nothing. */
static inline BOOL LayerRefusal(struct LayerGeometryRefusal *r, ULONG check,
    struct Layer *layer, LONG v0, LONG v1, LONG v2, LONG v3)
{
    ULONG i;
    if (!r) return FALSE;
    for (i = 0; i < sizeof(*r); ++i) ((UBYTE *)r)[i] = 0;
    r->lgr_Check = check;
    r->lgr_Layer = layer;
    r->lgr_Value[0] = v0; r->lgr_Value[1] = v1;
    r->lgr_Value[2] = v2; r->lgr_Value[3] = v3;
    return FALSE;
}
static inline BOOL LayerRefusalBlame(struct LayerGeometryRefusal *r, struct Layer *layer)
{
    if (r && r->lgr_Check != LGR_NONE && !r->lgr_Layer) r->lgr_Layer = layer;
    return FALSE;
}
static inline BOOL LayerRefusalRegion(struct LayerGeometryRefusal *r, struct Layer *layer)
{
    return LayerRefusal(r, LGR_NO_MEMORY, layer, 0, LGR_MEM_REGION, 0, 0);
}

BOOL PrepareLayerRegions(struct PreparedLayer *layers, ULONG count,
    const struct LayerGeometryRequest *requests, ULONG request_count,
    struct LayerGeometryRefusal *refusal, struct LayersBase *LayersBase);
void ReleasePreparedLayerRegions(struct PreparedLayer *layers, ULONG count,
    BOOL committed, struct LayersBase *LayersBase);
void InstallPreparedLayerFields(struct PreparedLayer *layers, ULONG count,
    BOOL rollback);
BOOL PrepareLayerClips(struct PreparedLayer *entry, struct LayerGeometryRefusal *refusal,
    struct LayersBase *LayersBase);
BOOL FillPreparedLayerClips(struct PreparedLayer *entry, struct LayerGeometryRefusal *refusal,
    struct LayersBase *LayersBase);
void ReleasePreparedLayerClips(struct PreparedLayer *entry, BOOL committed,
    struct LayersBase *LayersBase);
#endif
