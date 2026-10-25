#ifndef INTUITION_SCALEDETAIL_H
#define INTUITION_SCALEDETAIL_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    One wording for the exact cause of a screen scale refusal. Intuition's
    kernel log line and the CLI/Settings diagnostics both format the detail
    of a struct ScreenScaleResult with ScaleDetailFormat. Freestanding: no
    C library, no allocation; reads only the caller-owned receipt.
*/
#include <intuition/screens.h>
#include <intuition/windecorclass.h>
#include <graphics/layertransaction.h>
#include <hidd/preparedpixels.h>

struct ScaleDetailText
{
    char *at, *end;
};

static inline void ScaleDetailPut(struct ScaleDetailText *t, const char *text)
{
    while (*text && t->at + 1 < t->end) *t->at++ = *text++;
    *t->at = 0;
}

static inline void ScaleDetailLong(struct ScaleDetailText *t, LONG value)
{
    char digits[12];
    ULONG magnitude = value < 0 ? 0UL - (ULONG)value : (ULONG)value, n = 0;
    do digits[n++] = '0' + magnitude % 10; while ((magnitude /= 10) && n < sizeof(digits));
    if (value < 0 && t->at + 1 < t->end) *t->at++ = '-';
    while (n && t->at + 1 < t->end) *t->at++ = digits[--n];
    *t->at = 0;
}

/* Copies an owned, possibly unterminated name; control bytes, CSI and quotes
 * would break the one-line diagnostic and become spaces. */
static inline void ScaleDetailName(struct ScaleDetailText *t, const UBYTE *name, ULONG size)
{
    ULONG i;
    ScaleDetailPut(t, "'");
    for (i = 0; i < size && name[i] && t->at + 1 < t->end; ++i)
    {
        UBYTE value = name[i];
        *t->at++ = value < 32 || (value >= 127 && value <= 159) || value == '\'' || value == '"' ?
            ' ' : (char)value;
    }
    *t->at = 0;
    ScaleDetailPut(t, "'");
}

static inline void ScaleDetailHex(struct ScaleDetailText *t, IPTR value)
{
    char digits[2 * sizeof(IPTR)];
    ULONG n = 0;
    do digits[n++] = "0123456789abcdef"[value & 15]; while ((value >>= 4) && n < sizeof(digits));
    ScaleDetailPut(t, "0x");
    while (n && t->at + 1 < t->end) *t->at++ = digits[--n];
    *t->at = 0;
}

static inline const char *ScaleDecorMethodName(ULONG method)
{
    switch (method)
    {
    case WDM_PREPARESCALEWINDOW: return "WDM_PREPARESCALEWINDOW";
    case WDM_LAYOUTSCALEBORDERGADGETS: return "WDM_LAYOUTSCALEBORDERGADGETS";
    case WDM_GETSCALEDEFSIZE_SYSIMAGE: return "WDM_GETSCALEDEFSIZE_SYSIMAGE";
    case WDM_PREPARESCALESHAPE: return "WDM_PREPARESCALESHAPE";
    default: return "an unknown method";
    }
}

static inline const char *ScaleDecorMethodWork(ULONG method)
{
    switch (method)
    {
    case WDM_PREPARESCALEWINDOW: return "frame resource preparation";
    case WDM_LAYOUTSCALEBORDERGADGETS: return "frame gadget layout";
    case WDM_GETSCALEDEFSIZE_SYSIMAGE: return "system image sizing";
    case WDM_PREPARESCALESHAPE: return "window shape preparation";
    default: return "frame preparation";
    }
}

static inline void ScaleDetailDimensions(struct ScaleDetailText *t, LONG width, LONG height)
{
    ScaleDetailLong(t, width); ScaleDetailPut(t, " x "); ScaleDetailLong(t, height);
}

static inline void ScaleDetailDimensionsAt(struct ScaleDetailText *t, LONG width, LONG height,
    LONG x, LONG y)
{
    ScaleDetailDimensions(t, width, height); ScaleDetailPut(t, " at ");
    ScaleDetailLong(t, x); ScaleDetailPut(t, ","); ScaleDetailLong(t, y);
}

/* v: x, y, width, height */
static inline void ScaleDetailRect(struct ScaleDetailText *t, const LONG *v)
{
    ScaleDetailDimensionsAt(t, v[2], v[3], v[0], v[1]);
}

