/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Licensed under the AROS Public License (APL) Version 1.1.
 *
 * Screen scale is one prepared OS transaction. No operation in the install
 * section can allocate, call a client method, mutate a Region, or resize a
 * live layer. Old fields, backing and raw raster bytes remain owned until the
 * irreversible postcommit drawing step. Application layout follows commit.
 */
#include "intuition_intern.h"
#include "windowscale.h"
#include "scaleresult.h"
#include <intuition/scaledetail.h>
#include "windowfit.h"
#include "windowframe.h"
#include "systemrequestscale.h"
#include "activegadgetscale.h"
#include "windowcomposition.h"
#include "prepareddecor.h"
#include "preparednotifications.h"
#include "pointerscale.h"
#include "screenrendercontext.h"
#include "rendercontextbinding.h"
#include "inputhandler_actions.h"
#include "inputhandler_support.h"
#include "intuition_customize.h"
#include <exec/memory.h>
#include <graphics/layertransaction.h>
#include <intuition/windecorclass.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/layers.h>
#include <string.h>

struct WindowScaleShapes
{
    struct WindowScaleShapes *previous;
    struct Region *outer, *client;
};
struct ScaleWindow
{
    struct Window *window;
    struct WindowFitGeometry desired;
    LONG oldleft, oldtop, oldwidth, oldheight;
    WORD mouse_x, mouse_y, gzzmouse_x, gzzmouse_y;
    struct WindowScaleShapes *shapes;
    struct WindowCompositionScale *composition;
    struct PreparedWindowFrame *frame;
    struct PreparedSystemRequesterScale *requester;
    struct PreparedActiveGadgetScale active;
    struct Layer *proxy; /* requested legacy presentation layer, if composed */
    LONG borderleft, bordertop, borderright, borderbottom;
    BOOL linked;
};

struct ScaleFontState
{
    struct TextFont *font;
    WORD width, height, baseline;
};
struct ScaleInstall
{
    struct IntScreen *screen;
    struct TextFont *oldfont, *font, *oldbase, *base;
    struct TTextAttr oldattr, attr;
    struct ScaleFontState oldraster, oldbar;
    struct LogicalScreenInfo *oldlogical, *logical;
    struct WindowFitTransaction *fit;
    struct LayerGeometryBatch *layers;
    struct PreparedScreenScaleDecoration *decor;
    struct PreparedScreenRenderContext *render;
    struct PreparedScreenPointerScale *pointer;
    struct ScaleWindow *windows;
    ULONG count;
    UWORD oldscale, scale;
    BOOL oldsysfont, layoutonly;
};

static void capture_font(struct RastPort *rp, struct ScaleFontState *state)
{
    state->font = rp->Font; state->width = rp->TxWidth;
    state->height = rp->TxHeight; state->baseline = rp->TxBaseline;
}

static void restore_font(struct RastPort *rp, const struct ScaleFontState *state)
{
    rp->Font = state->font; rp->TxWidth = state->width;
    rp->TxHeight = state->height; rp->TxBaseline = state->baseline;
}

/* This is reversible until postcommit drawing begins. Layer rollback works
 * after a test cut inside its install, restoring every old field/raster even
 * if only a prefix of the new state was written. No allocator or callbacks. */
