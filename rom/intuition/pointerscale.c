/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    This file is part of AROS, distributed under the AROS Public License 1.1.
*/
#include <exec/memory.h>
#include <cybergraphx/cybergraphics.h>
#include <graphics/sprite.h>
#include <graphics/driver.h>
#include <intuition/monitorclass.h>
#include <hidd/gfx.h>
#include <hidd/preparedpixels.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/oop.h>
#include <string.h>
#include "intuition_intern.h"
#include "monitorclass_intern.h"
#include "pointerscale.h"

struct PointerRenderGeneration
{
    struct SharedPointer *source;
    struct BitMap *bitmap;
    UBYTE *presentationARGB; /* identity only: owned by the display snapshot */
    LONG xoffset, yoffset;
    ULONG scale;
    BOOL ownsBitmap;
};
struct PreparedScreenPointerScale
{
    struct IMonitorNode *monitor;
    struct PointerRenderGeneration *before, *after;
    struct HIDD_CursorState previous, proposed;
    struct HIDD_CursorState *live;
    struct Screen *screen, *previousScreen;
    struct SharedPointer *previousSource;
    BOOL noop, installed;
};

static BOOL PointerScaleValid(ULONG scale)
{
    return scale >= 100 && scale <= 400 && scale % 25 == 0;
}
static BOOL PointerScaleOffset(LONG point, ULONG scale, LONG *pixel)
{
    QUAD product = (QUAD)point * scale;
    QUAD result = product < 0 ? -((-product + 50) / 100) : (product + 50) / 100;
    if (result < (-2147483647LL - 1) || result > 2147483647LL) return FALSE;
    *pixel = result;
    return TRUE;
}
static BOOL PointerScaleExtent(ULONG point, ULONG scale, ULONG *pixel)
{
    UQUAD result = ((UQUAD)point * scale + 50) / 100;
    if (!result || result > 32767) return FALSE;
    *pixel = result;
    return TRUE;
}
/* Indexed role is explicit at built-in construction, never inferred from an
 * application bitmap. Geometry is in 1/256-points, sampled independently at
 * the destination size so fractional scales have antialiased vector edges. */
static ULONG PointerBuiltinSample(ULONG kind, LONG x, LONG y)
{
    if (x < 0 || y < 0 || x >= 4096 || y >= 4096) return 0;
    if (kind == POINTER_BUILTIN_ARROW)
    {
        if (x < 0 || y < 0 || x + y >= 3072) return 0;
        return x >= 256 && y >= 256 && x + y < 2304 ? 2 : 1;
    }
    if (kind == POINTER_BUILTIN_BUSY)
    {
        LONG dx = x - 2048, dy = y - 2048, radius = dx * dx + dy * dy;
        if (radius >= 1792 * 1792) return 0;
        if (radius >= 1536 * 1536 ||
            (dx >= -128 && dx <= 128 && dy <= 0 && dy >= -1024) ||
            (dy >= -128 && dy <= 128 && dx >= 0 && dx <= 768)) return 1;
        return 2;
    }
    return 0;
}
static void PointerRenderBuiltin(UBYTE *out, ULONG width, ULONG height,
    ULONG scale, ULONG kind, const UBYTE colors[3][4])
{
    ULONG x, y, sx, sy;
    for (y = 0; y < height; ++y) for (x = 0; x < width; ++x)
    {
        ULONG a = 0, r = 0, g = 0, b = 0;
        for (sy = 0; sy < 8; ++sy) for (sx = 0; sx < 8; ++sx)
        {
            LONG px = ((UQUAD)(2 * (x * 8 + sx) + 1) * 100 * 256) / (16 * scale);
            LONG py = ((UQUAD)(2 * (y * 8 + sy) + 1) * 100 * 256) / (16 * scale);
            const UBYTE *c = colors[PointerBuiltinSample(kind, px, py)];
            a += c[0]; r += c[0] * c[1]; g += c[0] * c[2]; b += c[0] * c[3];
        }
        out[0] = (a + 32) / 64;
        out[1] = a ? (r + a / 2) / a : 0;
        out[2] = a ? (g + a / 2) / a : 0;
        out[3] = a ? (b + a / 2) / a : 0;
        out += 4;
    }
}
/* Exact integer replication; fractional bitmap fallback interpolates in
 * premultiplied alpha, avoiding dark fringes from transparent source RGB. */
