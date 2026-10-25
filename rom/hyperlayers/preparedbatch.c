/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Licensed under the AROS Public License (APL) Version 1.1.
 *
 * Complete detached layer-stack generations. Preparation owns all fallible
 * region, bitmap and pixel work. Public objects keep their addresses, hooks
 * and locks; commit and rollback only install prepared fields and raw pixels.
 */
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <string.h>
#include "layers_intern.h"
#include "preparedgeometry.h"
#include "basicfuncs.h"
#include "preparedpixels.h"

struct PreparedStack
{
    struct Layer_Info *info;
    struct BitMap *bitmap;
    struct PreparedPixels *pixels;
    BOOL info_locked;
};

enum { BATCH_PREPARING, BATCH_SEALED, BATCH_INSTALLING, BATCH_COMMITTED, BATCH_FINISHED, BATCH_ROLLED_BACK };
struct LayerGeometryBatch
{
    struct PreparedStack *stacks;
    struct PreparedLayer *layers;
    struct LayerGeometryRefusal *refusal; /* caller-owned, borrowed */
    ULONG stack_count, layer_count, layer_locks;
    UBYTE state;
};

/* Mark where the refusing layer lives once the check is known. A stale or
 * non-member layer is only an identity: it is never dereferenced. */
static void place_refusal(struct LayerGeometryRefusal *r, struct Layer_Info *root)
{
    struct Layer *layer;
    if (!r || !(layer = r->lgr_Layer) || r->lgr_Check == LGR_STALE_REQUEST ||
        r->lgr_Check == LGR_NOT_MEMBER || !layer->LayerInfo) return;
    r->lgr_Flags = 0;
    if (layer == layer->LayerInfo->check_lp) r->lgr_Flags |= LGRF_ROOT;
    if (layer->LayerInfo != root) r->lgr_Flags |= LGRF_PRIVATE;
}
static BOOL batch_refuse(struct LayerGeometryBatch *batch, ULONG check,
    struct Layer *layer, LONG v0, LONG v1, LONG v2, LONG v3)
{
    if (!batch) return FALSE;
    LayerRefusal(batch->refusal, check, layer, v0, v1, v2, v3);
    place_refusal(batch->refusal, batch->stacks[0].info);
    return FALSE;
}
static BOOL batch_blame(struct LayerGeometryBatch *batch, struct Layer *layer)
{
    LayerRefusalBlame(batch->refusal, layer);
    place_refusal(batch->refusal, batch->stacks[0].info);
    return FALSE;
}

static struct PreparedStack *find_stack(struct LayerGeometryBatch *batch,
    struct Layer_Info *info)
{
    ULONG i;
    for (i = 0; i < batch->stack_count; ++i)
        if (batch->stacks[i].info == info) return &batch->stacks[i];
    return NULL;
}

static void *allocate_array(ULONG count, ULONG size)
{
    if (!count || !size || count > 0xffffffffUL / size) return NULL;
    return AllocVec(count * size, MEMF_PUBLIC | MEMF_CLEAR);
}
/* Saturated byte count for a refusal value. */
static LONG array_bytes(ULONG count, ULONG size)
{
    return (!size || count > 0x7fffffffUL / size) ? 0x7fffffff : (LONG)(count * size);
}

static BOOL rect_intersection(const struct Rectangle *a, const struct Rectangle *b,
    struct Rectangle *result)
{
    result->MinX = a->MinX > b->MinX ? a->MinX : b->MinX;
    result->MinY = a->MinY > b->MinY ? a->MinY : b->MinY;
    result->MaxX = a->MaxX < b->MaxX ? a->MaxX : b->MaxX;
    result->MaxY = a->MaxY < b->MaxY ? a->MaxY : b->MaxY;
    return result->MinX <= result->MaxX && result->MinY <= result->MaxY;
}

/* Fill content from the old regular list, then the active clipped list. The
 * latter can own newer hidden pixels than its regular backing. Neither source
 * list is uninstalled or copied back into the live other list. */
static BOOL read_clips(struct LayerGeometryBatch *batch, struct PreparedLayer *entry,
    struct ClipRect *clip, struct LayersBase *LayersBase)
{
    for (; clip; clip = clip->Next)
    {
        struct Rectangle source, kept;
        struct PreparedPixels *pixels;
        struct PreparedPixel *destination;
        LONG dx = (LONG)entry->next.bounds.MinX - entry->old.bounds.MinX;
        LONG dy = (LONG)entry->next.bounds.MinY - entry->old.bounds.MinY;
        LONG sx, sy, x, y;
        BOOL temporary = FALSE, success;
        if (clip->lobs && (!IS_SMARTREFRESH(entry->layer) || !clip->BitMap)) continue;
        source.MinX = entry->old.bounds.MinX;
        source.MinY = entry->old.bounds.MinY;
        /* The source viewport can extend beyond WORD even though both old
         * and proposed actual bounds fit. Clip in LONG before narrowing. */
        sx = (LONG)source.MinX + entry->next.width - 1;
        sy = (LONG)source.MinY + entry->next.height - 1;
        source.MaxX = sx > 32767 ? 32767 : (WORD)sx;
        source.MaxY = sy > 32767 ? 32767 : (WORD)sy;
        if (!rect_intersection(&source, &clip->bounds, &kept)) continue;
        x = (LONG)kept.MinX - entry->old.bounds.MinX;
        y = (LONG)kept.MinY - entry->old.bounds.MinY;
        if (clip->lobs)
        {
            pixels = PreparePixels(clip->BitMap, FALSE, batch->refusal, LGR_PIXELS_OBSCURED);
            temporary = TRUE;
            sx = (LONG)kept.MinX - clip->bounds.MinX + CR_BMOFFX(clip);
            sy = (LONG)kept.MinY - clip->bounds.MinY + CR_BMOFFY(clip);
        }
        else
        {
            struct PreparedStack *stack = find_stack(batch, entry->layer->LayerInfo);
            pixels = stack ? stack->pixels : NULL;
            sx = kept.MinX; sy = kept.MinY;
        }
        if (!pixels)
            return temporary ? batch_blame(batch, entry->layer) :
                batch_refuse(batch, LGR_NOT_MEMBER, entry->layer, 0, 0, 0, 0);
        destination = (struct PreparedPixel *)((UBYTE *)entry->content + (ULONG)y * entry->stride) + x;
        if (sx < 0 || sy < 0)
        {
            if (temporary) ReleasePreparedPixels(pixels);
            return batch_refuse(batch, LGR_SCREEN_OFFSET, entry->layer, sx, sy, 0, 0);
        }
        success = ReadOldPixels(pixels, (ULONG)sx, (ULONG)sy,
            (LONG)kept.MaxX - kept.MinX + 1, (LONG)kept.MaxY - kept.MinY + 1,
            destination, entry->stride);
        if (temporary) ReleasePreparedPixels(pixels);
        if (!success) return batch_blame(batch, entry->layer);
        _TranslateRect(&kept, dx, dy);
        if (!OrRectRegion(entry->valid, &kept)) return batch_refuse(batch, LGR_NO_MEMORY,
            entry->layer, 0, LGR_MEM_REGION, 0, 0);
    }
    return TRUE;
}

