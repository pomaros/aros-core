/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License, version 1.1.

    Legacy windows draw into an independent logical screen-sized canvas.
    Public Layer bounds, backfill offsets and bitmap coordinates retain their
    logical screen meaning. Only the screen's presentation layer is physical.
    The reference canvas costs width * height * native pixel depth plus driver
    alignment per window (about 16.4 MB for 2560x1600x32). Two preallocated
    native-pixel scanlines cost another width * 8 bytes; no frame-sized staging
    allocation or allocation in the damage callback/presentation loop is used.
*/
#include "intuition_intern.h"
#include "windowcomposition.h"
#include "windowfit.h"
#include <graphics/layertransaction.h>
#include "../graphics/scalecompose.h"

#include <aros/asmcall.h>
#include <exec/memory.h>
#include <graphics/layers.h>
#include <graphics/layersext.h>
#include <hidd/gfx.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/oop.h>

struct WindowComposition
{
    struct Window *window;
    struct IntuitionBase *base;
    struct BitMap *bitmap;
    struct Layer_Info *logical;
    struct Layer *proxy;
    OOP_Object *gc;
    struct Hook update;
    struct SignalSemaphore damageLock;
    struct ScaleComposeRect damage;
    struct ScaleComposeTransform transform;
    ULONG width, height;
    LONG logicalLeft, logicalTop, logicalWidth, logicalHeight;
    struct Region *projectedShape, *logicalShapeSnapshot;
    LONG shapeLeft, shapeTop;
    BOOL scalingFailure;
    ULONG *sourceRow, *destRow;
    BOOL dirty, stopping;
};

struct WindowCompositionValues
{
    struct ScaleComposeRect damage;
    struct ScaleComposeTransform transform;
    LONG logicalLeft, logicalTop, logicalWidth, logicalHeight;
    struct Region *projectedShape, *snapshot;
    LONG shapeLeft, shapeTop;
    BOOL failure, dirty;
    WORD mouseX, mouseY, gzzMouseX, gzzMouseY;
};
struct WindowCompositionScale
{
    struct WindowComposition *state;
    struct WindowCompositionValues old, next;
    BOOL locked, installed;
    ULONG physicalWidth, physicalHeight;
};

struct WindowCompositionScale *PrepareWindowCompositionScale(struct Window *window,
    ULONG scale, const struct WindowFitGeometry *geometry, struct Region *shape,
    struct LayerGeometryRequest *request, struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct WindowCompositionScale *prepared;
    struct WindowComposition *state = IW(window)->Composition;
    struct RegionRectangle *rr;
    struct ScaleComposeTransform transform = {0, 0, ScaleComposeFactor(scale)};
    if (!state || !state->proxy || !geometry || !request) return NULL;
    prepared = AllocVec(sizeof(*prepared), MEMF_PUBLIC | MEMF_CLEAR);
    if (!prepared) return NULL;
    prepared->state = state;
    prepared->physicalWidth = geometry->physical_width;
    prepared->physicalHeight = geometry->physical_height;
    prepared->next.transform.x = geometry->physical_left;
    prepared->next.transform.y = geometry->physical_top;
    prepared->next.transform.factor = transform.factor;
    prepared->next.logicalLeft = geometry->left; prepared->next.logicalTop = geometry->top;
    prepared->next.logicalWidth = geometry->width; prepared->next.logicalHeight = geometry->height;
    prepared->next.damage.left = prepared->next.damage.top = 0;
    prepared->next.damage.right = state->width; prepared->next.damage.bottom = state->height;
    prepared->next.dirty = TRUE;
    if (shape)
    {
        prepared->next.projectedShape = NewRegion();
        prepared->next.snapshot = NewRegion();
        if (!prepared->next.projectedShape || !prepared->next.snapshot ||
            !SetRegion(shape, prepared->next.snapshot)) goto fail;
        for (rr = shape->RegionRectangle; rr; rr = rr->Next)
        {
            struct ScaleComposeRect logical, physical;
            struct Rectangle rectangle;
            logical.left = (LONG)shape->bounds.MinX + rr->bounds.MinX;
            logical.top = (LONG)shape->bounds.MinY + rr->bounds.MinY;
            logical.right = (LONG)shape->bounds.MinX + rr->bounds.MaxX + 1;
            logical.bottom = (LONG)shape->bounds.MinY + rr->bounds.MaxY + 1;
            if (logical.left < 0) logical.left = 0;
            if (logical.top < 0) logical.top = 0;
            if (logical.right > geometry->width) logical.right = geometry->width;
            if (logical.bottom > geometry->height) logical.bottom = geometry->height;
            if (logical.left >= logical.right || logical.top >= logical.bottom) continue;
            if (!ScaleComposeDamage(&transform, &logical, &physical) ||
                physical.right > 32768 || physical.bottom > 32768) goto fail;
            rectangle.MinX = physical.left; rectangle.MinY = physical.top;
            rectangle.MaxX = physical.right - 1; rectangle.MaxY = physical.bottom - 1;
            if (!OrRectRegion(prepared->next.projectedShape, &rectangle)) goto fail;
        }
    }
    request->layer = state->proxy;
    request->bounds.MinX = geometry->physical_left;
    request->bounds.MinY = geometry->physical_top;
    request->bounds.MaxX = geometry->physical_left + geometry->physical_width - 1;
    request->bounds.MaxY = geometry->physical_top + geometry->physical_height - 1;
    request->flags = LGT_REPLACE_SHAPE | LGT_RESOLVED_SHAPE;
    request->shape = prepared->next.projectedShape;
    return prepared;
fail:
    if (prepared->next.projectedShape) DisposeRegion(prepared->next.projectedShape);
    if (prepared->next.snapshot) DisposeRegion(prepared->next.snapshot);
    FreeVec(prepared);
    return NULL;
}