static void PointerRenderBitmap(UBYTE *out, ULONG width, ULONG height,
    const UBYTE *source, ULONG sw, ULONG sh, ULONG scale)
{
    ULONG x, y;
    for (y = 0; y < height; ++y) for (x = 0; x < width; ++x)
    {
        if (scale % 100 == 0)
        {
            ULONG factor = scale / 100;
            memcpy(out, source + ((y / factor) * sw + x / factor) * 4, 4);
        }
        else
        {
            QUAD fx = ((QUAD)(2 * x + 1) * sw * 65536) / (2 * width) - 32768;
            QUAD fy = ((QUAD)(2 * y + 1) * sh * 65536) / (2 * height) - 32768;
            ULONG x0, y0, x1, y1, ix, iy;
            UQUAD a = 0, r = 0, g = 0, b = 0;
            if (fx < 0) fx = 0;
            if (fy < 0) fy = 0;
            if (fx > ((QUAD)sw - 1) * 65536) fx = ((QUAD)sw - 1) * 65536;
            if (fy > ((QUAD)sh - 1) * 65536) fy = ((QUAD)sh - 1) * 65536;
            x0 = fx >> 16; y0 = fy >> 16;
            x1 = x0 + 1 < sw ? x0 + 1 : x0;
            y1 = y0 + 1 < sh ? y0 + 1 : y0;
            for (iy = 0; iy < 2; ++iy) for (ix = 0; ix < 2; ++ix)
            {
                UQUAD weight = (ix ? (fx & 65535) : 65536 - (fx & 65535)) *
                    (UQUAD)(iy ? (fy & 65535) : 65536 - (fy & 65535));
                const UBYTE *c = source + ((iy ? y1 : y0) * sw + (ix ? x1 : x0)) * 4;
                UQUAD alpha = weight * c[0];
                a += alpha; r += alpha * c[1]; g += alpha * c[2]; b += alpha * c[3];
            }
            out[0] = (a + (1ULL << 31)) >> 32;
            out[1] = a ? (r + a / 2) / a : 0;
            out[2] = a ? (g + a / 2) / a : 0;
            out[3] = a ? (b + a / 2) / a : 0;
        }
        out += 4;
    }
}