static inline const char *ScaleLayerRoleName(ULONG role)
{
    switch (role)
    {
    case SSR_LAYER_ROOT: return "the screen root layer";
    case SSR_LAYER_BAR: return "the screen title bar layer";
    case SSR_LAYER_WINDOW: return "the window layer";
    case SSR_LAYER_WINDOW_BORDER: return "the window border layer";
    case SSR_LAYER_WINDOW_CLIENT: return "the window client layer";
    case SSR_LAYER_REQUESTER: return "a requester layer";
    case SSR_LAYER_PRESENTATION: return "the window presentation layer";
    case SSR_LAYER_CANVAS: return "the window layer in its logical canvas";
    case SSR_LAYER_CANVAS_ROOT: return "the root layer of the window's logical canvas";
    case SSR_LAYER_PRIVATE: return "a layer in a private canvas";
    default: return "layer";
    }
}

static inline void ScaleDetailLayerName(struct ScaleDetailText *t, const struct ScreenScaleResult *r)
{
    if (r->ssr_LayerRole == SSR_LAYER_NONE) return;
    ScaleDetailPut(t, ScaleLayerRoleName(r->ssr_LayerRole));
    if (r->ssr_LayerRole == SSR_LAYER_OTHER)
    {
        ScaleDetailPut(t, " "); ScaleDetailHex(t, r->ssr_Layer);
        if (r->ssr_LayerFlags & LGRF_ROOT) ScaleDetailPut(t, " (a stack root)");
        else if (r->ssr_LayerFlags & LGRF_PRIVATE) ScaleDetailPut(t, " (private canvas)");
    }
    ScaleDetailPut(t, ": ");
}

static inline const char *ScaleScanoutName(LONG cause)
{
    switch (cause)
    {
    case HIDD_SCANOUT_NO_FRAMEBUFFER: return "there is no framebuffer";
    case HIDD_SCANOUT_CALLBACK: return "presentation is a driver callback";
    case HIDD_SCANOUT_NOT_OWNED: return "the boot console still owns the framebuffer";
    case HIDD_SCANOUT_PANNED: return "the screen is panned by ";
    case HIDD_SCANOUT_WIDTH: return "the scanout width is ";
    case HIDD_SCANOUT_HEIGHT: return "the scanout height is ";
    case HIDD_SCANOUT_DEPTH: return "the scanout bytes per pixel are ";
    case HIDD_SCANOUT_MASKS: return "the scanout red mask is ";
    case HIDD_SCANOUT_PITCH: return "the scanout pitch does not fit its size: ";
    default: return "an unknown scanout difference ";
    }
}

static inline const char *ScaleCursorRenderName(LONG cause)
{
    switch (cause)
    {
    case HIDD_CURSOR_REFUSE_LEASE: return "there is no cursor lease, state or staging";
    case HIDD_CURSOR_REFUSE_ALIAS: return "the proposed pointer aliases the live pointer";
    case HIDD_CURSOR_REFUSE_TARGET: return "the live pointer is drawn on another bitmap";
    case HIDD_CURSOR_REFUSE_SHAPE: return "the proposed save-under is missing or too small, bytes: ";
    case HIDD_CURSOR_REFUSE_RENDERER: return "the staging renderer could not be created";
    case HIDD_CURSOR_REFUSE_BACKUP: return "the frozen pointer has no save-under";
    default: return "an unknown step refused";
    }
}