BOOL PrepareWindowCompositionPixels(struct WindowCompositionScale *prepared,
    struct LayerGeometryBatch *batch, struct IntuitionBase *IntuitionBase)
{
    struct LayersBase *LayersBase = GetPrivIBase(IntuitionBase)->LayersBase;
    struct WindowComposition *state = prepared->state;
    struct WindowCompositionValues *next = &prepared->next;
    struct PreparedPixel *row = NULL, *output = NULL;
    ULONG factor = next->transform.factor;
    ULONG width, height, stride, y, x;
    BOOL success = FALSE;
    if (next->logicalWidth <= 0 || next->logicalHeight <= 0) return FALSE;
    width = prepared->physicalWidth;
    height = prepared->physicalHeight;
    if (!width || !height || width > 0xffffffffUL / sizeof(*output)) return FALSE;
    stride = width * sizeof(*output);
    if (height > 0xffffffffUL / stride) return FALSE;
    row = AllocVec((ULONG)next->logicalWidth * sizeof(*row), MEMF_PUBLIC | MEMF_CLEAR);
    output = AllocVec(height * stride, MEMF_PUBLIC | MEMF_CLEAR);
    if (!row || !output) goto done;
    for (y = 0; y < height; ++y)
    {
        LONG source_y = next->logicalTop + (LONG)(y / factor);
        LONG start = next->logicalLeft, end = start + next->logicalWidth;
        memset(row, 0, (ULONG)next->logicalWidth * sizeof(*row));
        if (start < 0) start = 0;
        if (end > (LONG)state->width) end = state->width;
        if (y/factor < (ULONG)next->logicalHeight && source_y >= 0 && source_y < (LONG)state->height && start < end &&
            !ReadLayerGeometryPixels(batch, state->logical, start, source_y,
                end - start, 1, row + start - next->logicalLeft,
                (ULONG)next->logicalWidth * sizeof(*row))) goto done;
        for (x = 0; x < width && x/factor < (ULONG)next->logicalWidth; ++x)
            output[y * width + x] = row[x / factor];
    }
    success = WriteLayerGeometryPixels(batch, state->proxy, output, stride, height);
done:
    FreeVec(row); FreeVec(output);
    return success;
}

static void capture_composition(struct WindowComposition *state,
    struct WindowCompositionValues *values)
{
    values->damage = state->damage; values->transform = state->transform;
    values->logicalLeft = state->logicalLeft; values->logicalTop = state->logicalTop;
    values->logicalWidth = state->logicalWidth; values->logicalHeight = state->logicalHeight;
    values->projectedShape = state->projectedShape; values->snapshot = state->logicalShapeSnapshot;
    values->shapeLeft = state->shapeLeft; values->shapeTop = state->shapeTop;
    values->failure = state->scalingFailure; values->dirty = state->dirty;
    values->mouseX = state->window->MouseX; values->mouseY = state->window->MouseY;
    values->gzzMouseX = state->window->GZZMouseX; values->gzzMouseY = state->window->GZZMouseY;
}

void SealWindowCompositionScale(struct WindowCompositionScale *prepared)
{
    if (!prepared || prepared->locked) return;
    ObtainSemaphore(&prepared->state->damageLock);
    prepared->locked = TRUE;
    capture_composition(prepared->state, &prepared->old);
    {
        struct Window *window = prepared->state->window;
        LONG x, y;
        ScaleComposeInput(&prepared->next.transform,
            window->WScreen->MouseX, window->WScreen->MouseY, &x, &y);
        prepared->next.mouseX = x; prepared->next.mouseY = y;
        prepared->next.gzzMouseX = x - window->BorderLeft;
        prepared->next.gzzMouseY = y - window->BorderTop;
    }
}