/* A SUPER viewport may extend beyond its backing, in either direction.
 * Return its backed portion in layer-local coordinates, without narrowing
 * negative scroll or large bitmap dimensions to public WORD rectangles. */
static BOOL super_viewport(const struct PreparedLayer *entry,
    ULONG *left, ULONG *top, ULONG *width, ULONG *height)
{
    LONG x = entry->layer->Scroll_X < 0 ? -(LONG)entry->layer->Scroll_X : 0;
    LONG y = entry->layer->Scroll_Y < 0 ? -(LONG)entry->layer->Scroll_Y : 0;
    LONG right = (LONG)PreparedPixelsWidth(entry->super_pixels) - entry->layer->Scroll_X;
    LONG bottom = (LONG)PreparedPixelsHeight(entry->super_pixels) - entry->layer->Scroll_Y;
    if (right > entry->next.width) right = entry->next.width;
    if (bottom > entry->next.height) bottom = entry->next.height;
    if (x >= right || y >= bottom) return FALSE;
    *left = x; *top = y; *width = right-x; *height = bottom-y;
    return TRUE;
}

static BOOL prepare_content(struct LayerGeometryBatch *batch, struct PreparedLayer *entry,
    struct LayersBase *LayersBase)
{
    struct Layer *layer = entry->layer;
    struct Region *missing = NULL;
    struct PreparedLayerFields *next = &entry->next;
    BOOL success = FALSE;
    entry->stride = (ULONG)next->width * sizeof(struct PreparedPixel);
    BOOL root = layer == layer->LayerInfo->check_lp;
    ULONG rows = root ? 1 : (ULONG)next->height;
    entry->content = allocate_array(rows, entry->stride);
    if (!entry->content)
        return batch_refuse(batch, LGR_NO_MEMORY, layer, array_bytes(rows, entry->stride),
            LGR_MEM_CONTENT, next->width, rows);
    entry->valid = NewRegion();
    if (!entry->valid) return batch_refuse(batch, LGR_NO_MEMORY, layer, 0, LGR_MEM_REGION, 0, 0);
    {
        struct PreparedPixel seed;
        struct PreparedStack *stack = find_stack(batch, layer->LayerInfo);
        ULONG pixel, count = rows * (ULONG)next->width;
        if (!stack) return batch_refuse(batch, LGR_NOT_MEMBER, layer, 0, 0, 0, 0);
        if (!PreparedPenColor(stack->pixels, stack->bitmap, 0, &seed))
            return batch_refuse(batch, LGR_STACK_BITMAP, layer, 0, 0, 0, 0);
        for (pixel = 0; pixel < count; ++pixel) entry->content[pixel] = seed;
        /* NOBACKFILL leaves newly exposed destination pixels untouched. For
         * moved client content, seed those missing local pixels from the old
         * destination rather than manufacturing black/pen-zero pixels. */
        if (!root && layer->BackFill == LAYERS_NOBACKFILL)
        {
            LONG x = next->bounds.MinX < 0 ? 0 : next->bounds.MinX;
            LONG y = next->bounds.MinY < 0 ? 0 : next->bounds.MinY;
            LONG right = (LONG)next->bounds.MaxX + 1;
            LONG bottom = (LONG)next->bounds.MaxY + 1;
            if (right > (LONG)PreparedPixelsWidth(stack->pixels)) right = PreparedPixelsWidth(stack->pixels);
            if (bottom > (LONG)PreparedPixelsHeight(stack->pixels)) bottom = PreparedPixelsHeight(stack->pixels);
            if (x < right && y < bottom && !ReadOldPixels(stack->pixels, x, y, right-x, bottom-y,
                (struct PreparedPixel *)((UBYTE *)entry->content +
                    (ULONG)(y-next->bounds.MinY)*entry->stride) + x-next->bounds.MinX,
                entry->stride)) return batch_blame(batch, layer);
        }
    }
    if (root)
    {
        /* Unchanged uncovered pixels are not an exposure. Root VisibleRegion
         * is intentionally empty, so compute the OLD uncovered set explicitly
         * from the captured shapes and use it as the valid baseline. */
        ULONG i;
        struct Layer *front;
        if (!SetRegion(entry->old.shape, entry->valid))
            return batch_refuse(batch, LGR_NO_MEMORY, layer, 0, LGR_MEM_REGION, 0, 0);
        for (front = layer->front; front; front = front->front)
            if (IS_VISIBLE(front))
            {
                for (i = 0; i < batch->layer_count; ++i)
                    if (batch->layers[i].layer == front) break;
                if (i == batch->layer_count) return batch_refuse(batch, LGR_FRONT, layer, 0, 0, 0, 0);
                if (!ClearRegionRegion(batch->layers[i].old.visibleshape, entry->valid))
                    return batch_refuse(batch, LGR_NO_MEMORY, layer, 0, LGR_MEM_REGION, 0, 0);
            }
    }
    else if (IS_SUPERREFRESH(layer))
    {
        struct PreparedPixels *pixels = entry->super_pixels;
        ULONG x, y, width, height;
        if (!pixels) return batch_refuse(batch, LGR_SUPER_BITMAP, layer, 0, 0, 0, 0);
        if (super_viewport(entry, &x, &y, &width, &height))
        {
            struct Rectangle known;
            struct PreparedPixel *destination = (struct PreparedPixel *)
                ((UBYTE *)entry->content + y*entry->stride) + x;
            if (!ReadStagedPixels(pixels, (LONG)x+layer->Scroll_X,
                (LONG)y+layer->Scroll_Y, width, height, destination, entry->stride))
                return batch_blame(batch, layer);
            known.MinX = (LONG)next->bounds.MinX+x;
            known.MinY = (LONG)next->bounds.MinY+y;
            known.MaxX = known.MinX+width-1; known.MaxY = known.MinY+height-1;
            if (!OrRectRegion(entry->valid, &known))
                return batch_refuse(batch, LGR_NO_MEMORY, layer, 0, LGR_MEM_REGION, 0, 0);
        }
        /* Visible pixels belong to this live layer, including any portion
         * outside its backing. Re-overlay them after shared-backing staging:
         * another SUPER layer can synchronize a different view of that same
         * bitmap, but must not replace this layer's own visible image. */
        if (!read_clips(batch, entry, entry->old.regular, LayersBase) ||
            !read_clips(batch, entry, entry->old.clips, LayersBase)) return FALSE;
    }
    else if (!read_clips(batch, entry, entry->old.regular, LayersBase) ||
             !read_clips(batch, entry, entry->old.clips, LayersBase)) return FALSE;
    success = FALSE; /* a successful backing read is not a complete prepare */
    missing = NewRegion();
    if (!missing || !OrRectRegion(missing, &next->bounds)) goto region;
    if (root)
    {
        ULONG i;
        struct Layer *front;
        for (front = layer->front; front; front = front->front)
            if (IS_VISIBLE(front))
            {
                for (i = 0; i < batch->layer_count; ++i)
                    if (batch->layers[i].layer == front) break;
                if (i == batch->layer_count)
                { batch_refuse(batch, LGR_FRONT, layer, 0, 0, 0, 0); goto done; }
                if (!ClearRegionRegion(batch->layers[i].next.visibleshape, missing)) goto region;
            }
    }
    if (!ClearRegionRegion(entry->valid, missing)) goto region;
    if (IS_SIMPLEREFRESH(layer) && !root &&
        (!AndRegionRegion(next->visible, missing) ||
         !AndRegionRegion(next->visibleshape, missing))) goto region;
    _TranslateRect(&missing->bounds, -next->bounds.MinX, -next->bounds.MinY);
    if (!root && !OrRegionRegion(missing, next->damage)) goto region;
    if (root)
    {
        ClearRegion(next->damage);
        next->flags &= ~LAYERREFRESH;
    }
    if ((next->width != entry->old.width || next->height != entry->old.height) && !root)
    {
        /* Former border/gadget pixels can become client pixels on growth.
         * Damage the complete resized layer, rather than only the new strips. */
        struct Rectangle local = {0, 0, next->width - 1, next->height - 1};
        if (!OrRectRegion(next->damage, &local)) goto region;
    }
    if (next->damage->RegionRectangle) next->flags |= LAYERREFRESH;
    /* Keep exact newly missing rectangles for irreversible postcommit
     * backfill, before notification publication. No client hook runs here. */
    entry->backfill = missing; missing = NULL;
    success = TRUE;
    goto done;
region:
    batch_refuse(batch, LGR_NO_MEMORY, layer, 0, LGR_MEM_REGION, 0, 0);
done:
    if (missing) DisposeRegion(missing);
    return success;
}