static void PointerReleaseGeneration(struct PointerRenderGeneration *generation,
    struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    if (!generation) return;
    if (generation->ownsBitmap) FreeBitMap(generation->bitmap);
    ReleaseSharedPointer(generation->source, IntuitionBase);
    FreeVec(generation);
}
static struct PointerRenderGeneration *PointerPrepareGeneration(struct SharedPointer *source,
    ULONG scale, BOOL vector, struct IntuitionBase *IntuitionBase)
{
    struct IntIntuitionBase *ibase = GetPrivIBase(IntuitionBase);
    struct GfxBase *GfxBase = ibase->GfxBase;
    OOP_MethodID HiddBitMapBase = ibase->ib_HiddBitMapBase;
    struct PointerRenderGeneration *generation;
    struct BitMap *bitmap;
    ULONG sw, sh, width, height;
    UBYTE *input = NULL, *output = NULL;
    if (!source || !source->sprite || !source->sprite->es_BitMap || !PointerScaleValid(scale))
        return NULL;
    if (source->physicalPixels) scale = 100;
    generation = AllocVec(sizeof(*generation), MEMF_CLEAR | MEMF_PUBLIC);
    if (!generation) return NULL;
    ObtainSharedPointer(source, IntuitionBase);
    generation->source = source;
    generation->scale = scale;
    if (!PointerScaleOffset(source->xoffset, scale, &generation->xoffset) ||
        !PointerScaleOffset(source->yoffset, scale, &generation->yoffset)) goto fail;
    bitmap = source->sprite->es_BitMap;
    generation->bitmap = bitmap;
    if (scale == 100 && (!vector || !source->builtinShape || source->physicalPixels))
        return generation;
    if (!IS_HIDD_BM(bitmap)) goto fail;
    sw = GetBitMapAttr(bitmap, BMA_WIDTH);
    sh = GetBitMapAttr(bitmap, BMA_HEIGHT);
    if (!PointerScaleExtent(sw, scale, &width) || !PointerScaleExtent(sh, scale, &height) ||
        sw > (ULONG)-1 / sh / 4 || width > (ULONG)-1 / height / 4) goto fail;
    output = AllocVec(width * height * 4, MEMF_ANY);
    if (!output) goto fail;
    if (vector && source->builtinShape && !source->physicalPixels)
    {
        UBYTE colors[3][4] = {{0}};
        ULONG indices[] = {0, 8, 10}, i;
        for (i = 1; i < 3; ++i)
        {
            const struct Color32 *c = &ibase->Colors[indices[i]];
            colors[i][0] = ibase->PointerAlpha >> 8;
            colors[i][1] = c->red >> 24;
            colors[i][2] = c->green >> 24;
            colors[i][3] = c->blue >> 24;
        }
        PointerRenderBuiltin(output, width, height, scale, source->builtinShape, colors);
    }
    else
    {
        input = AllocVec(sw * sh * 4, MEMF_ANY);
        if (!input) goto fail;
        HIDD_BM_GetImage(HIDD_BM_OBJ(bitmap), input, sw * 4, 0, 0, sw, sh,
            vHidd_StdPixFmt_ARGB32);
        PointerRenderBitmap(output, width, height, input, sw, sh, scale);
    }
    generation->bitmap = AllocBitMap(width, height, 32,
        BMF_SPECIALFMT | ((ULONG)PIXFMT_ARGB32 << 24), bitmap);
    if (!generation->bitmap) goto fail;
    generation->ownsBitmap = TRUE;
    if (!IS_HIDD_BM(generation->bitmap)) goto fail;
    HIDD_BM_PutImage(HIDD_BM_OBJ(generation->bitmap), NULL, output, width * 4,
        0, 0, width, height, vHidd_StdPixFmt_ARGB32);
    FreeVec(input); FreeVec(output);
    return generation;
fail:
    FreeVec(input); FreeVec(output);
    PointerReleaseGeneration(generation, IntuitionBase);
    return NULL;
}