void InstallWindowCompositionScale(struct WindowCompositionScale *prepared, BOOL rollback)
{
    struct WindowComposition *state;
    const struct WindowCompositionValues *values;
    if (!prepared || !prepared->locked) return;
    state = prepared->state;
    values = rollback ? &prepared->old : &prepared->next;
    state->damage = values->damage; state->transform = values->transform;
    state->logicalLeft = values->logicalLeft; state->logicalTop = values->logicalTop;
    state->logicalWidth = values->logicalWidth; state->logicalHeight = values->logicalHeight;
    state->projectedShape = values->projectedShape; state->logicalShapeSnapshot = values->snapshot;
    state->shapeLeft = values->shapeLeft; state->shapeTop = values->shapeTop;
    state->scalingFailure = values->failure; state->dirty = values->dirty;
    state->window->MouseX = values->mouseX; state->window->MouseY = values->mouseY;
    state->window->GZZMouseX = values->gzzMouseX; state->window->GZZMouseY = values->gzzMouseY;
    prepared->installed = !rollback;
}

void ReleaseWindowCompositionScale(struct WindowCompositionScale *prepared,
    struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct WindowCompositionValues *loser;
    if (!prepared) return;
    loser = prepared->installed ? &prepared->old : &prepared->next;
    if (prepared->locked) ReleaseSemaphore(&prepared->state->damageLock);
    if (loser->projectedShape) DisposeRegion(loser->projectedShape);
    if (loser->snapshot) DisposeRegion(loser->snapshot);
    FreeVec(prepared);
}

static void mark_damage(struct WindowComposition *state, LONG left, LONG top,
                        LONG right, LONG bottom)
{
    /* This semaphore protects only the bounded damage union. Its holder never
     * takes screen/logical locks or waits for a presenter. Drawing may already
     * hold a logical layer lock when the callback runs. */
    ObtainSemaphore(&state->damageLock);
    if (!state->stopping && left < right && top < bottom)
    {
        if (!state->dirty)
        {
            state->damage.left = left; state->damage.top = top;
            state->damage.right = right; state->damage.bottom = bottom;
        }
        else
        {
            if (left < state->damage.left) state->damage.left = left;
            if (top < state->damage.top) state->damage.top = top;
            if (right > state->damage.right) state->damage.right = right;
            if (bottom > state->damage.bottom) state->damage.bottom = bottom;
        }
        state->dirty = TRUE;
    }
    ReleaseSemaphore(&state->damageLock);
}

AROS_UFH3(IPTR, composition_update,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(struct BitMap *, bitmap, A2),
    AROS_UFHA(struct BitMapUpdateMessage *, message, A1))
{
    AROS_USERFUNC_INIT
    struct WindowComposition *state = hook->h_Data;
    (void)bitmap;
    /* message lives on the graphics caller's stack; copy its coordinates now.
     * No presentation, allocation or screen/layer lock is allowed here. */
    mark_damage(state, message->x, message->y,
        (LONG)message->x + message->width,
        (LONG)message->y + message->height);
    return 0;
    AROS_USERFUNC_EXIT
}

static struct Layer *physical_parent(struct Window *parent, struct Screen *screen)
{
    struct Layer *layer;
    if (!parent || !screen || parent->WScreen != screen) return NULL;
    layer = WindowCompositionLayer(parent);
    if (!layer || layer->LayerInfo != &screen->LayerInfo) return NULL;
    return layer;
}

UWORD ScreenLegacyScaleFactorForScale(struct Screen *screen, ULONG scale)
{
    return !screen || !screen->RastPort.BitMap || !IS_HIDD_BM(screen->RastPort.BitMap)
        ? 1 : ScaleComposeFactor(scale);
}

UWORD ScreenLegacyScaleFactor(struct Screen *screen)
{
    return ScreenLegacyScaleFactorForScale(screen, screen ? IS(screen)->Scale : 100);
}

static UWORD child_factor(struct Screen *screen, BOOL scaleaware)
{
    return scaleaware ? 1 : ScreenLegacyScaleFactor(screen);
}

BOOL WindowCompositionChildPosition(struct Window *parent, struct Screen *screen,
    BOOL scaleaware, LONG relX, LONG relY, LONG *left, LONG *top)
{
    struct Layer *layer = physical_parent(parent, screen);
    UWORD factor;
    QUAD x, y;
    if (!layer || !left || !top) return FALSE;
    factor = child_factor(screen, scaleaware);
    x = ScaleComposeFloor(layer->bounds.MinX, factor) + (QUAD)relX;
    y = ScaleComposeFloor(layer->bounds.MinY, factor) + (QUAD)relY;
    if (!ScaleComposeLong(x) || !ScaleComposeLong(y)) return FALSE;
    *left = x; *top = y;
    return TRUE;
}

BOOL WindowCompositionChildExtent(struct Window *parent, struct Screen *screen,
    BOOL scaleaware, LONG *width, LONG *height)
{
    struct Layer *layer = physical_parent(parent, screen);
    UWORD factor;
    if (!layer || !width || !height) return FALSE;
    factor = child_factor(screen, scaleaware);
    {
        LONG w = layer->Width / factor, h = layer->Height / factor;
        if (w <= 0 || h <= 0) return FALSE;
        *width = w; *height = h;
    }
    return TRUE;
}