static void install_scale(struct ScaleInstall *prepared, BOOL rollback,
    struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct LayersBase *LayersBase = GetPrivIBase(IntuitionBase)->LayersBase;
    struct IntScreen *is = prepared->screen;
    struct Screen *screen = &is->Screen;
    ULONG i;
    if (!prepared->layoutonly)
    {
        is->DInfo.dri_Font = rollback ? prepared->oldfont : prepared->font;
        is->ScaleBaseFont = rollback ? prepared->oldbase : prepared->base;
        is->Scale = rollback ? prepared->oldscale : prepared->scale;
        is->SysFont = rollback ? prepared->oldsysfont :
            (prepared->scale == SA_SCALE_MIN ? is->ScaleSysFont : FALSE);
        is->textattr = rollback ? prepared->oldattr : prepared->attr;
        if (rollback)
        {
            restore_font(&screen->RastPort, &prepared->oldraster);
            if (screen->BarLayer) restore_font(screen->BarLayer->rp, &prepared->oldbar);
        }
        else
        {
            SetFont(&screen->RastPort, prepared->font);
            if (screen->BarLayer) SetFont(screen->BarLayer->rp, prepared->font);
        }
        InstallScreenScaleDecoration(prepared->decor, rollback);
        InstallScreenRenderContext(prepared->render, rollback);
    }
    for (i = 0; i < prepared->count; ++i)
    {
        if (prepared->windows[i].frame)
            InstallWindowFrame(prepared->windows[i].frame, rollback);
        InstallSystemRequesterScale(prepared->windows[i].requester, rollback);
    }
    /* Fit computes GZZ extents from the installed proposed borders. */
    WindowFitInstall(prepared->fit, rollback);
    for (i = 0; i < prepared->count; ++i)
        InstallActiveGadgetScale(&prepared->windows[i].active, rollback);
    if (rollback)
    {
        if (!prepared->layoutonly) RestoreLogicalScreenInfo(is, prepared->oldlogical);
        RollbackLayerGeometryBatch(prepared->layers);
    }
    else
    {
        if (!prepared->layoutonly) CommitLogicalScreenInfo(is, prepared->logical);
        CommitLayerGeometryBatch(prepared->layers);
    }
    if (!prepared->layoutonly) InstallScreenPointerScale(prepared->pointer, rollback);
    for (i = 0; i < prepared->count; ++i)
    {
        struct ScaleWindow *record = &prepared->windows[i];
        struct Window *window = record->window;
        if (prepared->layoutonly && !record->frame &&
            record->desired.left == record->oldleft && record->desired.top == record->oldtop &&
            record->desired.width == record->oldwidth && record->desired.height == record->oldheight)
            continue;
        if (record->composition) InstallWindowCompositionScale(record->composition, rollback);
        else if (rollback)
        {
            window->MouseX = record->mouse_x; window->MouseY = record->mouse_y;
            window->GZZMouseX = record->gzzmouse_x; window->GZZMouseY = record->gzzmouse_y;
        }
        else
        {
            window->MouseX = screen->MouseX - window->LeftEdge;
            window->MouseY = screen->MouseY - window->TopEdge;
            window->GZZMouseX = window->MouseX - window->BorderLeft;
            window->GZZMouseY = window->MouseY - window->BorderTop;
        }
        if (record->shapes)
        {
            if (rollback)
            {
                if (IW(window)->ScaleShapes == record->shapes)
                    IW(window)->ScaleShapes = record->shapes->previous;
                record->linked = FALSE;
            }
            else
            {
                record->shapes->previous = IW(window)->ScaleShapes;
                IW(window)->ScaleShapes = record->shapes;
                record->linked = TRUE;
            }
        }
    }
}

static void free_shape_chain(struct WindowScaleShapes *shapes,
    struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    while (shapes)
    {
        struct WindowScaleShapes *previous = shapes->previous;
        if (shapes->outer) DisposeRegion(shapes->outer);
        if (shapes->client) DisposeRegion(shapes->client);
        FreeVec(shapes);
        shapes = previous;
    }
}

void FreeWindowScaleShapes(struct Window *window, struct IntuitionBase *IntuitionBase)
{
    struct WindowScaleShapes *shapes = IW(window)->ScaleShapes;
    IW(window)->ScaleShapes = NULL;
    free_shape_chain(shapes, IntuitionBase);
}

static BOOL bounds(struct Rectangle *result, LONG x, LONG y, LONG width, LONG height)
{
    if (width <= 0 || height <= 0 || width > 32767 || height > 32767 ||
        x < -32768 || y < -32768 || x > 32767 - width + 1 || y > 32767 - height + 1)
        return FALSE;
    result->MinX = x; result->MinY = y;
    result->MaxX = x + width - 1; result->MaxY = y + height - 1;
    return TRUE;
}

static BOOL window_shape(struct ScaleWindow *record, LONG width, LONG height,
    struct Region **result, struct ScreenScaleResult *diagnostic,
    struct IntuitionBase *IntuitionBase)
{
    struct wdpPrepareScaleShape message;
    struct Window *window = record->window;
    struct IntDrawInfo *dri = PreparedWindowFrameDrawInfo(record->frame);
    (void)IntuitionBase;
    if (!dri || !dri->dri_Font)
    {
        ScaleResultDetail(diagnostic, SSR_DETAIL_FRAME_NO_FONT, 0, 0, 0, NULL);
        return FALSE;
    }
    message.MethodID = WDM_PREPARESCALESHAPE;
    message.wdp_Window = window;
    message.wdp_Width = width; message.wdp_Height = height;
    message.wdp_BorderTop = record->bordertop;
    message.wdp_Font = dri->dri_Font;
    message.wdp_UserBuffer = PreparedWindowFrameUserBuffer(record->frame);
    message.wdp_Result = result;
    *result = NULL;
    if (DoMethodA(IS(window->WScreen)->WinDecorObj, (Msg)&message)) return TRUE;
    {
        /* The shape method carries no typed refusal record. */
        const LONG values[3] = {0, width, height};
        ScaleResultDecorator(diagnostic, WDM_PREPARESCALESHAPE, WDP_REFUSE_NONE, values, NULL, 0);
    }
    return FALSE;
}

/* A proposed layer rectangle the WORD-based Layers geometry cannot hold. */
static BOOL request_refused(struct ScreenScaleResult *result, ULONG role,
    struct Layer *layer, LONG x, LONG y, LONG width, LONG height)
{
    const LONG values[4] = {x, y, width, height};
    ScaleResultLayer(result, SSR_DETAIL_LAYER_REQUEST, role, (IPTR)layer,
        LGR_NONE, 0, 0, 0, 0, values, NULL, 0);
    return FALSE;
}