BOOL SetMonitorPointerScale(struct IMonitorNode *monitor, struct Screen *screen,
    struct SharedPointer *source, struct IntuitionBase *IntuitionBase)
{
    struct IntIntuitionBase *ibase = GetPrivIBase(IntuitionBase);
    struct GfxBase *GfxBase = ibase->GfxBase;
    OOP_MethodID HiddDisplayBase = ibase->ib_HiddDisplayBase;
    OOP_Object *display = monitor->handle->display;
    struct PointerRenderGeneration *generation, *old;
    struct HIDD_CursorState proposed = {0}, previous = {0};
    ULONG scale = screen ? GetPrivScreen(screen)->Scale : 100;
    BOOL capable = HIDD_Display_SupportsPreparedCursor(display);
    if (!source || !PointerScaleValid(scale)) return FALSE;
    if (source->physicalPixels) scale = 100;
    if (monitor->pointerRender && monitor->pointer == source &&
        monitor->pointerScreen == screen && monitor->pointerRender->scale == scale)
        return TRUE;
    /* An unsupported display keeps its existing 100% pointer path. This is a
     * target capability boundary, never a substitute for scaled publication. */
    if (!capable && scale != 100) return FALSE;
    generation = PointerPrepareGeneration(source, scale, capable, IntuitionBase);
    if (!generation) return FALSE;
    if (capable)
    {
        if (!IS_HIDD_BM(generation->bitmap)) goto fail;
        if (!HIDD_Display_PrepareCursorShape(display, HIDD_BM_OBJ(generation->bitmap),
            generation->xoffset, generation->yoffset, &proposed)) goto fail;
        generation->presentationARGB = proposed.cursor_argb;
        if (!HIDD_Display_InstallCursorShape(display, &proposed, &previous)) goto fail;
    }
    else
    {
        struct BitMap *bitmap = generation->bitmap, *temporary = NULL;
        if (!IS_HIDD_BM(bitmap))
        {
            ULONG width = GetBitMapAttr(bitmap, BMA_WIDTH), height = GetBitMapAttr(bitmap, BMA_HEIGHT);
            temporary = AllocBitMap(width, height, GetBitMapAttr(bitmap, BMA_DEPTH), BMF_DISPLAYABLE, NULL);
            if (!temporary) goto fail;
            BltBitMap(bitmap, 0, 0, temporary, 0, 0, width, height, 0xC0, ~0L, NULL);
            bitmap = temporary;
        }
        if (!SetDisplayPointerShape(display, source, bitmap, IntuitionBase))
        {
            if (temporary) FreeBitMap(temporary);
            goto fail;
        }
        if (monitor->tmpPtr) FreeBitMap(monitor->tmpPtr);
        monitor->tmpPtr = temporary;
    }
    old = monitor->pointerRender;
    monitor->pointerRender = generation;
    monitor->pointer = source;
    monitor->pointerScreen = screen;
    if (capable) HIDD_Display_ReleaseCursorShape(display, &previous);
    PointerReleaseGeneration(old, IntuitionBase);
    return TRUE;
fail:
    if (capable) HIDD_Display_ReleaseCursorShape(display, &proposed);
    PointerReleaseGeneration(generation, IntuitionBase);
    return FALSE;
}
void ForgetScreenPointerScale(struct Screen *screen, struct IntuitionBase *IntuitionBase)
{
    ULONG lock = LockIBase(0);
    struct IntIntuitionBase *ibase = GetPrivIBase(IntuitionBase);
    Object *node = GetPrivScreen(screen)->IMonitorNode;
    if (node)
    {
        struct IMonitorNode *monitor = INST_DATA(ibase->monitorclass, node);
        if (monitor->pointerScreen == screen) monitor->pointerScreen = NULL;
    }
    UnlockIBase(lock);
}
void DisposeMonitorPointerScale(struct IMonitorNode *monitor,
    struct IntuitionBase *IntuitionBase)
{
    PointerReleaseGeneration(monitor->pointerRender, IntuitionBase);
    monitor->pointerRender = NULL;
    monitor->pointerScreen = NULL;
    monitor->pointer = NULL;
}