static BOOL paint_content(struct LayerGeometryBatch *batch, struct PreparedLayer *entry)
{
    struct PreparedStack *stack = find_stack(batch, entry->layer->LayerInfo);
    /* An installed client clip limits future drawing, not the content moved
     * with the window. Publish the complete regular visible set. */
    struct ClipRect *clip = entry->next.regular ? entry->next.regular : entry->next.clips;
    if (!stack) return batch_refuse(batch, LGR_NOT_MEMBER, entry->layer, 0, 0, 0, 0);
    if (entry->layer == entry->layer->LayerInfo->check_lp)
    {
        struct RegionRectangle *rr;
        if (entry->layer->BackFill == LAYERS_NOBACKFILL) return TRUE;
        for (rr = entry->backfill->RegionRectangle; rr; rr = rr->Next)
        {
            LONG x = (LONG)rr->bounds.MinX + entry->backfill->bounds.MinX + entry->next.bounds.MinX;
            LONG y = (LONG)rr->bounds.MinY + entry->backfill->bounds.MinY + entry->next.bounds.MinY;
            LONG height = (LONG)rr->bounds.MaxY - rr->bounds.MinY + 1;
            ULONG width = (LONG)rr->bounds.MaxX - rr->bounds.MinX + 1;
            if (x < 0 || y < 0) return batch_refuse(batch, LGR_SCREEN_OFFSET, entry->layer, x, y, 0, 0);
            while (height--)
                if (!WritePreparedPixels(stack->pixels, x, y++, width, 1,
                    entry->content, entry->stride)) return batch_blame(batch, entry->layer);
        }
        return TRUE;
    }
    for (; clip; clip = clip->Next)
    {
        const struct PreparedPixel *source;
        if (clip->lobs) continue;
        source = (const struct PreparedPixel *)((const UBYTE *)entry->content +
            (ULONG)((LONG)clip->bounds.MinY - entry->next.bounds.MinY) * entry->stride) +
            ((LONG)clip->bounds.MinX - entry->next.bounds.MinX);
        if (clip->bounds.MinX < 0 || clip->bounds.MinY < 0)
            return batch_refuse(batch, LGR_SCREEN_OFFSET, entry->layer,
                clip->bounds.MinX, clip->bounds.MinY, 0, 0);
        if (!WritePreparedPixels(stack->pixels, clip->bounds.MinX, clip->bounds.MinY,
                (LONG)clip->bounds.MaxX - clip->bounds.MinX + 1,
                (LONG)clip->bounds.MaxY - clip->bounds.MinY + 1, source, entry->stride))
            return batch_blame(batch, entry->layer);
    }
    return TRUE;
}