static BOOL request_bounds(struct ScreenScaleResult *result, ULONG role,
    struct LayerGeometryRequest *request, LONG x, LONG y, LONG width, LONG height)
{
    return bounds(&request->bounds, x, y, width, height) ||
        request_refused(result, role, request->layer, x, y, width, height);
}

/* Name the refusing layer by the role Intuition holds it in. Layers stay
 * pinned by IBase and the screen LayerInfo lock while this runs. */
static ULONG layer_role(struct Screen *screen, struct ScaleWindow *windows, ULONG count,
    const struct LayerGeometryRefusal *refusal, struct Window **owner)
{
    struct Layer *layer = refusal->lgr_Layer;
    BOOL composed = (refusal->lgr_Flags & LGRF_PRIVATE) != 0;
    ULONG i;
    *owner = NULL;
    if (!layer) return SSR_LAYER_NONE;
    if (layer == screen->BarLayer) return SSR_LAYER_BAR;
    for (i = 0; i < count; ++i)
    {
        struct Window *window = windows[i].window;
        struct Requester *requester;
        if (layer == windows[i].proxy) { *owner = window; return SSR_LAYER_PRESENTATION; }
        if (layer == WLAYER(window) || (BLAYER(window) && layer == BLAYER(window)))
        {
            *owner = window;
            if (composed) return SSR_LAYER_CANVAS;
            if (!BLAYER(window)) return SSR_LAYER_WINDOW;
            return layer == BLAYER(window) ? SSR_LAYER_WINDOW_BORDER : SSR_LAYER_WINDOW_CLIENT;
        }
        for (requester = window->FirstRequest; requester; requester = requester->OlderRequest)
            if (layer == requester->ReqLayer) { *owner = window; return SSR_LAYER_REQUESTER; }
    }
    if (!composed) return (refusal->lgr_Flags & LGRF_ROOT) ? SSR_LAYER_ROOT : SSR_LAYER_OTHER;
    if (refusal->lgr_Flags & LGRF_ROOT)
        for (i = 0; i < count; ++i)
            if (WLAYER(windows[i].window) &&
                WLAYER(windows[i].window)->LayerInfo == layer->LayerInfo)
            { *owner = windows[i].window; return SSR_LAYER_CANVAS_ROOT; }
    return SSR_LAYER_PRIVATE;
}

/* Copy a recorded Layers refusal into the receipt at this stage. A failure
 * that Layers did not record (an Intuition allocation) keeps the reason. */
static void layer_refused(struct ScreenScaleResult *result, ULONG stage,
    struct Window *current, const struct LayerGeometryRefusal *refusal,
    struct Screen *screen, struct ScaleWindow *windows, ULONG count)
{
    struct Window *owner;
    ULONG role;
    if (!result || refusal->lgr_Check == LGR_NONE) return;
    role = layer_role(screen, windows, count, refusal, &owner);
    ScaleResultSetStage(result, stage, owner ? owner : current);
    ScaleResultLayer(result, SSR_DETAIL_LAYER, role, (IPTR)refusal->lgr_Layer,
        refusal->lgr_Check, refusal->lgr_Flags, refusal->lgr_Pixels, refusal->lgr_Lease,
        refusal->lgr_Driver, refusal->lgr_Value, refusal->lgr_Class, sizeof(refusal->lgr_Class));
}

/* Paint an owned native requester into a private physical-format bitmap.
 * Only its proposed client rectangle reaches the detached layer canvas;
 * native border backing and occluded content outside it remain owned by the
 * Layers transaction. No client callback or live raster is used here. */
static BOOL prepare_requester_pixels(struct ScaleWindow *record,
    struct LayerGeometryBatch *layers, struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct LayersBase *LayersBase = GetPrivIBase(IntuitionBase)->LayersBase;
    struct BitMap *bitmap;
    struct RastPort port;
    struct Rectangle client;
    BOOL result;
    if (!record->requester) return TRUE;
    if ((record->window->Flags & WFLG_GIMMEZEROZERO) ||
        !SystemRequesterScaleBounds(record->requester, &client)) return FALSE;
    bitmap = AllocBitMap(record->desired.width, record->desired.height,
        GetBitMapAttr(record->window->WScreen->RastPort.BitMap, BMA_DEPTH),
        BMF_CLEAR, record->window->WScreen->RastPort.BitMap);
    if (!bitmap) return FALSE;
    InitRastPort(&port);
    port.BitMap = bitmap;
    result = PaintSystemRequesterScale(record->requester, &port, IntuitionBase) &&
        WriteLayerGeometryBitmap(layers, WLAYER(record->window), bitmap, &client);
    FreeBitMap(bitmap);
    return result;
}