static BOOL physical_geometry(struct Window *window, UWORD factor,
                              struct ScaleComposeRect *bounds)
{
    struct ScaleComposeTransform transform = {0, 0, factor};
    struct ScaleComposeRect logical;
    if (window->parent)
    {
        struct Layer *parent = physical_parent(window->parent, window->WScreen);
        if (!parent) return FALSE;
        /* Parent origin is already physical. Only the child's relative offsets
         * and dimensions use the child's integer factor. Keep the exact parent
         * pixel origin, including a remainder not divisible by that factor. */
        transform.x = parent->bounds.MinX; transform.y = parent->bounds.MinY;
        logical.left = window->RelLeftEdge; logical.top = window->RelTopEdge;
    }
    else
    {
        logical.left = window->LeftEdge; logical.top = window->TopEdge;
    }
    logical.right = logical.left + window->Width;
    logical.bottom = logical.top + window->Height;
    if (!ScaleComposeDamage(&transform, &logical, bounds)) return FALSE;
    /* layers.library stores inclusive bounds in signed WORDs. */
    return bounds->left >= -32768 && bounds->top >= -32768 &&
           bounds->right <= 32768 && bounds->bottom <= 32768 &&
           bounds->right - bounds->left <= 32767 &&
           bounds->bottom - bounds->top <= 32767;
}


/* A custom shape is borrowed by the logical layer, so project a separate owned
 * region. Bounds stay logical; the proxy shape is window-relative physical.
 * A GZZ border layer defines the outer window silhouette. */
static BOOL project_shape(struct Window *window, UWORD factor,
                          struct Region **result, struct Region **snapshot, struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct LayersBase *LayersBase = GetPrivIBase(IntuitionBase)->LayersBase;
    struct WindowComposition *state = IW(window)->Composition;
    struct Layer *source = BLAYER(window) ? BLAYER(window) : WLAYER(window);
    struct RegionRectangle *rr;
    struct ScaleComposeTransform transform = {0, 0, factor};
    *result = NULL; *snapshot = NULL;
    if (!source) return FALSE;
    LockLayers(state->logical);
    if (!source->shaperegion) { UnlockLayers(state->logical); return TRUE; }
    *result = NewRegion();
    *snapshot = NewRegion();
    if (!*result || !*snapshot || !OrRegionRegion(source->shaperegion, *snapshot)) goto failure;
    for (rr = source->shaperegion->RegionRectangle; rr; rr = rr->Next)
    {
        struct ScaleComposeRect logical, physical;
        struct Rectangle rect;
        LONG x = (LONG)source->shaperegion->bounds.MinX + source->bounds.MinX - window->LeftEdge;
        LONG y = (LONG)source->shaperegion->bounds.MinY + source->bounds.MinY - window->TopEdge;
        logical.left = x + rr->bounds.MinX; logical.top = y + rr->bounds.MinY;
        logical.right = x + rr->bounds.MaxX + 1; logical.bottom = y + rr->bounds.MaxY + 1;
        if (logical.left < 0) logical.left = 0;
        if (logical.top < 0) logical.top = 0;
        if (logical.right > window->Width) logical.right = window->Width;
        if (logical.bottom > window->Height) logical.bottom = window->Height;
        if (logical.left >= logical.right || logical.top >= logical.bottom) continue;
        if (!ScaleComposeDamage(&transform, &logical, &physical) ||
            physical.right > 32768 || physical.bottom > 32768) goto failure;
        rect.MinX = physical.left; rect.MinY = physical.top;
        rect.MaxX = physical.right - 1; rect.MaxY = physical.bottom - 1;
        if (!OrRectRegion(*result, &rect)) goto failure;
    }
    UnlockLayers(state->logical);
    return TRUE;
failure:
    if (*result) DisposeRegion(*result);
    *result = NULL;
    if (*snapshot) DisposeRegion(*snapshot);
    *snapshot = NULL;
    UnlockLayers(state->logical);
    return FALSE;
}

static BOOL shape_unchanged(struct Window *window, struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct LayersBase *LayersBase = GetPrivIBase(IntuitionBase)->LayersBase;
    struct WindowComposition *state = IW(window)->Composition;
    struct Layer *source = BLAYER(window) ? BLAYER(window) : WLAYER(window);
    BOOL same;
    if (!source) return FALSE;
    LockLayers(state->logical);
    same = source->bounds.MinX == state->shapeLeft && source->bounds.MinY == state->shapeTop &&
        ((source->shaperegion == NULL && state->logicalShapeSnapshot == NULL) ||
         (source->shaperegion && state->logicalShapeSnapshot &&
          AreRegionsEqual(source->shaperegion, state->logicalShapeSnapshot)));
    UnlockLayers(state->logical);
    return same;
}

