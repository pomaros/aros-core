/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Licensed under the AROS Public License (APL) Version 1.1.
 *
 * Create completely independent regular and installed-clip lists. Every
 * smart hidden rectangle owns its bitmap: no pending pointer aliases a live
 * backing store and failed preparation cannot alter an old pixel.
 */
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <string.h>
#include "layers_intern.h"
#include "preparedgeometry.h"
#include "preparedpixels.h"

static void free_clips(struct ClipRect *clip, struct Layer *layer,
    struct LayersBase *LayersBase)
{
    while (clip)
    {
        struct ClipRect *next = clip->Next;
        if (IS_SMARTREFRESH(layer) && clip->BitMap && !(clip->Flags & CR_SHAREDBITMAP))
            FreeBitMap(clip->BitMap);
        FreePooled(LayersBase->lb_ClipRectPool, clip, sizeof(*clip));
        clip = next;
    }
}

static BOOL append_region(struct PreparedLayer *entry, struct Region *region,
    BOOL hidden, struct ClipRect **list, struct LayerGeometryRefusal *refusal,
    struct LayersBase *LayersBase)
{
    struct RegionRectangle *rr;
    for (rr = region->RegionRectangle; rr; rr = rr->Next)
    {
        struct ClipRect *clip = AllocPooled(LayersBase->lb_ClipRectPool, sizeof(*clip));
        ULONG width, height;
        if (!clip)
            return LayerRefusal(refusal, LGR_NO_MEMORY, entry->layer, sizeof(*clip), LGR_MEM_CLIP, 0, 0);
        /* No live SuperSaveClipRects cache is consumed during preparation. */
        memset(clip, 0, sizeof(*clip));
        clip->bounds = rr->bounds;
        _TranslateRect(&clip->bounds, region->bounds.MinX, region->bounds.MinY);
        clip->lobs = hidden ? (struct Layer *)(IPTR)1 : NULL;
        clip->Next = *list;
        *list = clip;
        if (!hidden || !IS_SMARTREFRESH(entry->layer)) continue;
        width = (LONG)clip->bounds.MaxX - clip->bounds.MinX + 1;
        height = (LONG)clip->bounds.MaxY - clip->bounds.MinY + 1;
        clip->BitMap = AllocBitMap(width + ALIGN_CLIPRECT, height,
            GetBitMapAttr(entry->layer->rp->BitMap, BMA_DEPTH), BMF_CLEAR,
            entry->layer->rp->BitMap);
        if (!clip->BitMap)
            return LayerRefusal(refusal, LGR_NO_MEMORY, entry->layer, 0, LGR_MEM_BACKING,
                width + ALIGN_CLIPRECT, height);
        CR_SETBMOFF(clip, ALIGN_OFFSET(clip->bounds.MinX), 0);
    }
    return TRUE;
}

static BOOL make_list(struct PreparedLayer *entry, struct Region *extent,
    struct Region *visible, struct ClipRect **result, struct LayerGeometryRefusal *refusal,
    struct LayersBase *LayersBase)
{
    struct Region *hidden = NewRegion();
    BOOL success = FALSE;
    if (!hidden) return LayerRefusalRegion(refusal, entry->layer);
    if (!SetRegion(extent, hidden) || !ClearRegionRegion(visible, hidden))
        LayerRefusalRegion(refusal, entry->layer);
    else if (append_region(entry, visible, FALSE, result, refusal, LayersBase) &&
        append_region(entry, hidden, TRUE, result, refusal, LayersBase)) success = TRUE;
    DisposeRegion(hidden);
    return success;
}

BOOL PrepareLayerClips(struct PreparedLayer *entry, struct LayerGeometryRefusal *refusal,
    struct LayersBase *LayersBase)
{
    struct Region *extent = NULL, *visible = NULL, *clipregion = NULL;
    struct PreparedLayerFields *next = &entry->next;
    BOOL success = FALSE;
    if (entry->layer == entry->layer->LayerInfo->check_lp) return TRUE;
    extent = NewRegion(); visible = NewRegion();
    if (!extent || !visible || !OrRectRegion(extent, &next->bounds) ||
        !SetRegion(next->visible, visible) || !AndRegionRegion(next->shape, visible))
    { LayerRefusalRegion(refusal, entry->layer); goto done; }
    if (!make_list(entry, extent, visible, &next->clips, refusal, LayersBase)) goto done;
    if (entry->layer->ClipRegion)
    {
        next->regular = next->clips;
        next->clips = NULL;
        clipregion = NewRegion();
        if (!clipregion || !SetRegion(entry->layer->ClipRegion, clipregion))
        { LayerRefusalRegion(refusal, entry->layer); goto done; }
        _TranslateRect(&clipregion->bounds, next->bounds.MinX, next->bounds.MinY);
        if (!AndRegionRegion(clipregion, extent) || !AndRegionRegion(extent, visible))
        { LayerRefusalRegion(refusal, entry->layer); goto done; }
        if (!make_list(entry, extent, visible, &next->clips, refusal, LayersBase)) goto done;
    }
    success = TRUE;
done:
    if (extent) DisposeRegion(extent);
    if (visible) DisposeRegion(visible);
    if (clipregion) DisposeRegion(clipregion);
    return success;
}

BOOL FillPreparedLayerClips(struct PreparedLayer *entry, struct LayerGeometryRefusal *refusal,
    struct LayersBase *LayersBase)
{
    struct ClipRect *lists[2] = {entry->next.clips, entry->next.regular};
    ULONG i;
    (void)LayersBase;
    for (i = 0; i < 2; ++i)
    {
        struct ClipRect *clip;
        for (clip = lists[i]; clip; clip = clip->Next)
        {
            struct PreparedPixels *pixels;
            const struct PreparedPixel *source;
            BOOL success;
            if (!clip->BitMap) continue;
            source = (const struct PreparedPixel *)((const UBYTE *)entry->content +
                (ULONG)((LONG)clip->bounds.MinY - entry->next.bounds.MinY) * entry->stride) +
                ((LONG)clip->bounds.MinX - entry->next.bounds.MinX);
            pixels = PreparePixels(clip->BitMap, TRUE, refusal, LGR_PIXELS_BACKING);
            if (!pixels) return LayerRefusalBlame(refusal, entry->layer);
            success = WritePreparedPixels(pixels, CR_BMOFFX(clip), CR_BMOFFY(clip),
                (LONG)clip->bounds.MaxX - clip->bounds.MinX + 1,
                (LONG)clip->bounds.MaxY - clip->bounds.MinY + 1, source, entry->stride);
            /* This bitmap is unpublished and has no update/backfill hook. */
            if (success) success = SealPreparedPixels(pixels);
            if (success) CommitPreparedPixels(pixels);
            ReleasePreparedPixels(pixels);
            if (!success) return LayerRefusalBlame(refusal, entry->layer);
        }
    }
    return TRUE;
}

void ReleasePreparedLayerClips(struct PreparedLayer *entry, BOOL committed,
    struct LayersBase *LayersBase)
{
    struct PreparedLayerFields *loser = committed ? &entry->old : &entry->next;
    /* Existing installed-clip views can share backing with the regular list.
     * CR_SHAREDBITMAP marks borrowed views; the regular owner frees it once. */
    free_clips(loser->clips, entry->layer, LayersBase);
    free_clips(loser->regular, entry->layer, LayersBase);
    loser->clips = loser->regular = NULL;
}