/* The driver's own cause; v holds its two values. */
static inline void ScaleDetailDriver(struct ScaleDetailText *t, ULONG cause, const LONG *v)
{
    switch (cause)
    {
    case HIDD_PIXELS_REFUSE_UNSUPPORTED: ScaleDetailPut(t, "the class has no prepared mapping"); break;
    case HIDD_PIXELS_REFUSE_SUBCLASS:
        ScaleDetailPut(t, "the object is a subclass of the audited class"); break;
    case HIDD_PIXELS_REFUSE_STORAGE:
        ScaleDetailPut(t, v[1] ? "its CPU storage has " : "it has no CPU storage, ");
        ScaleDetailLong(t, v[0]); ScaleDetailPut(t, " bytes per pixel; 4 are required"); break;
    case HIDD_PIXELS_REFUSE_COLORMODEL:
        ScaleDetailPut(t, "its colour model "); ScaleDetailLong(t, v[0]);
        ScaleDetailPut(t, " is not truecolour"); break;
    case HIDD_PIXELS_REFUSE_SWAPPED: ScaleDetailPut(t, "its pixel format is byte-swapped"); break;
    case HIDD_PIXELS_REFUSE_GEOMETRY:
        ScaleDetailPut(t, "stride "); ScaleDetailLong(t, v[0]); ScaleDetailPut(t, " x height ");
        ScaleDetailLong(t, v[1]); ScaleDetailPut(t, " overflows 32 bits"); break;
    case HIDD_PIXELS_REFUSE_STORAGE_SIZE:
        ScaleDetailPut(t, "its storage of "); ScaleDetailLong(t, v[0]);
        ScaleDetailPut(t, " bytes is smaller than the "); ScaleDetailLong(t, v[1]);
        ScaleDetailPut(t, " bytes mapped"); break;
    case HIDD_PIXELS_REFUSE_SCANOUT:
        ScaleDetailPut(t, "the shown bitmap differs from the scanout: ");
        ScaleDetailPut(t, ScaleScanoutName(v[0]));
        if (v[0] == HIDD_SCANOUT_MASKS) ScaleDetailHex(t, (ULONG)v[1]);
        else if (v[0] >= HIDD_SCANOUT_PANNED) ScaleDetailLong(t, v[1]);
        break;
    case HIDD_PIXELS_REFUSE_DISPLAY: ScaleDetailPut(t, "the CursorFB owner is not a Display"); break;
    case HIDD_PIXELS_REFUSE_CURSOR:
        ScaleDetailPut(t, "the drawn pointer's save-under is inconsistent (");
        ScaleDetailLong(t, v[0]); ScaleDetailPut(t, v[1] ? " bytes per pixel)" : " bytes per pixel, no buffers)");
        break;
    case HIDD_PIXELS_REFUSE_CURSOR_RENDER:
        ScaleDetailPut(t, ScaleCursorRenderName(v[0]));
        if (v[0] == HIDD_CURSOR_REFUSE_SHAPE) ScaleDetailLong(t, v[1]);
        break;
    case HIDD_PIXELS_REFUSE_FOREIGN: ScaleDetailPut(t, "the map belongs to another lease"); break;
    default: ScaleDetailPut(t, "the driver stated no cause"); break;
    }
}

static inline const char *ScaleLeaseBufferName(const struct ScreenScaleResult *r)
{
    switch (r->ssr_LayerPixels)
    {
    case LGR_PIXELS_STACK:
        return (r->ssr_LayerFlags & LGRF_PRIVATE) ? "the private canvas bitmap" : "the screen bitmap";
    case LGR_PIXELS_SUPER: return "its SuperBitMap";
    case LGR_PIXELS_OBSCURED: return "a hidden smart-refresh backing bitmap";
    case LGR_PIXELS_BACKING: return "a new hidden smart-refresh backing bitmap";
    case LGR_PIXELS_SOURCE: return "the staging bitmap";
    default: return "a bitmap";
    }
}

static inline void ScaleDetailLease(struct ScaleDetailText *t, const struct ScreenScaleResult *r)
{
    const LONG *v = r->ssr_LayerValue;
    ScaleDetailPut(t, "the pixel lease of ");
    ScaleDetailPut(t, ScaleLeaseBufferName(r));
    ScaleDetailPut(t, " was refused: ");
    switch (r->ssr_LayerLease)
    {
    case LGR_LEASE_NOT_HIDD: ScaleDetailPut(t, "it is not a HIDD bitmap"); break;
    case LGR_LEASE_NO_OBJECT: ScaleDetailPut(t, "the HIDD bitmap has no object"); break;
    case LGR_LEASE_OOP: ScaleDetailPut(t, "oop.library could not be opened"); break;
    case LGR_LEASE_DRIVER:
    case LGR_LEASE_CURSOR:
        ScaleDetailName(t, r->ssr_DetailName, sizeof(r->ssr_DetailName));
        ScaleDetailPut(t, r->ssr_LayerLease == LGR_LEASE_DRIVER ?
            " refused PreparePixels: " : " refused RenderPreparedCursor: ");
        ScaleDetailDriver(t, r->ssr_LayerDriver, v);
        break;
    case LGR_LEASE_MAP:
        ScaleDetailPut(t, "the driver's map is invalid ("); ScaleDetailDimensions(t, v[0], v[1]);
        ScaleDetailPut(t, ", stride "); ScaleDetailLong(t, v[2]);
        ScaleDetailPut(t, ", size "); ScaleDetailLong(t, v[3]); ScaleDetailPut(t, ")");
        break;
    case LGR_LEASE_MEMORY:
        ScaleDetailPut(t, "allocation of "); ScaleDetailLong(t, v[0]);
        ScaleDetailPut(t, " bytes for its copy failed"); break;
    case LGR_LEASE_RANGE:
        ScaleDetailPut(t, "the rectangle "); ScaleDetailRect(t, v);
        ScaleDetailPut(t, " is outside the mapping"); break;
    case LGR_LEASE_SEALED: ScaleDetailPut(t, "its staging is read-only or already sealed"); break;
    default: ScaleDetailPut(t, "an unknown lease step refused"); break;
    }
}