struct PreparedScreenPointerScale *PrepareScreenPointerScale(struct Screen *screen,
    ULONG scale, struct IntuitionBase *IntuitionBase)
{
    struct IntIntuitionBase *ibase = GetPrivIBase(IntuitionBase);
    OOP_MethodID HiddDisplayBase = ibase->ib_HiddDisplayBase;
    struct PreparedScreenPointerScale *pending;
    struct IMonitorNode *monitor;
    struct SharedPointer *source;
    Object *node;
    CONST_STRPTR reason = "generation preparation";
    if (!screen || !PointerScaleValid(scale))
    { bug("[SCALE] pointer refused: invalid screen or percentage\n"); return NULL; }
    node = GetPrivScreen(screen)->IMonitorNode;
    pending = AllocVec(sizeof(*pending), MEMF_CLEAR | MEMF_PUBLIC);
    if (!pending)
    { bug("[SCALE] pointer refused: pending allocation\n"); return NULL; }
    if (!node)
    {
        /* A detached/headless screen has no selected pointer presentation. */
        pending->noop = TRUE;
        return pending;
    }
    monitor = INST_DATA(ibase->monitorclass, node);
    pending->monitor = monitor;
    pending->screen = screen;
    pending->previousScreen = monitor->pointerScreen;
    pending->previousSource = monitor->pointer;
    if (monitor->pointerScreen != screen &&
        (monitor->pointerScreen || FindFirstScreen(node, IntuitionBase) != screen))
    {
        pending->noop = TRUE;
        return pending;
    }
    pending->before = monitor->pointerRender;
    source = monitor->pointerScreen == screen ? monitor->pointer : GetPrivScreen(screen)->Pointer;
    if (!source && ibase->DefaultPointer)
        GetAttr(POINTERA_SharedPointer, ibase->DefaultPointer, (IPTR *)&source);
    if (!source)
    {
        /* No Intuition pointer resource exists for this screen. Preserve any
         * driver-owned presentation; no scaling promise is made for it. */
        bug("[SCALE] pointer unchanged: no Intuition source\n");
        pending->noop = TRUE;
        return pending;
    }
    reason = "unsupported prepared cursor backend";
    if (!HIDD_Display_SupportsPreparedCursor(monitor->handle->display)) goto fail;
    reason = "source generation allocation or rasterization";
    pending->after = PointerPrepareGeneration(source, scale, TRUE, IntuitionBase);
    if (!pending->after) goto fail;
    reason = "non-HIDD prepared bitmap";
    if (!IS_HIDD_BM(pending->after->bitmap)) goto fail;
    reason = "display cursor preparation";
    if (!HIDD_Display_PrepareCursorShape(monitor->handle->display,
        HIDD_BM_OBJ(pending->after->bitmap), pending->after->xoffset, pending->after->yoffset,
        &pending->proposed)) goto fail;
    pending->after->presentationARGB = pending->proposed.cursor_argb;
    return pending;
fail:
    bug("[SCALE] pointer refused: %s\n", reason);
    PointerReleaseGeneration(pending->after, IntuitionBase);
    FreeVec(pending);
    return NULL;
}
struct HIDD_CursorState *PreparedScreenPointerCursor(struct PreparedScreenPointerScale *pending)
{
    return pending && !pending->noop ? &pending->proposed : NULL;
}
BOOL BindScreenPointerScale(struct PreparedScreenPointerScale *pending,
    struct HIDD_PixelMap *map)
{
    if (!pending || pending->installed) return FALSE;
    if (pending->noop) return TRUE;
    if (!map || !map->cursorState || !map->cursorCookie || !map->cursorSemaphore)
    { bug("[SCALE] pointer refused: missing typed framebuffer lease\n"); return FALSE; }
    if (map->cursorState->cursor_argb != (pending->before ? pending->before->presentationARGB : NULL))
    {
        /* graphics ChangeExtSpriteA can replace the Display shape directly.
         * Refuse rather than treat an unrelated shape as the pinned source. */
        bug("[SCALE] pointer refused: live shape changed outside Intuition\n");
        return FALSE;
    }
    pending->live = map->cursorState;
    pending->previous = *map->cursorState;
    return TRUE;
}
void InstallScreenPointerScale(struct PreparedScreenPointerScale *pending, BOOL rollback)
{
    if (!pending || pending->noop || !pending->live) return;
    *pending->live = rollback ? pending->previous : pending->proposed;
    pending->monitor->pointerRender = rollback ? pending->before : pending->after;
    pending->monitor->pointer = rollback ? pending->previousSource : pending->after->source;
    pending->monitor->pointerScreen = rollback ? pending->previousScreen : pending->screen;
    pending->installed = !rollback;
}
BOOL ReleaseScreenPointerScale(struct PreparedScreenPointerScale *pending,
    BOOL committed, struct IntuitionBase *IntuitionBase)
{
    struct IntIntuitionBase *ibase = GetPrivIBase(IntuitionBase);
    OOP_MethodID HiddDisplayBase = ibase->ib_HiddDisplayBase;
    BOOL released;
    if (!pending) return TRUE;
    if (pending->noop) { FreeVec(pending); return TRUE; }
    if (committed != pending->installed) return FALSE;
    /* These calls occur after all pixel leases and screen commit locks leave.
     * The display refuses release of buffers that are still its live state. */
    released = HIDD_Display_ReleaseCursorShape(pending->monitor->handle->display,
        committed ? &pending->previous : &pending->proposed);
    if (!released) return FALSE;
    PointerReleaseGeneration(committed ? pending->before : pending->after, IntuitionBase);
    FreeVec(pending);
    return TRUE;
}