static BOOL copy_old_pixels(struct LayerGeometryRefusal *refusal,
    struct PreparedPixels *destination, ULONG dx, ULONG dy,
    const struct PreparedPixels *source, ULONG sx, ULONG sy, ULONG width, ULONG height)
{
    struct PreparedPixel *row;
    ULONG y;
    BOOL success = FALSE;
    /* Same-format transfer preserves native padding/channel bits exactly. */
    if (CopyPreparedPixels(destination, dx, dy, source, sx, sy, width, height, FALSE)) return TRUE;
    /* Ordinary SUPER backing can use another supported packed pixel format.
     * Convert only during preparation, with the same channel precision as
     * that backing; the source and destination mappings remain unpublished. */
    row = allocate_array(width, sizeof(*row));
    if (!row)
        return LayerRefusal(refusal, LGR_NO_MEMORY, NULL, array_bytes(width, sizeof(*row)),
            LGR_MEM_ROW, 0, 0);
    for (y = 0; y < height; ++y)
        if (!ReadOldPixels(source, sx, sy+y, width, 1, row, width*sizeof(*row)) ||
            !WritePreparedPixels(destination, dx, dy+y, width, 1, row, width*sizeof(*row))) goto done;
    success = TRUE;
done:
    FreeVec(row);
    return success;
}

/* Sync the old visible pixels into detached super backing, without calling
 * SyncSBitMap (which would mutate live state). A shared SuperBitMap has one
 * mapping/staging owner; all its users see the same proposed backing. */
static BOOL prepare_super_pixels(struct LayerGeometryBatch *batch,
    struct LayersBase *LayersBase)
{
    ULONG i, j, list;
    (void)LayersBase;
    for (i = 0; i < batch->layer_count; ++i)
    {
        struct PreparedLayer *entry = &batch->layers[i];
        struct Layer *layer = entry->layer;
        struct PreparedStack *stack;
        struct ClipRect *lists[2];
        if (!IS_SUPERREFRESH(layer)) continue;
        stack = find_stack(batch, layer->LayerInfo);
        if (!layer->SuperBitMap) return batch_refuse(batch, LGR_SUPER_BITMAP, layer, 0, 0, 0, 0);
        if (!stack) return batch_refuse(batch, LGR_NOT_MEMBER, layer, 0, 0, 0, 0);
        /* Mapping the display again would acquire its cursor lease twice. */
        for (j = 0; j < batch->stack_count; ++j)
            if (layer->SuperBitMap == batch->stacks[j].bitmap)
                return batch_refuse(batch, LGR_SUPER_ALIAS, layer, j, 0, 0, 0);
        for (j = 0; j < i; ++j)
            if (batch->layers[j].super_pixels &&
                batch->layers[j].layer->SuperBitMap == layer->SuperBitMap)
            { entry->super_pixels = batch->layers[j].super_pixels; break; }
        if (!entry->super_pixels)
        {
            entry->super_pixels = PreparePixels(layer->SuperBitMap, TRUE,
                batch->refusal, LGR_PIXELS_SUPER);
            if (!entry->super_pixels) return batch_blame(batch, layer);
            entry->owns_super_pixels = TRUE;
        }
        lists[0] = entry->old.regular; lists[1] = entry->old.clips;
        for (list = 0; list < 2; ++list)
        {
            struct ClipRect *clip;
            for (clip = lists[list]; clip; clip = clip->Next)
            {
                LONG left, top, right, bottom, originx, originy, limit;
                if (clip->lobs) continue;
                left = clip->bounds.MinX; top = clip->bounds.MinY;
                right = (LONG)clip->bounds.MaxX+1; bottom = (LONG)clip->bounds.MaxY+1;
                originx = (LONG)entry->old.bounds.MinX-layer->Scroll_X;
                originy = (LONG)entry->old.bounds.MinY-layer->Scroll_Y;
                if (left < originx) left = originx;
                if (top < originy) top = originy;
                if (left < 0) left = 0;
                if (top < 0) top = 0;
                limit = originx+(LONG)PreparedPixelsWidth(entry->super_pixels);
                if (right > limit) right = limit;
                limit = originy+(LONG)PreparedPixelsHeight(entry->super_pixels);
                if (bottom > limit) bottom = limit;
                if (right > (LONG)PreparedPixelsWidth(stack->pixels)) right = PreparedPixelsWidth(stack->pixels);
                if (bottom > (LONG)PreparedPixelsHeight(stack->pixels)) bottom = PreparedPixelsHeight(stack->pixels);
                if (left >= right || top >= bottom) continue;
                if (!copy_old_pixels(batch->refusal, entry->super_pixels, left-originx, top-originy,
                    stack->pixels, left, top, right-left, bottom-top)) return batch_blame(batch, layer);
            }
        }
    }
    return TRUE;
}

static void release_batch(struct LayerGeometryBatch *batch, struct LayersBase *LayersBase)
{
    ULONG i;
    BOOL committed;
    if (!batch) return;
    committed = batch->state == BATCH_COMMITTED || batch->state == BATCH_FINISHED;
    for (i = 0; i < batch->layer_count; ++i)
    {
        ReleasePreparedLayerClips(&batch->layers[i], committed, LayersBase);
        if (batch->layers[i].owns_super_pixels) ReleasePreparedPixels(batch->layers[i].super_pixels);
        FreeVec(batch->layers[i].content);
        if (batch->layers[i].valid) DisposeRegion(batch->layers[i].valid);
        if (batch->layers[i].backfill) DisposeRegion(batch->layers[i].backfill);
    }
    ReleasePreparedLayerRegions(batch->layers, batch->layer_count, committed, LayersBase);
    for (i = batch->stack_count; i > 0; --i)
        if (batch->stacks[i - 1].pixels) ReleasePreparedPixels(batch->stacks[i - 1].pixels);
    while (batch->layer_locks)
        UnlockLayer(batch->layers[--batch->layer_locks].layer);
    for (i = batch->stack_count; i > 0; --i)
        if (batch->stacks[i - 1].info_locked) UnlockLayerInfo(batch->stacks[i - 1].info);
    FreeVec(batch->layers); FreeVec(batch->stacks); FreeVec(batch);
}