static BOOL prepare_window_requests(struct ScaleWindow *record, ULONG scale,
    struct LayerGeometryRequest *requests, ULONG *used, struct ScreenScaleResult *result,
    struct IntuitionBase *IntuitionBase)
{
    struct Window *window = record->window;
    const struct WindowFitGeometry *g = &record->desired;
    struct Requester *requester;
    struct Layer *outer = BLAYER(window) ? BLAYER(window) : WLAYER(window);
    BOOL size_changed = g->width != window->Width || g->height != window->Height;
    /* An aware frame can change its shape/font while outer geometry stays. */
    BOOL shape_changed = size_changed || (IW(window)->ScaleAware && record->frame);
    BOOL default_hook = !IW(window)->CustomShape && !(window->Flags & WFLG_BORDERLESS) &&
        !IS_GZZWINDOW(window);
    struct Region *outershape = outer->shaperegion;
    struct LayerGeometryRequest *request;
    /* Layout-only records without a frame keep their exact retained shape.
     * The Layers batch still includes them in scene/exposure computation. */
    if (!record->frame && !size_changed && g->left == window->LeftEdge &&
        g->top == window->TopEdge) return TRUE;
    if (shape_changed && default_hook)
    {
        record->shapes = AllocVec(sizeof(*record->shapes), MEMF_PUBLIC | MEMF_CLEAR);
        if (!record->shapes) { ScaleResultReason(result, SSR_REASON_NO_MEMORY); return FALSE; }
        if (!window_shape(record, g->width, g->height,
            &record->shapes->outer, result, IntuitionBase)) return FALSE;
        outershape = record->shapes->outer;
        if (BLAYER(window) && !window_shape(record,
            g->width - record->borderleft - record->borderright,
            g->height - record->bordertop - record->borderbottom,
            &record->shapes->client, result, IntuitionBase)) return FALSE;
    }
    if (BLAYER(window))
    {
        request = &requests[(*used)++]; request->layer = BLAYER(window);
        if (!request_bounds(result, SSR_LAYER_WINDOW_BORDER, request,
            g->left, g->top, g->width, g->height)) return FALSE;
        if (default_hook)
        {
            request->flags = LGT_RESOLVED_SHAPE;
            request->resolved_hook = &IW(window)->DefaultWindowShapeHook;
            if (shape_changed) { request->flags |= LGT_REPLACE_SHAPE; request->shape = outershape; }
        }
    }
    request = &requests[(*used)++]; request->layer = WLAYER(window);
    if (!request_bounds(result, BLAYER(window) ? SSR_LAYER_WINDOW_CLIENT : SSR_LAYER_WINDOW, request,
        g->left + (BLAYER(window) ? record->borderleft : 0),
        g->top + (BLAYER(window) ? record->bordertop : 0),
        BLAYER(window) ? g->width - record->borderleft - record->borderright : g->width,
        BLAYER(window) ? g->height - record->bordertop - record->borderbottom : g->height)) return FALSE;
    if (default_hook)
    {
        request->flags = LGT_RESOLVED_SHAPE;
        request->resolved_hook = &IW(window)->DefaultWindowShapeHook;
        if (shape_changed)
        {
            request->flags |= LGT_REPLACE_SHAPE;
            request->shape = BLAYER(window) ? record->shapes->client : outershape;
        }
    }
    for (requester = window->FirstRequest; requester; requester = requester->OlderRequest)
        if (requester->ReqLayer)
        {
            LONG left = g->left + requester->LeftEdge, top = g->top + requester->TopEdge;
            LONG right = left + requester->Width - 1, bottom = top + requester->Height - 1;
            LONG maxright = g->left + g->width - 1, maxbottom = g->top + g->height - 1;
            if (left > maxright) left = maxright;
            if (top > maxbottom) top = maxbottom;
            if (right > maxright) right = maxright;
            if (bottom > maxbottom) bottom = maxbottom;
            request = &requests[(*used)++]; request->layer = requester->ReqLayer;
            if (!request_bounds(result, SSR_LAYER_REQUESTER, request,
                left, top, right - left + 1, bottom - top + 1)) return FALSE;
        }
    if (IW(window)->Composition)
    {
        record->composition = PrepareWindowCompositionScale(window, scale, g,
            outershape, &requests[(*used)++], IntuitionBase);
        if (!record->composition)
        {
            ScaleResultDetail(result, SSR_DETAIL_COMPOSITION,
                g->physical_width, g->physical_height, 0, NULL);
            return FALSE;
        }
        record->proxy = requests[*used - 1].layer;
    }
    return TRUE;
}

/* One kernel log line per detailed refusal; the clause is the same text
 * C:DisplayScale and ScreenMode print from the receipt. */