static void commit_snapshot(struct Window *window, struct Region *snapshot,
                            struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct WindowComposition *state = IW(window)->Composition;
    struct Layer *source = BLAYER(window) ? BLAYER(window) : WLAYER(window);
    if (state->logicalShapeSnapshot) DisposeRegion(state->logicalShapeSnapshot);
    state->logicalShapeSnapshot = snapshot;
    state->shapeLeft = source->bounds.MinX; state->shapeTop = source->bounds.MinY;
}

BOOL WindowCompositionPrepare(struct Window *window, struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct LayersBase *LayersBase = GetPrivIBase(IntuitionBase)->LayersBase;
    struct Library *OOPBase = GetPrivIBase(IntuitionBase)->OOPBase;
    struct WindowComposition *state;
    struct BitMap *screenBitmap = window->WScreen->RastPort.BitMap;
    struct TagItem tags[4];
    ULONG depth;

    if (IW(window)->Composition) return FALSE;
    if (IW(window)->ScaleAware || !IS_HIDD_BM(screenBitmap)) return TRUE;
    if (window->WScreen->Width <= 0 || window->WScreen->Height <= 0)
        return FALSE;
    state = AllocVec(sizeof(*state), MEMF_PUBLIC | MEMF_CLEAR);
    if (!state) return FALSE;
    state->window = window;
    state->base = IntuitionBase;
    state->width = window->WScreen->Width;
    state->height = window->WScreen->Height;
    InitSemaphore(&state->damageLock);
    state->update.h_Entry = (HOOKFUNC)composition_update;
    state->update.h_Data = state;
    state->transform.factor = ScaleComposeFactor(IS(window->WScreen)->Scale);
    /* Window/screen dimensions are WORD-based, also bounding both scanlines. */
    if (state->width > 32767 || state->height > 32767) goto failure;
    state->sourceRow = AllocVec(state->width * sizeof(ULONG), MEMF_PUBLIC);
    state->destRow = AllocVec(state->width * sizeof(ULONG), MEMF_PUBLIC);
    state->logical = NewLayerInfo();
    if (state->logical && !SetLayerInfoCompositionRoot(state->logical,
            &window->WScreen->LayerInfo)) goto failure;
    {
        OOP_AttrBase HiddGCAttrBase = OOP_ObtainAttrBase(IID_Hidd_GC);
        if (HiddGCAttrBase)
        {
            struct TagItem gcTags[] = {
                {aHidd_GC_DrawMode, vHidd_GC_DrawMode_Copy},
                {aHidd_GC_ColorMask, ~(ULONG)0}, {TAG_DONE, 0}};
            state->gc = OOP_NewObject(NULL, (CONST_STRPTR)CLID_Hidd_GC, gcTags);
            OOP_ReleaseAttrBase(IID_Hidd_GC);
        }
    }
    if (!state->sourceRow || !state->destRow || !state->logical || !state->gc)
        goto failure;
    tags[0].ti_Tag = BMATags_Friend; tags[0].ti_Data = (IPTR)screenBitmap;
    tags[1].ti_Tag = BMATags_Clear; tags[1].ti_Data = TRUE;
    tags[2].ti_Tag = BMATags_UpdateHook; tags[2].ti_Data = (IPTR)&state->update;
    tags[3].ti_Tag = TAG_DONE; tags[3].ti_Data = 0;
    depth = GetBitMapAttr(screenBitmap, BMA_DEPTH);
    state->bitmap = AllocBitMap(state->width, state->height, depth,
        BMF_CHECKVALUE, (struct BitMap *)tags);
    if (!state->bitmap || !IS_HIDD_BM(state->bitmap)) goto failure;
    IW(window)->Composition = state;
    mark_damage(state, 0, 0, state->width, state->height);
    return TRUE;

failure:
    if (state->bitmap) FreeBitMap(state->bitmap);
    if (state->gc) OOP_DisposeObject(state->gc);
    if (state->logical) DisposeLayerInfo(state->logical);
    FreeVec(state->sourceRow); FreeVec(state->destRow); FreeVec(state);
    return FALSE;
}

struct Layer_Info *WindowCompositionLayerInfo(struct Window *window)
{
    struct WindowComposition *state = IW(window)->Composition;
    return state ? state->logical : &window->WScreen->LayerInfo;
}

struct BitMap *WindowCompositionBitMap(struct Window *window)
{
    struct WindowComposition *state = IW(window)->Composition;
    return state ? state->bitmap : window->WScreen->RastPort.BitMap;
}

struct Layer *WindowCompositionLayer(struct Window *window)
{
    struct WindowComposition *state = IW(window)->Composition;
    return state ? state->proxy : WLAYER(window);
}

UWORD WindowCompositionFactor(struct Window *window)
{
    struct WindowComposition *state = IW(window)->Composition;
    UWORD factor;
    if (!state) return 1;
    ObtainSemaphore(&state->damageLock);
    factor = state->transform.factor;
    ReleaseSemaphore(&state->damageLock);
    return factor;
}