static struct LayerGeometryBatch *prepare_batch(struct Layer_Info *root,
    const struct LayerGeometryRequest *requests, ULONG count,
    struct LayerGeometryRefusal *refusal, struct LayersBase *LayersBase)
{
    struct LayerGeometryBatch *batch;
    ULONG i, j, layers = 0;
    if (refusal) LayerRefusal(refusal, LGR_NONE, NULL, 0, 0, 0, 0);
    if (!root || (count && !requests) || count == 0xffffffffUL)
    { LayerRefusal(refusal, LGR_ARGUMENTS, NULL, count, 0, 0, 0); return NULL; }
    /* An ordinary stack is its own composition root; only a private canvas
     * stack names another one. */
    if (GetLayerInfoCompositionRoot(root) != root)
    { LayerRefusal(refusal, LGR_ROOT_COMPOSED, NULL, 0, 0, 0, 0); return NULL; }
    batch = AllocVec(sizeof(*batch), MEMF_PUBLIC | MEMF_CLEAR);
    if (!batch)
    {
        LayerRefusal(refusal, LGR_NO_MEMORY, NULL, sizeof(*batch), LGR_MEM_BATCH, 0, 0);
        return NULL;
    }
    batch->refusal = refusal;
    batch->stacks = allocate_array(count + 1, sizeof(*batch->stacks));
    if (!batch->stacks)
    {
        LayerRefusal(refusal, LGR_NO_MEMORY, NULL, array_bytes(count + 1, sizeof(*batch->stacks)),
            LGR_MEM_STACKS, 0, 0);
        goto fail;
    }
    batch->stack_count = 1; batch->stacks[0].info = root;
    LockLayerInfo(root); batch->stacks[0].info_locked = TRUE;
    for (i = 0; i < count; ++i)
    {
        struct Layer_Info *info;
        if (!requests[i].layer || !(info = requests[i].layer->LayerInfo))
        { LayerRefusal(refusal, LGR_REQUEST_LAYER, requests[i].layer, i, 0, 0, 0); goto fail; }
        if (info != root && GetLayerInfoCompositionRoot(info) != root)
        { batch_refuse(batch, LGR_FOREIGN_STACK, requests[i].layer, i, 0, 0, 0); goto fail; }
        if (!find_stack(batch, info)) batch->stacks[batch->stack_count++].info = info;
    }
    /* Physical root first, private roots address-ordered, all infos before
     * any layer lock. No reverse acquisition from a private client layer. */
    for (i = 1; i < batch->stack_count; ++i)
        for (j = i + 1; j < batch->stack_count; ++j)
            if ((IPTR)batch->stacks[j].info < (IPTR)batch->stacks[i].info)
            { struct PreparedStack swap = batch->stacks[i]; batch->stacks[i] = batch->stacks[j]; batch->stacks[j] = swap; }
    for (i = 1; i < batch->stack_count; ++i)
    { LockLayerInfo(batch->stacks[i].info); batch->stacks[i].info_locked = TRUE; }
    for (i = 0; i < batch->stack_count; ++i)
    {
        struct Layer *layer;
        for (layer = batch->stacks[i].info->top_layer; layer; layer = layer->back)
            if (++layers == 0xffffffffUL)
            { LayerRefusal(refusal, LGR_LAYER_COUNT, NULL, layers, 0, 0, 0); goto fail; }
    }
    batch->layers = allocate_array(layers, sizeof(*batch->layers));
    if (!batch->layers)
    {
        LayerRefusal(refusal, LGR_NO_MEMORY, NULL, array_bytes(layers, sizeof(*batch->layers)),
            LGR_MEM_LAYERS, layers, 0);
        goto fail;
    }
    for (i = 0; i < batch->stack_count; ++i)
    {
        struct Layer *layer;
        for (layer = batch->stacks[i].info->top_layer; layer; layer = layer->back)
        {
            batch->layers[batch->layer_count++].layer = layer;
            LockLayer(0, layer); batch->layer_locks++;
        }
    }
    if (!PrepareLayerRegions(batch->layers, batch->layer_count, requests, count,
        refusal, LayersBase)) goto fail;
    for (i = 0; i < batch->stack_count; ++i)
    {
        struct Layer *layer = batch->stacks[i].info->check_lp;
        if (!layer || !layer->rp || !layer->rp->BitMap)
        { batch_refuse(batch, LGR_STACK_BITMAP, layer, i, 0, 0, 0); goto fail; }
        batch->stacks[i].bitmap = layer->rp->BitMap;
        batch->stacks[i].pixels = PreparePixels(layer->rp->BitMap, TRUE, refusal, LGR_PIXELS_STACK);
        if (!batch->stacks[i].pixels) { batch_blame(batch, layer); goto fail; }
    }
    if (!prepare_super_pixels(batch, LayersBase)) goto fail;
    for (i = 0; i < batch->layer_count; ++i)
    {
        struct PreparedLayer *entry = &batch->layers[i];
        struct PreparedStack *stack = find_stack(batch, entry->layer->LayerInfo);
        if (!stack) { batch_refuse(batch, LGR_NOT_MEMBER, entry->layer, 0, 0, 0, 0); goto fail; }
        if (!entry->layer->rp || entry->layer->rp->BitMap != stack->bitmap)
        { batch_refuse(batch, LGR_FOREIGN_BITMAP, entry->layer, entry->layer->rp != NULL, 0, 0, 0); goto fail; }
        if (!prepare_content(batch, entry, LayersBase)) goto fail;
        if (!PrepareLayerClips(entry, refusal, LayersBase) ||
            !FillPreparedLayerClips(entry, refusal, LayersBase))
        { place_refusal(refusal, root); goto fail; }
    }
    for (i = 0; i < batch->layer_count; ++i)
        if (!paint_content(batch, &batch->layers[i])) goto fail;
    return batch;
fail:
    place_refusal(refusal, root);
    release_batch(batch, LayersBase);
    return NULL;
}

AROS_LH4(struct LayerGeometryBatch *, PrepareLayerGeometryBatch,
    AROS_LHA(struct Layer_Info *, root, A0),
    AROS_LHA(const struct LayerGeometryRequest *, requests, A1),
    AROS_LHA(ULONG, count, D0),
    AROS_LHA(struct LayerGeometryRefusal *, refusal, A2),
    struct LayersBase *, LayersBase, 48, Layers)
{
    AROS_LIBFUNC_INIT
    return prepare_batch(root, requests, count, refusal, LayersBase);
    AROS_LIBFUNC_EXIT
}