static inline const char *ScaleLayerMemoryName(LONG what)
{
    switch (what)
    {
    case LGR_MEM_BATCH: return "the batch";
    case LGR_MEM_STACKS: return "the stack table";
    case LGR_MEM_LAYERS: return "the layer table";
    case LGR_MEM_REGION: return "a region";
    case LGR_MEM_CONTENT: return "the detached canvas of ";
    case LGR_MEM_CLIP: return "a clip rectangle";
    case LGR_MEM_BACKING: return "a hidden smart-refresh backing of ";
    case LGR_MEM_ROW: return "a conversion row";
    default: return "an unknown buffer";
    }
}

/* SSR_DETAIL_LAYER: role prefix, then the Layers check. */
static inline void ScaleDetailLayer(struct ScaleDetailText *t, const struct ScreenScaleResult *r)
{
    const LONG *v = r->ssr_LayerValue;
    ScaleDetailLayerName(t, r);
    switch (r->ssr_LayerCheck)
    {
    case LGR_ARGUMENTS:
        ScaleDetailPut(t, "Layers refused the batch arguments ("); ScaleDetailLong(t, v[0]);
        ScaleDetailPut(t, ")"); break;
    case LGR_ROOT_COMPOSED: ScaleDetailPut(t, "the screen's Layer_Info is itself a composed canvas"); break;
    case LGR_NO_MEMORY:
        if (v[1] == LGR_MEM_REGION) { ScaleDetailPut(t, "a region allocation failed"); break; }
        ScaleDetailPut(t, "allocation of "); ScaleDetailLong(t, v[0]); ScaleDetailPut(t, " bytes for ");
        ScaleDetailPut(t, ScaleLayerMemoryName(v[1]));
        if (v[1] == LGR_MEM_CONTENT || v[1] == LGR_MEM_BACKING)
        { ScaleDetailDimensions(t, v[2], v[3]); ScaleDetailPut(t, " pixels"); }
        ScaleDetailPut(t, " failed");
        break;
    case LGR_REQUEST_LAYER:
        ScaleDetailPut(t, "request "); ScaleDetailLong(t, v[0]);
        ScaleDetailPut(t, " names no layer or a layer without Layer_Info"); break;
    case LGR_FOREIGN_STACK:
        ScaleDetailPut(t, "request "); ScaleDetailLong(t, v[0]);
        ScaleDetailPut(t, " names a layer outside the screen's layer stacks"); break;
    case LGR_LAYER_COUNT:
        ScaleDetailPut(t, "the stacks hold too many layers ("); ScaleDetailLong(t, v[0]);
        ScaleDetailPut(t, ")"); break;
    case LGR_UPDATING:
        ScaleDetailPut(t, "it is between BeginUpdate and EndUpdate (a refresh is in progress)"); break;
    case LGR_DUPLICATE_REQUEST:
        ScaleDetailPut(t, "it is requested twice (request "); ScaleDetailLong(t, v[0]);
        ScaleDetailPut(t, ")"); break;
    case LGR_REQUEST_FLAGS:
        ScaleDetailPut(t, "request "); ScaleDetailLong(t, v[0]); ScaleDetailPut(t, " has unsupported flags ");
        ScaleDetailHex(t, (ULONG)v[1]); break;
    case LGR_HOOK_MISMATCH:
        ScaleDetailPut(t, "request "); ScaleDetailLong(t, v[0]);
        ScaleDetailPut(t, v[1] ? " resolves a shape hook that is not the layer's own" :
            " resolves a shape hook but the layer has none"); break;
    case LGR_BOUNDS:
        ScaleDetailPut(t, "its proposed size "); ScaleDetailDimensions(t, v[0], v[1]);
        ScaleDetailPut(t, " is outside 1..32767"); break;
    case LGR_SHAPE_HOOK:
        ScaleDetailPut(t, "its client shape hook would have to run for its new ");
        ScaleDetailDimensions(t, v[0], v[1]);
        ScaleDetailPut(t, v[2] ? " replaced shape" : " size"); break;
    case LGR_STALE_REQUEST:
        ScaleDetailPut(t, "request "); ScaleDetailLong(t, v[0]);
        ScaleDetailPut(t, " names a layer in no pinned stack"); break;
    case LGR_PARENT:
        ScaleDetailPut(t, v[0] ? "its parent chain is a cycle" : "its parent layer is outside the batch"); break;
    case LGR_FRONT: ScaleDetailPut(t, "a layer in front of it is outside the batch"); break;
    case LGR_STACK_BITMAP:
        ScaleDetailPut(t, v[0] < 0 ? "its stack root is outside the batch" :
            "its stack has no root layer, RastPort, bitmap or pen colour"); break;
    case LGR_FOREIGN_BITMAP:
        ScaleDetailPut(t, v[0] ? "its RastPort draws into another bitmap than its stack's" :
            "it has no RastPort"); break;
    case LGR_PIXELS: ScaleDetailLease(t, r); break;
    case LGR_SUPER_BITMAP: ScaleDetailPut(t, "it is a SuperBitMap layer without a SuperBitMap"); break;
    case LGR_SUPER_ALIAS: ScaleDetailPut(t, "its SuperBitMap is a stack bitmap"); break;
    case LGR_STATE:
        ScaleDetailPut(t, "the batch is in state "); ScaleDetailLong(t, v[0]); break;
    case LGR_NOT_MEMBER: ScaleDetailPut(t, "it is not in the prepared batch"); break;
    case LGR_ROOT_CANVAS: ScaleDetailPut(t, "a stack root has no client canvas"); break;
    case LGR_SOURCE_SIZE:
        ScaleDetailPut(t, "the source has stride "); ScaleDetailLong(t, v[0]);
        ScaleDetailPut(t, " and "); ScaleDetailLong(t, v[1]); ScaleDetailPut(t, " rows; stride ");
        ScaleDetailLong(t, v[2]); ScaleDetailPut(t, " and "); ScaleDetailLong(t, v[3]);
        ScaleDetailPut(t, " rows are needed"); break;
    case LGR_RECTANGLE:
        ScaleDetailPut(t, "the rectangle "); ScaleDetailLong(t, v[0]); ScaleDetailPut(t, ",");
        ScaleDetailLong(t, v[1]); ScaleDetailPut(t, "-"); ScaleDetailLong(t, v[2]);
        ScaleDetailPut(t, ","); ScaleDetailLong(t, v[3]); ScaleDetailPut(t, " is outside it"); break;
    case LGR_SOURCE_ALIAS: ScaleDetailPut(t, "the source bitmap aliases a stack bitmap or SuperBitMap"); break;
    case LGR_SUPER_TARGET: ScaleDetailPut(t, "a SuperBitMap layer cannot take a bounded native paint"); break;
    case LGR_CURSOR_OWNER: ScaleDetailPut(t, "the cursor bitmap is not the physical screen bitmap"); break;
    case LGR_CURSOR_LEASE: ScaleDetailPut(t, "the screen lease cannot take a proposed pointer"); break;
    case LGR_SCREEN_OFFSET:
        ScaleDetailPut(t, "a visible rectangle at "); ScaleDetailLong(t, v[0]); ScaleDetailPut(t, ",");
        ScaleDetailLong(t, v[1]); ScaleDetailPut(t, " is left of or above the bitmap"); break;
    default: ScaleDetailPut(t, "Layers stated no cause"); break;
    }
}