BOOL WindowCompositionActivate(struct Window *window, struct IntuitionBase *IntuitionBase,
                              BOOL invisible, ULONG layerflags)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct LayersBase *LayersBase = GetPrivIBase(IntuitionBase)->LayersBase;
    struct WindowComposition *state = IW(window)->Composition;
    struct ScaleComposeRect bounds;
    struct Layer *parent = window->parent ? WindowCompositionLayer(window->parent) : NULL;
    LONG left, top, right, bottom;
    struct Region *shape = NULL, *snapshot = NULL;
    struct TagItem tags[] = {
        {LA_BackfillHook, (IPTR)LAYERS_NOBACKFILL},
        {LA_WindowPtr, (IPTR)window}, {LA_Hidden, invisible},
        {parent ? LA_ChildOf : TAG_IGNORE, (IPTR)parent},
        {LA_ShapeRegion, 0}, {TAG_DONE, 0}};
    if (!state) return TRUE;
    if (state->proxy || !WLAYER(window)) return FALSE;
    if (!physical_geometry(window, state->transform.factor, &bounds)) return FALSE;
    if (window->parent && (!parent || parent->LayerInfo != &window->WScreen->LayerInfo))
        return FALSE;
    if (!project_shape(window, state->transform.factor, &shape, &snapshot, IntuitionBase)) return FALSE;
    tags[4].ti_Data = (IPTR)shape;
    left = bounds.left; top = bounds.top;
    right = bounds.right - 1; bottom = bounds.bottom - 1;
    if (parent)
    {
        left -= parent->bounds.MinX; right -= parent->bounds.MinX;
        top -= parent->bounds.MinY; bottom -= parent->bounds.MinY;
    }
    state->proxy = CreateUpfrontLayerTagList(&window->WScreen->LayerInfo,
        window->WScreen->RastPort.BitMap, left, top, right, bottom,
        LAYERSIMPLE | (layerflags & (LAYERBACKDROP | LAYERPROTECTED)), tags);
    if (!state->proxy) { if (shape) DisposeRegion(shape); if (snapshot) DisposeRegion(snapshot); return FALSE; }
    state->projectedShape = shape;
    commit_snapshot(window, snapshot, IntuitionBase);
    state->transform.x = bounds.left;
    state->transform.y = bounds.top;
    state->logicalLeft = window->LeftEdge; state->logicalTop = window->TopEdge;
    state->logicalWidth = window->Width; state->logicalHeight = window->Height;
    mark_damage(state, 0, 0, state->width, state->height);
    return TRUE;
}

BOOL WindowCompositionSync(struct Window *window, struct IntuitionBase *IntuitionBase)
{
    struct LayersBase *LayersBase = GetPrivIBase(IntuitionBase)->LayersBase;
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct WindowComposition *state = IW(window)->Composition;
    struct ScaleComposeRect bounds;
    UWORD factor;
    BOOL changed, shapeChanged, success = FALSE;
    struct Region *shape = NULL, *snapshot = NULL;
    if (!state)
    {
        return WindowCompositionSyncFamily(window, IntuitionBase);
    }
    if (!state->proxy) return FALSE;
    /* MoveTask uses this same screen lock. Snapshot public geometry and shape
     * in screen -> logical order, including commits to the presentation layer. */
    LockLayerInfo(&window->WScreen->LayerInfo);
    factor = ScaleComposeFactor(IS(window->WScreen)->Scale);
    if (!physical_geometry(window, factor, &bounds)) goto done;
    changed = factor != state->transform.factor ||
        state->logicalLeft != window->LeftEdge || state->logicalTop != window->TopEdge ||
        state->logicalWidth != window->Width || state->logicalHeight != window->Height ||
        bounds.left != state->proxy->bounds.MinX || bounds.top != state->proxy->bounds.MinY ||
        bounds.right - 1 != state->proxy->bounds.MaxX || bounds.bottom - 1 != state->proxy->bounds.MaxY;
    /* Compare borrowed region contents against an owned snapshot without
     * allocating. In-place edits are detected even with a stable pointer. */
    if (!changed && shape_unchanged(window, IntuitionBase))
    { success = TRUE; goto done; }
    if (!project_shape(window, factor, &shape, &snapshot, IntuitionBase)) goto done;
    shapeChanged = (shape == NULL) != (state->projectedShape == NULL) ||
        (shape && state->projectedShape && !AreRegionsEqual(shape, state->projectedShape));
    changed |= shapeChanged;
    if (bounds.left != state->proxy->bounds.MinX ||
        bounds.top != state->proxy->bounds.MinY ||
        bounds.right - 1 != state->proxy->bounds.MaxX ||
        bounds.bottom - 1 != state->proxy->bounds.MaxY)
    {
        if (!MoveSizeLayer(state->proxy,
            bounds.left - state->proxy->bounds.MinX,
            bounds.top - state->proxy->bounds.MinY,
            bounds.right - bounds.left - state->proxy->Width,
            bounds.bottom - bounds.top - state->proxy->Height)) goto done;
    }
    if (shapeChanged)
    {
        ChangeLayerShape(state->proxy, shape, NULL);
        if (state->proxy->shaperegion != shape) goto done;
        if (state->projectedShape) DisposeRegion(state->projectedShape);
        state->projectedShape = shape; shape = NULL;
    }
    ObtainSemaphore(&state->damageLock);
    state->transform.x = bounds.left;
    state->transform.y = bounds.top;
    state->transform.factor = factor;
    ReleaseSemaphore(&state->damageLock);
    commit_snapshot(window, snapshot, IntuitionBase); snapshot = NULL;
    state->logicalLeft = window->LeftEdge; state->logicalTop = window->TopEdge;
    state->logicalWidth = window->Width; state->logicalHeight = window->Height;
    if (changed) mark_damage(state, 0, 0, state->width, state->height);
    success = TRUE;
done:
    if (shape) DisposeRegion(shape);
    if (snapshot) DisposeRegion(snapshot);
    ObtainSemaphore(&state->damageLock);
    state->scalingFailure = !success;
    ReleaseSemaphore(&state->damageLock);
    UnlockLayerInfo(&window->WScreen->LayerInfo);
    if (success && !WindowCompositionSyncFamily(window, IntuitionBase))
    {
        ObtainSemaphore(&state->damageLock);
        state->scalingFailure = TRUE;
        ReleaseSemaphore(&state->damageLock);
        success = FALSE;
    }
    return success;
}