AROS_LH8(BOOL, ReadLayerGeometryPixels,
    AROS_LHA(struct LayerGeometryBatch *, batch, A0),
    AROS_LHA(struct Layer_Info *, info, A1),
    AROS_LHA(ULONG, x, D0), AROS_LHA(ULONG, y, D1),
    AROS_LHA(ULONG, width, D2), AROS_LHA(ULONG, height, D3),
    AROS_LHA(struct PreparedPixel *, pixels, A2), AROS_LHA(ULONG, stride, D4),
    struct LayersBase *, LayersBase, 49, Layers)
{
    AROS_LIBFUNC_INIT
    struct PreparedStack *stack = batch ? find_stack(batch, info) : NULL;
    (void)LayersBase;
    if (!batch) return FALSE;
    if (!stack) return batch_refuse(batch, LGR_NOT_MEMBER, NULL, 0, 0, 0, 0);
    return ReadStagedPixels(stack->pixels, x, y, width, height, pixels, stride) ||
        batch_blame(batch, info ? info->check_lp : NULL);
    AROS_LIBFUNC_EXIT
}

/* Read a detached caller-owned staging bitmap without losing native channel
 * precision. A scene or SuperBitMap alias would reacquire an existing lease;
 * reject it before mapping. This PREPARE-only read never changes batch pixels. */
static BOOL read_bitmap_pixels(struct LayerGeometryBatch *batch,
    struct BitMap *bitmap, ULONG x, ULONG y, ULONG width, ULONG height,
    struct PreparedPixel *pixels, ULONG stride, struct LayersBase *LayersBase)
{
    struct PreparedPixels *source;
    ULONG i;
    BOOL result;
    (void)LayersBase;
    if (!batch) return FALSE;
    if (batch->state != BATCH_PREPARING) return batch_refuse(batch, LGR_STATE, NULL, batch->state, 0, 0, 0);
    if (!bitmap || !pixels || !width || !height)
        return batch_refuse(batch, LGR_ARGUMENTS, NULL, width, height, 0, 0);
    for (i = 0; i < batch->stack_count; ++i)
        if (batch->stacks[i].bitmap == bitmap) return batch_refuse(batch, LGR_SOURCE_ALIAS, NULL, i, 0, 0, 0);
    for (i = 0; i < batch->layer_count; ++i)
        if (batch->layers[i].layer->SuperBitMap == bitmap)
            return batch_refuse(batch, LGR_SOURCE_ALIAS, batch->layers[i].layer, -1, 0, 0, 0);
    source = PreparePixels(bitmap, FALSE, batch->refusal, LGR_PIXELS_SOURCE);
    if (!source) return FALSE;
    result = ReadOldPixels(source, x, y, width, height, pixels, stride);
    ReleasePreparedPixels(source);
    return result;
}

AROS_LH5(BOOL, WriteLayerGeometryPixels,
    AROS_LHA(struct LayerGeometryBatch *, batch, A0),
    AROS_LHA(struct Layer *, layer, A1),
    AROS_LHA(const struct PreparedPixel *, pixels, A2), AROS_LHA(ULONG, stride, D0),
    AROS_LHA(ULONG, height, D1),
    struct LayersBase *, LayersBase, 50, Layers)
{
    AROS_LIBFUNC_INIT
    ULONG i, y;
    if (!batch) return FALSE;
    if (batch->state != BATCH_PREPARING) return batch_refuse(batch, LGR_STATE, layer, batch->state, 0, 0, 0);
    if (!pixels) return batch_refuse(batch, LGR_ARGUMENTS, layer, 0, 0, 0, 0);
    for (i = 0; i < batch->layer_count; ++i)
        if (batch->layers[i].layer == layer)
        {
            struct PreparedLayer *entry = &batch->layers[i];
            if (layer == layer->LayerInfo->check_lp)
                return batch_refuse(batch, LGR_ROOT_CANVAS, layer, 0, 0, 0, 0);
            if (stride < entry->stride || height < (ULONG)entry->next.height ||
                height > 0xffffffffUL / stride)
                return batch_refuse(batch, LGR_SOURCE_SIZE, layer, stride, height,
                    entry->stride, entry->next.height);
            for (y = 0; y < (ULONG)entry->next.height; ++y)
                memcpy((UBYTE *)entry->content + y * entry->stride,
                    (const UBYTE *)pixels + y * stride, entry->stride);
            if (entry->super_pixels)
            {
                ULONG x, y, width, height;
                if (super_viewport(entry, &x, &y, &width, &height) &&
                    !WritePreparedPixels(entry->super_pixels, (LONG)x+layer->Scroll_X,
                        (LONG)y+layer->Scroll_Y, width, height,
                        (const struct PreparedPixel *)((const UBYTE *)entry->content+y*entry->stride)+x,
                        entry->stride)) return batch_blame(batch, layer);
            }
            if (entry->backfill) ClearRegion(entry->backfill);
            ClearRegion(entry->next.damage);
            if (!SetRegion(entry->old.damage, entry->next.damage))
                return batch_refuse(batch, LGR_NO_MEMORY, layer, 0, LGR_MEM_REGION, 0, 0);
            {
                struct Rectangle local = {0, 0, entry->next.width-1, entry->next.height-1};
                AndRectRegion(entry->next.damage, &local);
            }
            entry->next.flags = entry->old.flags;
            if (!entry->next.damage->RegionRectangle) entry->next.flags &= ~LAYERREFRESH;
            if (!FillPreparedLayerClips(entry, batch->refusal, LayersBase))
                return batch_blame(batch, layer);
            return paint_content(batch, entry);
        }
    return batch_refuse(batch, LGR_NOT_MEMBER, layer, 0, 0, 0, 0);
    AROS_LIBFUNC_EXIT
}

/* Native OS-owned client painting updates a bounded part of its detached
 * layer canvas. Preserve the border/occluded backing and damage outside that
 * rectangle. SuperBitMaps require their own scroll/extent contract. */