static inline void ScaleDetailDecorator(struct ScaleDetailText *t,
    const struct ScreenScaleResult *r)
{
    const LONG *v = r->ssr_DetailValue;
    ScaleDetailPut(t, ScaleDecorMethodWork(r->ssr_DetailMethod));
    ScaleDetailPut(t, " was refused by the window decorator (");
    ScaleDetailPut(t, ScaleDecorMethodName(r->ssr_DetailMethod));
    ScaleDetailPut(t, "): ");
    switch (r->ssr_DecorRefusal)
    {
    case WDP_REFUSE_UNSUPPORTED:
        ScaleDetailPut(t, "the decorator class does not implement this method");
        break;
    case WDP_REFUSE_ARGUMENTS:
        ScaleDetailPut(t, "the request is invalid (scale "); ScaleDetailLong(t, v[0]);
        ScaleDetailPut(t, "%, "); ScaleDetailDimensions(t, v[1], v[2]); ScaleDetailPut(t, ")");
        break;
    case WDP_REFUSE_STATE:
        ScaleDetailPut(t, "the window decoration buffer is missing or in the wrong state");
        break;
    case WDP_REFUSE_NO_MEMORY:
        ScaleDetailPut(t, "allocation of "); ScaleDetailLong(t, v[0]); ScaleDetailPut(t, " bytes failed");
        break;
    case WDP_REFUSE_CONFIG:
        ScaleDetailPut(t, "theme value ");
        ScaleDetailName(t, r->ssr_DetailName, sizeof(r->ssr_DetailName));
        ScaleDetailPut(t, " = "); ScaleDetailLong(t, v[0]);
        ScaleDetailPut(t, " is out of range at "); ScaleDetailLong(t, v[1]); ScaleDetailPut(t, "%");
        break;
    case WDP_REFUSE_IMAGE_GRID:
        ScaleDetailPut(t, "theme image ");
        ScaleDetailName(t, r->ssr_DetailName, sizeof(r->ssr_DetailName));
        ScaleDetailPut(t, " ("); ScaleDetailDimensions(t, v[0], v[1]);
        ScaleDetailPut(t, ") does not divide into its "); ScaleDetailDimensions(t, v[2] / 100, v[2] % 100);
        ScaleDetailPut(t, " states");
        break;
    case WDP_REFUSE_IMAGE_EXTENT:
        ScaleDetailPut(t, "theme image ");
        ScaleDetailName(t, r->ssr_DetailName, sizeof(r->ssr_DetailName));
        ScaleDetailPut(t, " cannot be prepared at "); ScaleDetailDimensions(t, v[0], v[1]);
        ScaleDetailPut(t, " pixels per state");
        break;
    case WDP_REFUSE_ELEMENT:
        ScaleDetailPut(t, "a theme element of image ");
        ScaleDetailName(t, r->ssr_DetailName, sizeof(r->ssr_DetailName));
        ScaleDetailPut(t, " at offset "); ScaleDetailLong(t, v[0]);
        ScaleDetailPut(t, " size "); ScaleDetailLong(t, v[1]);
        ScaleDetailPut(t, " exceeds its "); ScaleDetailLong(t, v[2]); ScaleDetailPut(t, " pixel extent");
        break;
    case WDP_REFUSE_FONT:
        ScaleDetailPut(t, "font height "); ScaleDetailLong(t, v[0]);
        ScaleDetailPut(t, " cannot be matched to theme bar height "); ScaleDetailLong(t, v[1]);
        break;
    case WDP_REFUSE_SUPERCLASS:
        ScaleDetailPut(t, "its superclass refused ");
        ScaleDetailPut(t, ScaleDecorMethodName((ULONG)v[0]));
        break;
    default:
        ScaleDetailPut(t, "the decorator stated no cause");
        break;
    }
}