BOOL WindowCompositionInputFrame(struct Window *window, LONG *x, LONG *y,
                                 UWORD *factor)
{
    struct WindowComposition *state = IW(window)->Composition;
    struct ScaleComposeTransform transform;
    if (!state)
    {
        transform.x = window->LeftEdge; transform.y = window->TopEdge;
        transform.factor = 1;
    }
    else
    {
        ObtainSemaphore(&state->damageLock);
        transform = state->transform;
        ReleaseSemaphore(&state->damageLock);
    }
    if (transform.factor < 1 || transform.factor > 4) return FALSE;
    *x = transform.x; *y = transform.y; *factor = transform.factor;
    return TRUE;
}

BOOL WindowCompositionMouse(struct Window *window, LONG x, LONG y,
                           LONG *logicalX, LONG *logicalY)
{
    struct WindowComposition *state = IW(window)->Composition;
    if (!state)
    {
        *logicalX = x - window->LeftEdge;
        *logicalY = y - window->TopEdge;
        return TRUE;
    }
    {
        struct ScaleComposeTransform transform;
        ObtainSemaphore(&state->damageLock);
        transform = state->transform;
        ReleaseSemaphore(&state->damageLock);
        return ScaleComposeInput(&transform, x, y, logicalX, logicalY);
    }
}