AROS_LH4(BOOL, WriteLayerGeometryBitmap,
    AROS_LHA(struct LayerGeometryBatch *, batch, A0),
    AROS_LHA(struct Layer *, layer, A1),
    AROS_LHA(struct BitMap *, bitmap, A2),
    AROS_LHA(const struct Rectangle *, bounds, A3),
    struct LayersBase *, LayersBase, 58, Layers)
{
    AROS_LIBFUNC_INIT
    ULONG i;
    if (!batch) return FALSE;
    if (batch->state != BATCH_PREPARING) return batch_refuse(batch, LGR_STATE, layer, batch->state, 0, 0, 0);
    if (!layer || !bounds || !bitmap) return batch_refuse(batch, LGR_ARGUMENTS, layer, 0, 0, 0, 0);
    if (bounds->MinX < 0 || bounds->MinY < 0 ||
        bounds->MaxX < bounds->MinX || bounds->MaxY < bounds->MinY)
        return batch_refuse(batch, LGR_RECTANGLE, layer, bounds->MinX, bounds->MinY,
            bounds->MaxX, bounds->MaxY);
    for (i = 0; i < batch->layer_count; ++i)
        if (batch->layers[i].layer == layer)
        {
            struct PreparedLayer *entry = &batch->layers[i];
            ULONG x = bounds->MinX, y = bounds->MinY;
            ULONG width = (LONG)bounds->MaxX - bounds->MinX + 1;
            ULONG height = (LONG)bounds->MaxY - bounds->MinY + 1;
            struct PreparedPixel *destination;
            struct Rectangle painted = *bounds;
            if (layer == layer->LayerInfo->check_lp)
                return batch_refuse(batch, LGR_ROOT_CANVAS, layer, 0, 0, 0, 0);
            if (entry->super_pixels) return batch_refuse(batch, LGR_SUPER_TARGET, layer, 0, 0, 0, 0);
            if (bounds->MaxX >= entry->next.width || bounds->MaxY >= entry->next.height)
                return batch_refuse(batch, LGR_RECTANGLE, layer, bounds->MinX, bounds->MinY,
                    bounds->MaxX, bounds->MaxY);
            destination = (struct PreparedPixel *)((UBYTE *)entry->content +
                y * entry->stride) + x;
            if (!read_bitmap_pixels(batch, bitmap, x, y, width, height,
                destination, entry->stride, LayersBase)) return batch_blame(batch, layer);
            if ((entry->backfill && !ClearRectRegion(entry->backfill, &painted)) ||
                !ClearRectRegion(entry->next.damage, &painted))
                return batch_refuse(batch, LGR_NO_MEMORY, layer, 0, LGR_MEM_REGION, 0, 0);
            if (!entry->next.damage->RegionRectangle) entry->next.flags &= ~LAYERREFRESH;
            if (!FillPreparedLayerClips(entry, batch->refusal, LayersBase))
                return batch_blame(batch, layer);
            return paint_content(batch, entry);
        }
    return batch_refuse(batch, LGR_NOT_MEMBER, layer, 0, 0, 0, 0);
    AROS_LIBFUNC_EXIT
}

/* Register only one exact bitmap owner before sealing. The returned map is
 * borrowed under the batch lease, and becomes invalid at Finish/Release.
 * The caller binds its detached cursor state immediately and retains that
 * state through lease release. No live cursor is replaced here. */
AROS_LH3(struct HIDD_PixelMap *, PrepareLayerGeometryCursor,
    AROS_LHA(struct LayerGeometryBatch *, batch, A0),
    AROS_LHA(struct BitMap *, bitmap, A1),
    AROS_LHA(struct HIDD_CursorState *, proposed, A2),
    struct LayersBase *, LayersBase, 57, Layers)
{
    AROS_LIBFUNC_INIT
    struct PreparedPixels *owner = NULL;
    struct HIDD_PixelMap *map;
    ULONG i;
    (void)LayersBase;
    if (!batch) return NULL;
    if (!batch->stack_count) return NULL;
    if (batch->state != BATCH_PREPARING)
    { batch_refuse(batch, LGR_STATE, NULL, batch->state, 0, 0, 0); return NULL; }
    if (!bitmap || !proposed) { batch_refuse(batch, LGR_ARGUMENTS, NULL, 0, 0, 0, 0); return NULL; }
    if (batch->stacks[0].bitmap != bitmap)
    { batch_refuse(batch, LGR_CURSOR_OWNER, NULL, 0, 0, 0, 0); return NULL; }
    for (i = 0; i < batch->stack_count; ++i)
        if (batch->stacks[i].bitmap == bitmap)
        {
            /* [0] the second stack sharing the display bitmap */
            if (owner) { batch_refuse(batch, LGR_CURSOR_OWNER, NULL, i, 0, 0, 0); return NULL; }
            owner = batch->stacks[i].pixels;
            if (!owner) { batch_refuse(batch, LGR_CURSOR_LEASE, NULL, 0, 0, 0, 0); return NULL; }
        }
    map = PreparePixelsCursor(owner, proposed);
    if (!map) batch_refuse(batch, LGR_CURSOR_LEASE, NULL, 1, 0, 0, 0);
    return map;
    AROS_LIBFUNC_EXIT
}

AROS_LH1(BOOL, SealLayerGeometryBatch,
    AROS_LHA(struct LayerGeometryBatch *, batch, A0),
    struct LayersBase *, LayersBase, 51, Layers)
{
    AROS_LIBFUNC_INIT
    ULONG i;
    (void)LayersBase;
    if (!batch) return FALSE;
    if (batch->state != BATCH_PREPARING) return batch_refuse(batch, LGR_STATE, NULL, batch->state, 0, 0, 0);
    for (i = 0; i < batch->stack_count; ++i)
        if (!SealPreparedPixels(batch->stacks[i].pixels))
            return batch_blame(batch, batch->stacks[i].info->check_lp);
    for (i = 0; i < batch->layer_count; ++i)
        if (batch->layers[i].owns_super_pixels &&
            !SealPreparedPixels(batch->layers[i].super_pixels))
            return batch_blame(batch, batch->layers[i].layer);
    batch->state = BATCH_SEALED;
    return TRUE;
    AROS_LIBFUNC_EXIT
}