/* Writes the detail clause, or an empty string for SSR_DETAIL_NONE. Returns
 * TRUE when a clause was written. Size must be at least 1. */
static inline BOOL ScaleDetailFormat(const struct ScreenScaleResult *r, char *text, ULONG size)
{
    static const char *const sides[] = {"left", "top", "right", "bottom"};
    struct ScaleDetailText t = {text, text + size};
    const LONG *v = r->ssr_DetailValue;
    *text = 0;
    switch (r->ssr_Detail)
    {
    case SSR_DETAIL_NONE: return FALSE;
    case SSR_DETAIL_FRAME_NO_GENERATION:
        ScaleDetailPut(&t, "the window has no committed frame generation");
        break;
    case SSR_DETAIL_FRAME_CONTEXT:
        ScaleDetailPut(&t, "the render context has no draw information for the window's screen");
        break;
    case SSR_DETAIL_FRAME_CONTEXT_BORDERS:
        ScaleDetailPut(&t, v[0] ? "the render context has no logical frame border metrics" :
            "the render context has no physical frame border metrics");
        break;
    case SSR_DETAIL_FRAME_NO_FONT:
        ScaleDetailPut(&t, "the render context has no frame font");
        break;
    case SSR_DETAIL_FRAME_BORDER_RANGE:
        ScaleDetailPut(&t, "the ");
        ScaleDetailPut(&t, v[0] >= 0 && v[0] < 4 ? sides[v[0]] : "unknown");
        ScaleDetailPut(&t, " border would be "); ScaleDetailLong(&t, v[1]);
        ScaleDetailPut(&t, " pixels; frame borders must be 0..127");
        break;
    case SSR_DETAIL_FRAME_SIZE_IMAGE:
        ScaleDetailPut(&t, "the size gadget image query returned status "); ScaleDetailLong(&t, v[0]);
        ScaleDetailPut(&t, " with "); ScaleDetailDimensions(&t, v[1], v[2]);
        ScaleDetailPut(&t, " pixels; it must be ready and 1..127 pixels");
        break;
    case SSR_DETAIL_FRAME_LAYOUT_SIZE:
        ScaleDetailPut(&t, "the proposed frame size "); ScaleDetailDimensions(&t, v[0], v[1]);
        ScaleDetailPut(&t, " is outside 1..32767 or the frame was already prepared");
        break;
    case SSR_DETAIL_FRAME_USER_BUFFER:
        ScaleDetailPut(&t, "the window decorator's buffer size "); ScaleDetailLong(&t, v[0]);
        ScaleDetailPut(&t, " is invalid");
        break;
    case SSR_DETAIL_FRAME_UNTYPED_GADGET:
        ScaleDetailPut(&t, "system gadget slot "); ScaleDetailLong(&t, v[0]);
        ScaleDetailPut(&t, " has an image but no system image type");
        break;
    case SSR_DETAIL_FRAME_IMAGE_QUERY:
        ScaleDetailPut(&t, "the window decorator (WDM_GETSCALEDEFSIZE_SYSIMAGE) returned status ");
        ScaleDetailLong(&t, v[0]); ScaleDetailPut(&t, " for the ");
        ScaleDetailName(&t, r->ssr_DetailName, sizeof(r->ssr_DetailName));
        ScaleDetailPut(&t, " image with "); ScaleDetailDimensions(&t, v[1], v[2]);
        ScaleDetailPut(&t, " pixels");
        break;
    case SSR_DETAIL_FRAME_IMAGE_EXTENT:
        ScaleDetailPut(&t, "the ");
        ScaleDetailName(&t, r->ssr_DetailName, sizeof(r->ssr_DetailName));
        ScaleDetailPut(&t, " image extent "); ScaleDetailDimensions(&t, v[0], v[1]);
        ScaleDetailPut(&t, " (preferred height "); ScaleDetailLong(&t, v[2]);
        ScaleDetailPut(&t, ") is outside 1..32767");
        break;
    case SSR_DETAIL_FRAME_IMAGE_OBJECT:
        ScaleDetailPut(&t, "the ");
        ScaleDetailName(&t, r->ssr_DetailName, sizeof(r->ssr_DetailName));
        ScaleDetailPut(&t, " system image object ("); ScaleDetailDimensions(&t, v[0], v[1]);
        ScaleDetailPut(&t, ") could not be created");
        break;
    case SSR_DETAIL_FRAME_METRICS:
        ScaleDetailPut(&t, "the window is missing from the measured frame set");
        break;
    case SSR_DETAIL_DECORATOR:
        ScaleDetailDecorator(&t, r);
        break;
    case SSR_DETAIL_LAYER:
        ScaleDetailLayer(&t, r);
        break;
    case SSR_DETAIL_LAYER_REQUEST:
        ScaleDetailLayerName(&t, r);
        ScaleDetailPut(&t, "the proposed rectangle ");
        ScaleDetailDimensionsAt(&t, r->ssr_LayerValue[2], r->ssr_LayerValue[3],
            r->ssr_LayerValue[0], r->ssr_LayerValue[1]);
        ScaleDetailPut(&t, " is outside the 16-bit layer range");
        break;
    case SSR_DETAIL_COMPOSITION:
        ScaleDetailPut(&t, "the legacy window composition could not project its shape to ");
        ScaleDetailDimensions(&t, v[0], v[1]);
        ScaleDetailPut(&t, " physical pixels");
        break;
    default:
        ScaleDetailPut(&t, "refusal detail "); ScaleDetailLong(&t, (LONG)r->ssr_Detail);
        break;
    }
    return TRUE;
}
#endif