BOOL WindowCompositionPresent(struct Window *window, struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct LayersBase *LayersBase = GetPrivIBase(IntuitionBase)->LayersBase;
    OOP_MethodID HiddBitMapBase = GetPrivIBase(IntuitionBase)->ib_HiddBitMapBase;
    struct WindowComposition *state = IW(window)->Composition;
    struct ScaleComposeRect damage, logical, physical;
    struct ScaleComposeTransform transform;
    struct ClipRect *clip;
    BOOL dirty;

    if (!state || !state->proxy) return TRUE;
    /* Freeze physical clipping without owning unrelated aware client layers.
     * A cross-LI ClipBlit may already own this window's logical layer and need
     * an aware destination layer before it can release that logical layer.
     * Owning every screen layer while waiting here would deadlock that client.
     * Physical clip geometry changes require the screen Layer_Info lock. */
    LockLayerInfo(&window->WScreen->LayerInfo);
    LockLayers(state->logical);
    ObtainSemaphore(&state->damageLock);
    dirty = state->dirty || !!(state->proxy->Flags & LAYERREFRESH);
    damage = state->damage;
    if (state->proxy->Flags & LAYERREFRESH)
    {
        damage.left = 0; damage.top = 0;
        damage.right = state->width; damage.bottom = state->height;
    }
    state->dirty = FALSE;
    ReleaseSemaphore(&state->damageLock);
    if (!dirty) goto done;
    if (!IS_VISIBLE(state->proxy))
    {
        mark_damage(state, damage.left, damage.top, damage.right, damage.bottom);
        goto done;
    }

    /* Dirty canvas coordinates become window-local, then physical. */
    logical.left = damage.left - window->LeftEdge;
    logical.top = damage.top - window->TopEdge;
    logical.right = damage.right - window->LeftEdge;
    logical.bottom = damage.bottom - window->TopEdge;
    if (logical.left < 0) logical.left = 0;
    if (logical.top < 0) logical.top = 0;
    if (logical.right > window->Width) logical.right = window->Width;
    if (logical.bottom > window->Height) logical.bottom = window->Height;
    if (logical.left >= logical.right || logical.top >= logical.bottom) goto done;
    transform = state->transform;
    if (!ScaleComposeDamage(&transform, &logical, &physical))
    {
        mark_damage(state, damage.left, damage.top, damage.right, damage.bottom);
        UnlockLayers(state->logical);
        UnlockLayerInfo(&window->WScreen->LayerInfo);
        return FALSE;
    }
    for (clip = state->proxy->ClipRect; clip; clip = clip->Next)
    {
        LONG left, top, right, bottom, x, y, cachedY = -1;
        if (clip->lobs) continue;
        left = physical.left > clip->bounds.MinX ? physical.left : clip->bounds.MinX;
        top = physical.top > clip->bounds.MinY ? physical.top : clip->bounds.MinY;
        right = physical.right < (LONG)clip->bounds.MaxX + 1 ? physical.right : (LONG)clip->bounds.MaxX + 1;
        bottom = physical.bottom < (LONG)clip->bounds.MaxY + 1 ? physical.bottom : (LONG)clip->bounds.MaxY + 1;
        if (left < 0) left = 0;
        if (top < 0) top = 0;
        if (right > (LONG)state->width) right = state->width;
        if (bottom > (LONG)state->height) bottom = state->height;
        if (left >= right || top >= bottom) continue;
        for (y = top; y < bottom; y++)
        {
            LONG sy = window->TopEdge + (y - transform.y) / transform.factor;
            if (sy < 0 || sy >= (LONG)state->height) continue;
            if (sy != cachedY)
            {
                HIDD_BM_GetImage(HIDD_BM_OBJ(state->bitmap), (UBYTE *)state->sourceRow,
                    state->width * sizeof(ULONG), 0, sy, state->width, 1,
                    vHidd_StdPixFmt_Native32);
                cachedY = sy;
            }
            for (x = left; x < right; x++)
            {
                LONG sx = window->LeftEdge + (x - transform.x) / transform.factor;
                /* Source rectangle is additionally clipped to the logical canvas.
                 * Offscreen logical pixels retain layers' usual refresh semantics. */
                state->destRow[x - left] = sx >= 0 && sx < (LONG)state->width
                    ? state->sourceRow[sx] : 0;
            }
            HIDD_BM_PutImage(HIDD_BM_OBJ(window->WScreen->RastPort.BitMap), state->gc,
                (UBYTE *)state->destRow, (right - left) * sizeof(ULONG),
                left, y, right - left, 1, vHidd_StdPixFmt_Native32);
        }
        UpdateBitMap(window->WScreen->RastPort.BitMap, left, top, right-left, bottom-top);
    }
    state->proxy->Flags &= ~LAYERREFRESH;
    ClearRegion(state->proxy->DamageList);
done:
    UnlockLayers(state->logical);
    UnlockLayerInfo(&window->WScreen->LayerInfo);
    return TRUE;
}

BOOL WindowCompositionPresentScreen(struct Screen *screen, struct IntuitionBase *IntuitionBase)
{
    struct Window *window;
    BOOL success = TRUE;
    /* Resolve every family's geometry before sampling any visible pixels.
     * Open/close actions serialize the screen's window list. */
    for (window = screen->FirstWindow; window; window = window->NextWindow)
        if (!window->parent && !WindowCompositionSync(window, IntuitionBase))
            success = FALSE;
    for (window = screen->FirstWindow; window; window = window->NextWindow)
        if (!WindowCompositionFailed(window) &&
            !WindowCompositionPresent(window, IntuitionBase)) success = FALSE;
    return success;
}

void WindowCompositionDispose(struct Window *window, struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct LayersBase *LayersBase = GetPrivIBase(IntuitionBase)->LayersBase;
    struct Library *OOPBase = GetPrivIBase(IntuitionBase)->OOPBase;
    struct WindowComposition *state = IW(window)->Composition;
    if (!state) return;
    /* The action queue excludes presenters, and clients have stopped drawing.
     * No queued pointers exist: damage is an in-state bounding rectangle. */
    ObtainSemaphore(&state->damageLock);
    state->stopping = TRUE;
    ReleaseSemaphore(&state->damageLock);
    IW(window)->Composition = NULL;
    if (state->proxy) DeleteLayer(0, state->proxy);
    if (state->projectedShape) DisposeRegion(state->projectedShape);
    if (state->logicalShapeSnapshot) DisposeRegion(state->logicalShapeSnapshot);
    if (state->logical)
    {
        /* Logical client/border/requester layers must already be closed.
         * DisposeLayerInfo owns the implicit root as well as its extension. */
        DisposeLayerInfo(state->logical);
    }
    FreeBitMap(state->bitmap);
    OOP_DisposeObject(state->gc);
    FreeVec(state->sourceRow); FreeVec(state->destRow); FreeVec(state);
}

BOOL WindowCompositionFailed(struct Window *window)
{
    struct WindowComposition *state = IW(window)->Composition;
    BOOL failed;
    if (!state) return FALSE;
    ObtainSemaphore(&state->damageLock);
    failed = state->scalingFailure;
    ReleaseSemaphore(&state->damageLock);
    return failed;
}