static void log_scale_refusal(const struct ScreenScaleResult *result)
{
    char detail[320], title[sizeof(result->ssr_WindowTitle) + 2];
    struct ScaleDetailText caption = {title, title + sizeof(title)};
    if (!result || result->ssr_Status != SSR_FAILED ||
        result->ssr_Detail == SSR_DETAIL_NONE) return;
    ScaleDetailFormat(result, detail, sizeof(detail));
    ScaleDetailName(&caption, result->ssr_WindowTitle, sizeof(result->ssr_WindowTitle));
    bug("[SCALE] %lu%% refused at stage %lu, window %p %s: %s\n",
        (unsigned long)result->ssr_RequestedScale, (unsigned long)result->ssr_Stage,
        (void *)result->ssr_Window, title, detail);
}

BOOL ApplyPreparedScreenScale(struct IntScreen *is, struct TextFont *font,
    struct TextFont *basefont, ULONG scale, const struct WindowLayoutFit *layout,
    struct ScreenScaleResult *result, struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct LayersBase *LayersBase = GetPrivIBase(IntuitionBase)->LayersBase;
    struct Screen *screen = &is->Screen, *found;
    const struct ScreenRenderContext *context = NULL;
    struct Window *window;
    struct WindowFitTransaction *fit = NULL;
    struct WindowFitFrameMetrics *fitframes = NULL;
    struct LayerGeometryBatch *layers = NULL;
    struct LogicalScreenInfo *logical = NULL, *oldlogical = is->LogicalInfo;
    struct PreparedScreenScaleDecoration *decor = NULL;
    struct PreparedScreenRenderContext *render = NULL;
    struct PreparedScreenPointerScale *pointer = NULL;
    struct PreparedNotifications *notifications = NULL;
    struct PreparedNotificationSpec *specs = NULL;
    struct ScaleWindow *windows = NULL;
    struct LayerGeometryRequest *requests = NULL;
    struct TextFont *oldfont = is->DInfo.dri_Font, *oldbase = is->ScaleBaseFont;
    struct TTextAttr oldattr = is->textattr, newattr;
    UWORD oldscale = is->Scale;
    BOOL oldsysfont = is->SysFont, committed = FALSE;
    struct ScaleInstall install;
    struct LayerGeometryRefusal refusal;
    ULONG lock = LockIBase(0), count = 0, capacity = 1, used = 0, i;
    ScaleResultReset(result, scale, oldscale);
    memset(&refusal, 0, sizeof(refusal));
    for (found = IntuitionBase->FirstScreen; found && found != screen; found = found->NextScreen) ;
    if (!found)
    {
        ScaleResultReason(result, SSR_REASON_INVALID_REQUEST);
        UnlockIBase(lock); return FALSE;
    }
    if (!layout && is->Scale == scale && is->ScaleBaseFont == basefont)
    {
        ScaleResultFinish(result, SSR_UNCHANGED, is->Scale);
        UnlockIBase(lock); return FALSE;
    }
    LOCK_REFRESH(screen);
    if (layout)
    {
        /* Only the synchronous Window action supplies a layout request. It
         * borrows existing fonts/context and changes no screen ownership. */
        for (window = screen->FirstWindow; window && window != layout->window; window = window->NextWindow) ;
        if (!window || !IW(window)->ScaleAware || !IW(window)->ScaleGeometryEnabled ||
            !BoundScreenRenderContext(layout->context, screen) || scale != is->Scale ||
            font != oldfont || basefont != oldbase)
        { ScaleResultReason(result, SSR_REASON_INVALID_CONTEXT); goto done; }
        context = layout->context;
    }
    for (window = screen->FirstWindow; window; window = window->NextWindow) count++;
    if (!layout)
    {
        ScaleResultSetStage(result, SSR_STAGE_LOGICAL_SCREEN, NULL);
        logical = PrepareLogicalScreenInfo(is, basefont, IntuitionBase);
        if (!logical) goto done;
        ScaleResultSetStage(result, SSR_STAGE_DECORATION, NULL);
        decor = PrepareScreenScaleDecoration(screen, basefont, font, scale, result, IntuitionBase);
        if (!decor) goto done;
        ScaleResultSetStage(result, SSR_STAGE_RENDER_CONTEXT, NULL);
        render = PrepareScreenRenderContext(is, basefont, font, scale,
            PreparedScreenScaleDecorationMetrics(decor), IntuitionBase);
        if (!render || !BindScreenScaleDecoration(decor, PreparedScreenRenderContextView(render))) goto done;
        context = PreparedScreenRenderContextView(render);
    }
    ScaleResultSetStage(result, SSR_STAGE_FRAME, NULL);
    if (count > 0xffffffffUL / sizeof(*windows) ||
        count > 0xffffffffUL / sizeof(*specs) ||
        count > 0xffffffffUL / sizeof(*fitframes)) goto done;
    if (count)
    {
        windows = AllocVec(count * sizeof(*windows), MEMF_PUBLIC | MEMF_CLEAR);
        specs = AllocVec(count * sizeof(*specs), MEMF_PUBLIC | MEMF_CLEAR);
        fitframes = AllocVec(count * sizeof(*fitframes), MEMF_PUBLIC | MEMF_CLEAR);
        if (!windows || !specs || !fitframes)
        { ScaleResultReason(result, SSR_REASON_NO_MEMORY); goto done; }
        i = 0;
        for (window = screen->FirstWindow; window; window = window->NextWindow, ++i)
        {
            struct ScaleWindow *record = &windows[i];
            record->window = window;
            if (layout && window != layout->window)
            {
                record->borderleft = window->BorderLeft; record->bordertop = window->BorderTop;
                record->borderright = window->BorderRight; record->borderbottom = window->BorderBottom;
            }
            else
            {
                ScaleResultSetStage(result, SSR_STAGE_FRAME, window);
                record->frame = PrepareWindowFrame(window, context, result, IntuitionBase);
                if (!record->frame || !WindowFrameBorders(record->frame,
                    &record->borderleft, &record->bordertop,
                    &record->borderright, &record->borderbottom)) goto done;
            }
            fitframes[i] = (struct WindowFitFrameMetrics){window,
                record->borderleft, record->bordertop,
                record->borderright, record->borderbottom};
        }
    }
    ScaleResultSetStage(result, SSR_STAGE_GEOMETRY, NULL);
    if (!WindowFitPreflight(screen, scale, fitframes, count, layout, &fit, result, IntuitionBase) ||
        WindowFitCount(fit) != count) goto done;
    for (i = 0; i < count; ++i)
    {
        struct Requester *requester;
        struct Window *planned;
        ScaleResultSetStage(result, SSR_STAGE_GEOMETRY, windows[i].window);
        if (!WindowFitDesired(fit, i, &planned, &windows[i].desired) ||
            planned != windows[i].window) goto done;
        window = windows[i].window;
        if (windows[i].frame)
        {
            ScaleResultSetStage(result, SSR_STAGE_FRAME, window);
            if (!PrepareWindowFrameLayout(windows[i].frame,
                windows[i].desired.width, windows[i].desired.height, result,
                IntuitionBase)) goto done;
            ScaleResultSetStage(result, SSR_STAGE_ACTIVE_GADGET, window);
            if (!PrepareActiveGadgetScale(window,
                (struct DrawInfo *)PreparedWindowFrameDrawInfo(windows[i].frame),
                &windows[i].desired, windows[i].borderleft, windows[i].bordertop,
                windows[i].borderright, windows[i].borderbottom,
                &windows[i].active, IntuitionBase)) goto done;
            ScaleResultSetStage(result, SSR_STAGE_REQUESTER, window);
            if (!PrepareSystemRequesterScale(window, context,
                &windows[i].desired, windows[i].borderleft, windows[i].bordertop,
                windows[i].borderright, windows[i].borderbottom,
                &windows[i].requester, IntuitionBase)) goto done;
        }
        windows[i].oldleft = window->LeftEdge; windows[i].oldtop = window->TopEdge;
        windows[i].oldwidth = window->Width; windows[i].oldheight = window->Height;
        windows[i].mouse_x = window->MouseX; windows[i].mouse_y = window->MouseY;
        windows[i].gzzmouse_x = window->GZZMouseX; windows[i].gzzmouse_y = window->GZZMouseY;
        if (capacity > 0xffffffffUL - 3) goto done;
        capacity += 3; /* client, optional border and proxy */
        for (requester = window->FirstRequest; requester; requester = requester->OlderRequest)
            if (requester->ReqLayer) { if (capacity == 0xffffffffUL) goto done; ++capacity; }
    }
    ScaleResultSetStage(result, SSR_STAGE_LAYER_GEOMETRY, NULL);
    if (capacity > 0xffffffffUL / sizeof(*requests)) goto done;
    requests = AllocVec(capacity * sizeof(*requests), MEMF_PUBLIC | MEMF_CLEAR);
    if (!requests) { ScaleResultReason(result, SSR_REASON_NO_MEMORY); goto done; }
    for (i = 0; i < count; ++i)
    {
        ScaleResultSetStage(result, SSR_STAGE_LAYER_GEOMETRY, windows[i].window);
        if (!prepare_window_requests(&windows[i], scale, requests, &used, result, IntuitionBase)) goto done;
    }
    if (!layout && screen->BarLayer)
    {
        const struct ScreenScaleMetrics *metrics = PreparedScreenScaleDecorationMetrics(decor);
        ScaleResultSetStage(result, SSR_STAGE_LAYER_GEOMETRY, NULL);
        struct LayerGeometryRequest *request = &requests[used++];
        request->layer = screen->BarLayer;
        if (!request_bounds(result, SSR_LAYER_BAR, request,
            screen->BarLayer->bounds.MinX, screen->BarLayer->bounds.MinY,
            screen->BarLayer->Width, screen->BarLayer->Height + metrics->BarHeight - screen->BarHeight)) goto done;
    }
    if (!layout)
    {
        ScaleResultSetStage(result, SSR_STAGE_POINTER, NULL);
        pointer = PrepareScreenPointerScale(screen, scale, IntuitionBase);
        if (!pointer) goto done;
    }
    ScaleResultSetStage(result, SSR_STAGE_LAYER_GEOMETRY, NULL);
    layers = PrepareLayerGeometryBatch(&screen->LayerInfo, requests, used, &refusal);
    if (!layers)
    {
        layer_refused(result, SSR_STAGE_LAYER_GEOMETRY, NULL, &refusal, screen, windows, count);
        goto done;
    }
    if (pointer)
    {
        ScaleResultSetStage(result, SSR_STAGE_CURSOR, NULL);
        struct HIDD_CursorState *proposed = PreparedScreenPointerCursor(pointer);
        struct HIDD_PixelMap *map;
        memset(&refusal, 0, sizeof(refusal));
        map = proposed ? PrepareLayerGeometryCursor(layers,
            screen->RastPort.BitMap, proposed) : NULL;
        if (proposed && !map)
        {
            layer_refused(result, SSR_STAGE_CURSOR, NULL, &refusal, screen, windows, count);
            goto done;
        }
        if (!BindScreenPointerScale(pointer, map)) goto done;
    }
    for (i = 0; i < count; ++i)
    {
        ScaleResultSetStage(result, SSR_STAGE_PIXELS, windows[i].window);
        memset(&refusal, 0, sizeof(refusal));
        if (windows[i].composition &&
            !PrepareWindowCompositionPixels(windows[i].composition, layers, IntuitionBase))
        {
            layer_refused(result, SSR_STAGE_PIXELS, windows[i].window, &refusal, screen, windows, count);
            goto done;
        }
    }
    for (i = 0; i < count; ++i)
    {
        ScaleResultSetStage(result, SSR_STAGE_PIXELS, windows[i].window);
        memset(&refusal, 0, sizeof(refusal));
        if (!prepare_requester_pixels(&windows[i], layers, IntuitionBase))
        {
            layer_refused(result, SSR_STAGE_PIXELS, windows[i].window, &refusal, screen, windows, count);
            goto done;
        }
    }
    for (i = 0; i < count; ++i)
    {
        const struct WindowFitGeometry *g = &windows[i].desired;
        window = windows[i].window;
        specs[i].window = window; specs[i].scale = scale;
        /* Explicit event subscription is independent of raster awareness.
         * Legacy-canvas MUI can invalidate its baseline-font cache too. */
        if (!layout) specs[i].flags |= PNF_SCALE;
        if (g->left != window->LeftEdge || g->top != window->TopEdge ||
            g->width != window->Width || g->height != window->Height) specs[i].flags |= PNF_GEOMETRY;
        if (g->width != window->Width || g->height != window->Height) specs[i].flags |= PNF_NEWSIZE;
        /* Final exposure can affect an aware window without moving it. */
        if (IS_DOCAREREFRESH(window) && LayerGeometryNeedsRefresh(layers, WLAYER(window)))
            specs[i].flags |= PNF_REFRESH;
    }
    ScaleResultSetStage(result, SSR_STAGE_NOTIFICATIONS, NULL);
    notifications = PreparedNotificationsPrepare(specs, count, IntuitionBase);
    if (!notifications) goto done;
    ScaleResultSetStage(result, SSR_STAGE_LAYER_GEOMETRY, NULL);
    memset(&refusal, 0, sizeof(refusal));
    if (!SealLayerGeometryBatch(layers))
    {
        layer_refused(result, SSR_STAGE_LAYER_GEOMETRY, NULL, &refusal, screen, windows, count);
        goto done;
    }
    for (i = 0; i < count; ++i)
        if (windows[i].composition) SealWindowCompositionScale(windows[i].composition);
    newattr = oldattr;
    if (!layout) ScreenFontTextAttr(font, &newattr);
    memset(&install, 0, sizeof(install));
    install.screen = is; install.oldfont = oldfont; install.font = font;
    install.oldbase = oldbase; install.base = basefont;
    install.oldattr = oldattr; install.attr = newattr;
    install.oldlogical = oldlogical; install.logical = logical;
    install.fit = fit; install.layers = layers; install.decor = decor;
    install.pointer = pointer;
    install.render = render;
    install.windows = windows; install.count = count;
    install.layoutonly = layout != NULL;
    install.oldscale = oldscale; install.scale = scale; install.oldsysfont = oldsysfont;
    capture_font(&screen->RastPort, &install.oldraster);
    if (screen->BarLayer) capture_font(screen->BarLayer->rp, &install.oldbar);

    ScaleResultSetStage(result, SSR_STAGE_COMMIT, NULL);
    ScaleResultReason(result, SSR_REASON_COMMIT_FAILED);
    /* BEGIN allocation-free install. Every possible failure is above. */
    install_scale(&install, FALSE, IntuitionBase);
    /* Metadata/pixels are committed. Ordinary frame/layout drawing below may
     * allocate and invoke hooks, so rollback is no longer attempted. Events
     * remain private until that OS work completes under the pinned layers. */
    committed = TRUE;
    /* END allocation-free install. */
done:
    if (fitframes) FreeVec(fitframes);
    /* Composition damage locks were acquired last, so release them first. */
    if (windows)
        for (i = count; i > 0; --i)
        {
            struct ScaleWindow *record = &windows[i - 1];
            if (record->composition) ReleaseWindowCompositionScale(record->composition, IntuitionBase);
            if (record->shapes && !record->linked)
            {
                if (record->shapes->outer) DisposeRegion(record->shapes->outer);
                if (record->shapes->client) DisposeRegion(record->shapes->client);
                FreeVec(record->shapes);
            }
        }
    if (committed) FinishLayerGeometryBatch(layers);
    if (committed)
    {
        /* Client layout/drawing can allocate; this is ordinary postcommit work.
         * Keep the pinned layer locks until all OS
         * frame/layout work is complete, before an SMP client can consume a
         * refresh message and clear LAYERREFRESH. */
        for (i = 0; i < count; ++i)
        {
            struct ScaleWindow *record = &windows[i];
            LONG dx = record->window->Width - record->oldwidth;
            LONG dy = record->window->Height - record->oldheight;
            if (dx || dy) WindowSizeHasChanged(record->window, dx, dy, FALSE, IntuitionBase);
            if (WLAYER(record->window)->Flags & LAYERREFRESH)
                DrawWindowRefresh(record->window, IntuitionBase);
            /* Font/base-face changes may preserve the outer rectangle. */
            if ((!layout || record->frame) && IW(record->window)->ScaleAware &&
                !(record->window->Flags & WFLG_BORDERLESS))
                int_refreshwindowframe(record->window, 0, 0, IntuitionBase);
            UpdateMouseCoords(record->window);
        }
        if (!layout)
        {
            int_CalcSkinInfo(screen, IntuitionBase);
            InstallScreenScaleDecoration(decor, FALSE); /* prepared depth stays inside bar */
        }
        /* A layout-only resize can expose OS-owned border/requester/bar
         * layers too; refresh those without rebuilding screen metadata. */
        DrawAuxiliaryLayerRefresh(screen, IntuitionBase);
        if (!layout) RenderScreenBar(screen, FALSE, IntuitionBase);
        WindowCompositionPresentScreen(screen, IntuitionBase);
    }
    if (committed)
    {
        PreparedNotificationsFill(notifications, IntuitionBase);
        PreparedNotificationsEnqueue(notifications, IntuitionBase);
    }
    if (layers) ReleaseLayerGeometryBatch(layers);
    if (windows)
        for (i = count; i > 0; --i)
            if (windows[i - 1].requester)
                FinishSystemRequesterScale(windows[i - 1].requester, committed, IntuitionBase);
    /* Prepared frame resources may be borrowed by shape and pixel generations. */
    if (windows)
        for (i = count; i > 0; --i)
            if (windows[i - 1].frame)
                ReleaseWindowFrame(windows[i - 1].frame, committed, IntuitionBase);
    if (committed)
        for (i = 0; i < count; ++i)
            if (windows[i].shapes)
            {
                /* Old Layers/clip generations and their borrowed custom
                 * shapes are now retired. Keep only the current owner. */
                free_shape_chain(windows[i].shapes->previous, IntuitionBase);
                windows[i].shapes->previous = NULL;
            }
    if (!committed && logical && logical != oldlogical) DisposeLogicalScreenInfo(logical, IntuitionBase);
    if (notifications) PreparedNotificationsRelease(notifications, IntuitionBase);
    if (decor) ReleaseScreenScaleDecoration(decor, IntuitionBase);
    if (render) ReleaseScreenRenderContext(render, committed, IntuitionBase);
    WindowFitFinish(fit, FALSE, IntuitionBase);
    FreeVec(requests); FreeVec(specs); FreeVec(windows);
    UNLOCK_REFRESH(screen);
    /* Finish/Release has ended every cursor lease. IBase still pins lifetime.
     * An invariant failure retains pending generations/buffers deliberately:
     * releasing possibly live resources would corrupt the display. */
    if (pointer && !ReleaseScreenPointerScale(pointer, committed, IntuitionBase))
        bug("[SCALE] pointer release refused after frame lease release\n");
    if (committed) ScaleResultFinish(result, SSR_SUCCESS, is->Scale);
    UnlockIBase(lock);
    if (!committed) log_scale_refusal(result);
    if (committed && !layout) { CloseFont(oldfont); CloseFont(oldbase); }
    return committed;
}