AROS_LH1(void, CommitLayerGeometryBatch,
    AROS_LHA(struct LayerGeometryBatch *, batch, A0),
    struct LayersBase *, LayersBase, 52, Layers)
{
    AROS_LIBFUNC_INIT
    ULONG i;
    (void)LayersBase;
    if (!batch || batch->state != BATCH_SEALED) return;
    batch->state = BATCH_INSTALLING;
    InstallPreparedLayerFields(batch->layers, batch->layer_count, FALSE);
    for (i = 0; i < batch->layer_count; ++i)
        if (batch->layers[i].owns_super_pixels) CommitPreparedPixels(batch->layers[i].super_pixels);
    /* Private canvases first; the physical root is published last. */
    for (i = batch->stack_count; i > 0; --i) CommitPreparedPixels(batch->stacks[i - 1].pixels);
    batch->state = BATCH_COMMITTED;
    AROS_LIBFUNC_EXIT
}

AROS_LH1(void, RollbackLayerGeometryBatch,
    AROS_LHA(struct LayerGeometryBatch *, batch, A0),
    struct LayersBase *, LayersBase, 53, Layers)
{
    AROS_LIBFUNC_INIT
    ULONG i;
    (void)LayersBase;
    if (!batch || (batch->state != BATCH_COMMITTED && batch->state != BATCH_INSTALLING)) return;
    InstallPreparedLayerFields(batch->layers, batch->layer_count, TRUE);
    for (i = batch->stack_count; i > 0; --i) RestorePreparedPixels(batch->stacks[i - 1].pixels);
    for (i = 0; i < batch->layer_count; ++i)
        if (batch->layers[i].owns_super_pixels) RestorePreparedPixels(batch->layers[i].super_pixels);
    batch->state = BATCH_ROLLED_BACK;
    AROS_LIBFUNC_EXIT
}

/* Postcommit drawing is deliberately separate from commit/rollback. It may
 * call arbitrary client backfill hooks only after all metadata, rasters and
 * rasters are committed, before notifications are published. Release direct
 * mappings first so ordinary
 * driver drawing can acquire cursor/framebuffer locks. Layer locks still pin
 * the installed clip/backing generations until batch release. */
AROS_LH1(void, FinishLayerGeometryBatch,
    AROS_LHA(struct LayerGeometryBatch *, batch, A0),
    struct LayersBase *, LayersBase, 56, Layers)
{
    AROS_LIBFUNC_INIT
    ULONG i;
    if (!batch || batch->state != BATCH_COMMITTED) return;
    batch->state = BATCH_FINISHED; /* callbacks cannot reopen rollback */
    for (i = batch->stack_count; i > 0; --i)
    {
        ReleasePreparedPixels(batch->stacks[i - 1].pixels);
        batch->stacks[i - 1].pixels = NULL;
    }
    for (i = 0; i < batch->layer_count; ++i)
        if (batch->layers[i].owns_super_pixels)
        {
            ReleasePreparedPixels(batch->layers[i].super_pixels);
            batch->layers[i].super_pixels = NULL;
            batch->layers[i].owns_super_pixels = FALSE;
        }
    for (i = 0; i < batch->layer_count; ++i)
    {
        struct PreparedLayer *entry = &batch->layers[i];
        struct Layer *layer = entry->layer;
        struct Region *area = entry->backfill;
        struct RegionRectangle *rr;
        if (!area || IS_SUPERREFRESH(layer) || layer->BackFill == LAYERS_BACKFILL ||
            layer->BackFill == LAYERS_NOBACKFILL) continue;
        for (rr = area->RegionRectangle; rr; rr = rr->Next)
        {
            struct Rectangle wanted = rr->bounds;
            struct ClipRect *clip;
            struct RastPort port = *layer->rp;
            _TranslateRect(&wanted, area->bounds.MinX, area->bounds.MinY);
            if (layer == layer->LayerInfo->check_lp)
            {
                /* Root VisibleRegion is intentionally empty; the prepared
                 * uncovered set is the authoritative screen-relative clip. */
                _TranslateRect(&wanted, layer->bounds.MinX, layer->bounds.MinY);
                port.Layer = NULL;
                _CallLayerHook(layer->BackFill, &port, layer, &wanted,
                    wanted.MinX, wanted.MinY, LayersBase);
                continue;
            }
            _TranslateRect(&wanted, layer->bounds.MinX, layer->bounds.MinY);
            for (clip = entry->next.regular ? entry->next.regular : entry->next.clips;
                clip; clip = clip->Next)
            {
                struct Rectangle part;
                WORD offsetx, offsety;
                if ((clip->lobs && IS_SIMPLEREFRESH(layer)) ||
                    !rect_intersection(&wanted, &clip->bounds, &part)) continue;
                offsetx = part.MinX - layer->bounds.MinX + layer->Scroll_X;
                offsety = part.MinY - layer->bounds.MinY + layer->Scroll_Y;
                if (clip->lobs)
                {
                    _TranslateRect(&part, -clip->bounds.MinX + CR_BMOFFX(clip),
                        -clip->bounds.MinY + CR_BMOFFY(clip));
                    port.BitMap = clip->BitMap;
                }
                else port.BitMap = layer->rp->BitMap;
                port.Layer = NULL;
                _CallLayerHook(layer->BackFill, &port, layer, &part, offsetx, offsety, LayersBase);
            }
        }
    }
    AROS_LIBFUNC_EXIT
}

AROS_LH1(void, ReleaseLayerGeometryBatch,
    AROS_LHA(struct LayerGeometryBatch *, batch, A0),
    struct LayersBase *, LayersBase, 54, Layers)
{
    AROS_LIBFUNC_INIT
    release_batch(batch, LayersBase);
    AROS_LIBFUNC_EXIT
}

AROS_LH2(BOOL, LayerGeometryNeedsRefresh,
    AROS_LHA(struct LayerGeometryBatch *, batch, A0),
    AROS_LHA(struct Layer *, layer, A1),
    struct LayersBase *, LayersBase, 55, Layers)
{
    AROS_LIBFUNC_INIT
    ULONG i;
    (void)LayersBase;
    if (!batch || !layer) return FALSE;
    for (i = 0; i < batch->layer_count; ++i)
        if (batch->layers[i].layer == layer)
            return (batch->layers[i].next.flags & LAYERREFRESH) != 0 &&
                batch->layers[i].next.damage->RegionRectangle != NULL;
    return FALSE;
    AROS_LIBFUNC_EXIT
}
