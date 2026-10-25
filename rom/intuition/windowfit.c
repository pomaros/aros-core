/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License, version 1.1.

    Canonical placements use unscaled client units. Automatic fitting never
    overwrites them. Explicit client geometry changes replace only the axes
    changed; a child's placement is relative to its physical parent. Geometry
    is validated for every window before any existing window is changed.
    Layer/region allocation failure during commit requires rollback and is a
    distinct contract from geometry validation; it is not made atomic here.
*/
#include "intuition_intern.h"
#include "windowfit.h"
#include "scaleresult.h"
#include "windowcomposition.h"
#include "inputhandler_actions.h"
#include "inputhandler_support.h"
#include "../graphics/scalecompose.h"
#include <exec/memory.h>
#include <hidd/gfx.h>
#include <proto/exec.h>
#include <proto/layers.h>

struct FitGeometry
{
    LONG left, top, width, height, relleft, reltop;
    UWORD minwidth, minheight, maxwidth, maxheight;
};
struct FitPlan
{
    struct Window *window;
    struct FitGeometry old, desired;
    struct ScaleComposeRect physical;
    UBYTE state;
    BOOL legacy;
    struct AwareGeometryFrame frame;
    const struct ScreenRenderContext *oldMinContext, *newMinContext;
    LONG oldMinWidth, oldMinHeight, newMinWidth, newMinHeight;
};
struct WindowFitTransaction
{
    struct Screen *screen;
    ULONG count;
    UWORD factor, scale;
    BOOL restoreOffscreen;
    const struct WindowLayoutFit *layout;
    struct FitPlan plans[];
};

ULONG WindowFitCount(const struct WindowFitTransaction *transaction)
{
    return transaction ? transaction->count : 0;
}

BOOL WindowFitDesired(const struct WindowFitTransaction *transaction, ULONG index,
    struct Window **window, struct WindowFitGeometry *geometry)
{
    const struct FitPlan *plan;
    if (!transaction || index >= transaction->count || !window || !geometry)
        return FALSE;
    plan = &transaction->plans[index];
    *window = plan->window;
    geometry->left = plan->desired.left; geometry->top = plan->desired.top;
    geometry->width = plan->desired.width; geometry->height = plan->desired.height;
    geometry->relleft = plan->desired.relleft; geometry->reltop = plan->desired.reltop;
    geometry->physical_left = plan->physical.left;
    geometry->physical_top = plan->physical.top;
    geometry->physical_width = plan->physical.right - plan->physical.left;
    geometry->physical_height = plan->physical.bottom - plan->physical.top;
    return TRUE;
}

/* No layer API, canonical-state change, client method, damage operation or
 * notification is permitted here. The prepared Layers batch owns all of those
 * OS geometry consequences; application layout happens after publication. */
void WindowFitInstall(struct WindowFitTransaction *transaction, BOOL rollback)
{
    ULONG index;
    if (!transaction) return;
    for (index = 0; index < transaction->count; ++index)
    {
        const struct FitPlan *plan = &transaction->plans[index];
        const struct FitGeometry *geometry = rollback ? &plan->old : &plan->desired;
        struct Window *window = plan->window;
        window->LeftEdge = geometry->left; window->TopEdge = geometry->top;
        window->Width = geometry->width; window->Height = geometry->height;
        window->RelLeftEdge = geometry->relleft; window->RelTopEdge = geometry->reltop;
        window->MinWidth = geometry->minwidth; window->MinHeight = geometry->minheight;
        window->MaxWidth = geometry->maxwidth; window->MaxHeight = geometry->maxheight;
        window->GZZWidth = geometry->width - window->BorderLeft - window->BorderRight;
        window->GZZHeight = geometry->height - window->BorderTop - window->BorderBottom;
        IW(window)->LayoutMinContext = rollback ? plan->oldMinContext : plan->newMinContext;
        IW(window)->LayoutMinWidth = rollback ? plan->oldMinWidth : plan->newMinWidth;
        IW(window)->LayoutMinHeight = rollback ? plan->oldMinHeight : plan->newMinHeight;
    }
}

static BOOL legacy_window(struct Window *window)
{
    return !IW(window)->ScaleAware && IS_HIDD_BM(window->WScreen->RastPort.BitMap);
}

BOOL WindowFitViewport(struct Window *window, LONG *width, LONG *height)
{
    UWORD factor = legacy_window(window) ? ScaleComposeFactor(IS(window->WScreen)->Scale) : 1;
    *width = window->WScreen->Width / factor;
    *height = window->WScreen->Height / factor;
    return *width > 0 && *height > 0;
}

static void current_geometry(struct Window *window, struct FitGeometry *geometry)
{
    geometry->left = window->LeftEdge; geometry->top = window->TopEdge;
    geometry->width = window->Width; geometry->height = window->Height;
    geometry->relleft = window->RelLeftEdge; geometry->reltop = window->RelTopEdge;
    geometry->minwidth = window->MinWidth; geometry->minheight = window->MinHeight;
    geometry->maxwidth = window->MaxWidth; geometry->maxheight = window->MaxHeight;
}

/* Constructor references are copied before AutoAdjust. A filled dimension
 * keeps the exact physical viewport as its reference, avoiding early rounding. */
BOOL WindowFitInitializeGeometry(struct Window *window,
    const struct ScaleWindowGeometry *requested, LONG width, LONG height,
    BOOL autoAdjust, struct NewWindow *nw)
{
    struct AwareGeometry canonical;
    struct AwareGeometryPlan plan;
    struct AwareGeometryFrame frame = {window->BorderLeft, window->BorderTop,
        window->BorderRight, window->BorderBottom};
    LONG values[AG_COUNT] = { requested->Left, requested->Top,
        requested->Width, requested->Height, requested->MinWidth,
        requested->MinHeight, requested->MaxWidth, requested->MaxHeight };
    ULONG scale = IS(window->WScreen)->Scale, i;
    if (!IW(window)->ScaleAware || !aware_geometry_percentage(scale)) return FALSE;
    for (i = 0; i < AG_COUNT; ++i)
        canonical.axis[i] = (struct AwareGeometryValue){values[i], 100};
    for (i = AG_WIDTH; i <= AG_HEIGHT; ++i)
    {
        if (values[i] == -1)
        {
            LONG position;
            if (!aware_geometry_value(&canonical.axis[i - AG_WIDTH], scale, &position))
                return FALSE;
            canonical.axis[i].value = (i == AG_WIDTH ? width : height) -
                (position > 0 ? position : 0);
            canonical.axis[i].scale = scale;
        }
    }
    /* Unspecified limits are derived from the accepted initial extent,
     * matching OpenWindow's defaulting after AutoAdjust. First fit with just
     * the explicit constraints and the proposed frame's minimum extent. */
    for (i = AG_MAXWIDTH; i < AG_COUNT; ++i)
        if (!values[i]) canonical.axis[i].value = -1;
    if (!aware_geometry_prepare(&canonical, scale, width, height, &frame, &plan)) return FALSE;
    if (!autoAdjust)
    {
        LONG physical[4];
        for (i = 0; i < 4; ++i)
            if (!aware_geometry_value(&canonical.axis[i], scale, &physical[i])) return FALSE;
        if (physical[AG_LEFT] != plan.left || physical[AG_TOP] != plan.top ||
            physical[AG_WIDTH] != plan.width || physical[AG_HEIGHT] != plan.height) return FALSE;
    }
    for (i = AG_MINWIDTH; i < AG_COUNT; ++i)
        if (!values[i]) canonical.axis[i] = (struct AwareGeometryValue){
            (i - AG_MINWIDTH) % 2 ? plan.height : plan.width, scale};
    if (!values[AG_MINWIDTH]) plan.minwidth = plan.width;
    if (!values[AG_MINHEIGHT]) plan.minheight = plan.height;
    if (!values[AG_MAXWIDTH]) plan.maxwidth = plan.width;
    if (!values[AG_MAXHEIGHT]) plan.maxheight = plan.height;
    nw->LeftEdge = plan.left; nw->TopEdge = plan.top;
    nw->Width = plan.width; nw->Height = plan.height;
    nw->MinWidth = plan.minwidth; nw->MinHeight = plan.minheight;
    nw->MaxWidth = plan.maxwidth; nw->MaxHeight = plan.maxheight;
    IW(window)->ScaleGeometry = canonical;
    IW(window)->ScaleGeometryEnabled = TRUE;
    return TRUE;
}

void WindowFitRecordLimits(struct Window *window, ULONG axis, BOOL unlimited)
{
    LONG value;
    if (!IW(window)->ScaleGeometryEnabled || axis < AG_MINWIDTH || axis >= AG_COUNT) return;
    switch (axis)
    {
        case AG_MINWIDTH: value = window->MinWidth; break;
        case AG_MINHEIGHT: value = window->MinHeight; break;
        case AG_MAXWIDTH: value = window->MaxWidth; break;
        default: value = window->MaxHeight; break;
    }
    IW(window)->ScaleGeometry.axis[axis] = (struct AwareGeometryValue){
        unlimited ? -1 : value, IS(window->WScreen)->Scale};
}

static void capture_canonical(struct Window *window)
{
    IW(window)->FitLeft = window->parent ? window->RelLeftEdge : window->LeftEdge;
    IW(window)->FitTop = window->parent ? window->RelTopEdge : window->TopEdge;
    IW(window)->FitWidth = window->Width; IW(window)->FitHeight = window->Height;
    IW(window)->FitCanonicalValid = TRUE;
}

void WindowFitRecordChange(struct Window *window, ULONG axes)
{
    if (IW(window)->FitAutomatic) return;
    if (IW(window)->ScaleGeometryEnabled)
    {
        LONG values[4] = {window->parent ? window->RelLeftEdge : window->LeftEdge,
            window->parent ? window->RelTopEdge : window->TopEdge, window->Width, window->Height};
        ULONG i;
        for (i = 0; i < 4; ++i)
            if (axes & (1UL << i)) IW(window)->ScaleGeometry.axis[i] =
                (struct AwareGeometryValue){values[i], IS(window->WScreen)->Scale};
        return;
    }
    if (!legacy_window(window)) return;
    if (!IW(window)->FitCanonicalValid) capture_canonical(window);
    if (axes & WINDOWFIT_X)
        IW(window)->FitLeft = window->parent ? window->RelLeftEdge : window->LeftEdge;
    if (axes & WINDOWFIT_Y)
        IW(window)->FitTop = window->parent ? window->RelTopEdge : window->TopEdge;
    if (axes & WINDOWFIT_WIDTH) IW(window)->FitWidth = window->Width;
    if (axes & WINDOWFIT_HEIGHT) IW(window)->FitHeight = window->Height;
}

/* Fit the declared min/max contract, including fixed-size windows. Reject a
 * scale when a client's minimum cannot fit, rather than clipping its content. */
static BOOL fit_axis(LONG requestedPosition, LONG requestedSize, LONG available,
    ULONG minimum, ULONG maximum, LONG *position, LONG *size)
{
    LONG length;
    if (available <= 0 || !maximum || minimum > maximum || minimum > (ULONG)available)
        return FALSE;
    length = requestedSize;
    if (length < (LONG)minimum) length = minimum;
    if ((ULONG)length > maximum) length = maximum;
    if (length > available) length = available;
    if (length <= 0) return FALSE;
    *size = length;
    *position = requestedPosition;
    if (*position < 0) *position = 0;
    if (*position > available - length) *position = available - length;
    return TRUE;
}

/* Explicit moves and resizes use the same logical workarea as automatic fit.
 * At 100%, and for aware clients, preserve the existing offscreen policy. */
BOOL WindowFitConstrain(struct Window *window, LONG *left, LONG *top,
    LONG *width, LONG *height, struct IntuitionBase *IntuitionBase)
{
    struct LayersBase *LayersBase = GetPrivIBase(IntuitionBase)->LayersBase;
    UWORD factor = ScaleComposeFactor(IS(window->WScreen)->Scale);
    LONG originX = 0, originY = 0;
    LONG availableWidth, availableHeight, x, y, w, h;
    BOOL success;
    if (!legacy_window(window) || factor == 1 || IS(window->WScreen)->FitBatch) return TRUE;
    LOCK_REFRESH(window->WScreen);
    availableWidth = window->WScreen->Width / factor;
    availableHeight = window->WScreen->Height / factor;
    if (window->parent)
    {
        struct Layer *layer = WindowCompositionLayer(window->parent);
        if (!layer || layer->LayerInfo != &window->WScreen->LayerInfo)
        { UNLOCK_REFRESH(window->WScreen); return FALSE; }
        originX = ScaleComposeFloor(layer->bounds.MinX, factor);
        originY = ScaleComposeFloor(layer->bounds.MinY, factor);
        availableWidth = layer->Width / factor;
        availableHeight = layer->Height / factor;
    }
    success = fit_axis(*left - originX, *width, availableWidth,
        (UWORD)window->MinWidth, (UWORD)window->MaxWidth, &x, &w) &&
        fit_axis(*top - originY, *height, availableHeight,
        (UWORD)window->MinHeight, (UWORD)window->MaxHeight, &y, &h);
    if (success) { *left = originX + x; *top = originY + y; *width = w; *height = h; }
    UNLOCK_REFRESH(window->WScreen);
    return success;
}

static BOOL fit_geometry(struct Window *window, UWORD factor, BOOL restoreOffscreen,
    const struct ScaleComposeRect *parent, struct FitGeometry *geometry,
    struct ScaleComposeRect *physical, struct ScreenScaleResult *diagnostic)
{
    LONG left = IW(window)->FitCanonicalValid ? IW(window)->FitLeft :
        (window->parent ? window->RelLeftEdge : window->LeftEdge);
    LONG top = IW(window)->FitCanonicalValid ? IW(window)->FitTop :
        (window->parent ? window->RelTopEdge : window->TopEdge);
    LONG width = IW(window)->FitCanonicalValid ? IW(window)->FitWidth : window->Width;
    LONG height = IW(window)->FitCanonicalValid ? IW(window)->FitHeight : window->Height;
    LONG availableWidth = parent ? (parent->right - parent->left) / factor : window->WScreen->Width / factor;
    LONG availableHeight = parent ? (parent->bottom - parent->top) / factor : window->WScreen->Height / factor;
    struct ScaleComposeRect local;
    struct ScaleComposeTransform transform = {parent ? parent->left : 0,
        parent ? parent->top : 0, factor};
    ULONG minWidth = (UWORD)window->MinWidth, minHeight = (UWORD)window->MinHeight;
    if (minWidth < (ULONG)window->BorderLeft + window->BorderRight + 1)
        minWidth = window->BorderLeft + window->BorderRight + 1;
    if (minHeight < (ULONG)window->BorderTop + window->BorderBottom + 1)
        minHeight = window->BorderTop + window->BorderBottom + 1;
    /* At base scale, child layers retain the original screen-bound public
     * geometry; their visible region is clipped by the parent separately.
     * A parent-relative viewport is a zoom fitting rule only. */
    LONG originX = parent ? parent->left : 0;
    LONG originY = parent ? parent->top : 0;
    if (factor == 1)
    {
        availableWidth = window->WScreen->Width;
        availableHeight = window->WScreen->Height;
        left += originX;
        top += originY;
    }
    if (!fit_axis(left, width, availableWidth, minWidth, (UWORD)window->MaxWidth,
            &geometry->relleft, &geometry->width))
    {
        ScaleResultGeometry(diagnostic, window, SSR_DOMAIN_LOGICAL, SSR_AXIS_WIDTH,
            minWidth, minHeight, (UWORD)window->MaxWidth, (UWORD)window->MaxHeight,
            availableWidth, availableHeight);
        return FALSE;
    }
    if (!fit_axis(top, height, availableHeight, minHeight, (UWORD)window->MaxHeight,
            &geometry->reltop, &geometry->height))
    {
        ScaleResultGeometry(diagnostic, window, SSR_DOMAIN_LOGICAL, SSR_AXIS_HEIGHT,
            minWidth, minHeight, (UWORD)window->MaxWidth, (UWORD)window->MaxHeight,
            availableWidth, availableHeight);
        return FALSE;
    }
    if (restoreOffscreen)
    {
        /* At factor one, live offscreen moves were permitted by the existing
         * Intuition policy. Restore that canonical placement, including
         * negative edges, instead of applying the zoomed viewport clamp.
         * FixWindowCoords caps only the public origin at the panel's last
         * pixel; a child's canonical origin remains parent-relative. */
        geometry->relleft = left;
        geometry->reltop = top;
        if (left > window->WScreen->Width - 1)
            geometry->relleft = window->WScreen->Width - 1;
        if (top > window->WScreen->Height - 1)
            geometry->reltop = window->WScreen->Height - 1;
    }
    if (factor == 1)
    {
        geometry->relleft -= originX;
        geometry->reltop -= originY;
    }
    local.left = geometry->relleft; local.top = geometry->reltop;
    local.right = local.left + geometry->width; local.bottom = local.top + geometry->height;
    if (!ScaleComposeDamage(&transform, &local, physical)) return FALSE;
    geometry->left = parent ? ScaleComposeFloor(parent->left, factor) + geometry->relleft : geometry->relleft;
    geometry->top = parent ? ScaleComposeFloor(parent->top, factor) + geometry->reltop : geometry->reltop;
    return geometry->left >= -32768 && geometry->left <= 32767 &&
        geometry->top >= -32768 && geometry->top <= 32767 &&
        geometry->width <= 32767 && geometry->height <= 32767;
}

static LONG find_plan(struct WindowFitTransaction *transaction, struct Window *window)
{
    ULONG index;
    for (index = 0; index < transaction->count; index++)
        if (transaction->plans[index].window == window) return index;
    return -1;
}

static BOOL build_plan(struct WindowFitTransaction *transaction, ULONG index,
    struct ScreenScaleResult *diagnostic)
{
    struct FitPlan *plan = &transaction->plans[index];
    struct Window *window = plan->window;
    struct ScaleComposeRect *parent = NULL;
    if (plan->state == 2) return TRUE;
    ScaleResultSetStage(diagnostic, SSR_STAGE_GEOMETRY, window);
    if (plan->state == 1)
    { ScaleResultReason(diagnostic, SSR_REASON_INVALID_CONTEXT); return FALSE; }
    plan->state = 1;
    if (window->parent)
    {
        LONG parentIndex = find_plan(transaction, window->parent);
        if (parentIndex < 0 || !build_plan(transaction, parentIndex, diagnostic)) return FALSE;
        parent = &transaction->plans[parentIndex].physical;
    }
    /* Parent planning may have published its own diagnostic snapshot. Restore
     * this child before any child-specific validation can refuse the plan. */
    ScaleResultSetStage(diagnostic, SSR_STAGE_GEOMETRY, window);
    if (transaction->layout && transaction->layout->window != window)
    {
        /* Preserve the exact old physical box and client-relative origin.
         * A descendant follows only its parent's physical translation, using
         * its own native coordinate domain for the public Window position. */
        struct Layer *layer = WindowCompositionLayer(window);
        LONG dx = 0, dy = 0;
        if (!layer || layer->LayerInfo != &transaction->screen->LayerInfo) return FALSE;
        if (parent)
        {
            struct Layer *oldparent = WindowCompositionLayer(window->parent);
            if (!oldparent) return FALSE;
            dx = parent->left - oldparent->bounds.MinX;
            dy = parent->top - oldparent->bounds.MinY;
            plan->desired.left = plan->old.left + (plan->legacy ?
                ScaleComposeFloor(parent->left, transaction->factor) -
                ScaleComposeFloor(oldparent->bounds.MinX, transaction->factor) : dx);
            plan->desired.top = plan->old.top + (plan->legacy ?
                ScaleComposeFloor(parent->top, transaction->factor) -
                ScaleComposeFloor(oldparent->bounds.MinY, transaction->factor) : dy);
        }
        plan->physical = (struct ScaleComposeRect){layer->bounds.MinX + dx,
            layer->bounds.MinY + dy, layer->bounds.MinX + dx + layer->Width,
            layer->bounds.MinY + dy + layer->Height};
    }
    else if (IW(window)->ScaleGeometryEnabled)
    {
        struct AwareGeometryPlan geometry;
        LONG width = parent ? parent->right - parent->left : transaction->screen->Width;
        LONG height = parent ? parent->bottom - parent->top : transaction->screen->Height;
        if (!aware_geometry_prepare(&IW(window)->ScaleGeometry, transaction->scale,
                width, height, &plan->frame, &geometry))
        {
            if (aware_geometry_metrics(&IW(window)->ScaleGeometry, transaction->scale,
                width, height, &plan->frame, &geometry))
            {
                LONG position, extent;
                ULONG axis = aware_geometry_axis(geometry.left, geometry.width, width,
                    geometry.minwidth, geometry.maxwidth, &position, &extent) ? SSR_AXIS_HEIGHT : SSR_AXIS_WIDTH;
                ScaleResultGeometry(diagnostic, window, SSR_DOMAIN_PHYSICAL, axis,
                    geometry.minwidth, geometry.minheight, geometry.maxwidth, geometry.maxheight, width, height);
            }
            else ScaleResultReason(diagnostic, SSR_REASON_INVALID_CONTEXT);
            return FALSE;
        }
        if (transaction->layout)
        {
            if (geometry.minwidth < transaction->layout->minwidth)
                geometry.minwidth = transaction->layout->minwidth;
            if (geometry.minheight < transaction->layout->minheight)
                geometry.minheight = transaction->layout->minheight;
            if (!aware_geometry_axis(geometry.left, geometry.width, width,
                    geometry.minwidth, geometry.maxwidth, &geometry.left, &geometry.width) ||
                !aware_geometry_axis(geometry.top, geometry.height, height,
                    geometry.minheight, geometry.maxheight, &geometry.top, &geometry.height))
            {
                LONG position, extent;
                ULONG axis = aware_geometry_axis(geometry.left, geometry.width, width,
                    geometry.minwidth, geometry.maxwidth, &position, &extent) ? SSR_AXIS_HEIGHT : SSR_AXIS_WIDTH;
                ScaleResultGeometry(diagnostic, window, SSR_DOMAIN_PHYSICAL, axis,
                    geometry.minwidth, geometry.minheight, geometry.maxwidth, geometry.maxheight, width, height);
                return FALSE;
            }
        }
        plan->desired.relleft = geometry.left; plan->desired.reltop = geometry.top;
        plan->desired.left = geometry.left + (parent ? parent->left : 0);
        plan->desired.top = geometry.top + (parent ? parent->top : 0);
        plan->desired.width = geometry.width; plan->desired.height = geometry.height;
        plan->desired.minwidth = geometry.minwidth; plan->desired.minheight = geometry.minheight;
        plan->desired.maxwidth = geometry.maxwidth; plan->desired.maxheight = geometry.maxheight;
        plan->physical.left = plan->desired.left; plan->physical.top = plan->desired.top;
        plan->physical.right = plan->physical.left + geometry.width;
        plan->physical.bottom = plan->physical.top + geometry.height;
    }
    else if (plan->legacy)
    {
        if (!fit_geometry(window, transaction->factor, transaction->restoreOffscreen, parent,
                &plan->desired, &plan->physical, diagnostic)) return FALSE;
    }
    else
    {
        struct Layer *layer = WindowCompositionLayer(window);
        if (!layer || layer->LayerInfo != &transaction->screen->LayerInfo) return FALSE;
        if (parent)
        {
            plan->desired.left = parent->left + window->RelLeftEdge;
            plan->desired.top = parent->top + window->RelTopEdge;
        }
        plan->physical.left = parent ? parent->left + window->RelLeftEdge +
            layer->bounds.MinX - window->LeftEdge : layer->bounds.MinX;
        plan->physical.top = parent ? parent->top + window->RelTopEdge +
            layer->bounds.MinY - window->TopEdge : layer->bounds.MinY;
        plan->physical.right = plan->physical.left + layer->Width;
        plan->physical.bottom = plan->physical.top + layer->Height;
    }
    plan->state = 2;
    return TRUE;
}

BOOL WindowFitPrepareOpen(struct Window *window, struct IntuitionBase *IntuitionBase)
{
    struct LayersBase *LayersBase = GetPrivIBase(IntuitionBase)->LayersBase;
    struct ScaleComposeRect parent, physical;
    struct ScaleComposeRect *parentPointer = NULL;
    struct FitGeometry geometry;
    BOOL success;
    if (!legacy_window(window)) return TRUE;
    /* OpenWindow has already applied WA_AutoAdjust and validated its ordinary
     * constructor geometry. At 100%, preserve that result, including child
     * extents clipped by a smaller parent, rather than imposing zoom fitting. */
    if (ScaleComposeFactor(IS(window->WScreen)->Scale) == 1)
    {
        capture_canonical(window);
        return TRUE;
    }
    LOCK_REFRESH(window->WScreen);
    if (window->parent)
    {
        struct Layer *layer = WindowCompositionLayer(window->parent);
        if (!layer || layer->LayerInfo != &window->WScreen->LayerInfo)
        { UNLOCK_REFRESH(window->WScreen); return FALSE; }
        parent.left = layer->bounds.MinX; parent.top = layer->bounds.MinY;
        parent.right = parent.left + layer->Width; parent.bottom = parent.top + layer->Height;
        parentPointer = &parent;
    }
    success = fit_geometry(window, ScaleComposeFactor(IS(window->WScreen)->Scale), FALSE,
        parentPointer, &geometry, &physical, NULL);
    if (success)
    {
        /* There are no layers, gadgets or client events yet. Published windows
         * use DoMoveSizeWindow instead; initial fitting precedes their creation. */
        window->LeftEdge = geometry.left; window->TopEdge = geometry.top;
        window->RelLeftEdge = geometry.relleft; window->RelTopEdge = geometry.reltop;
        window->Width = geometry.width; window->Height = geometry.height;
        window->GZZWidth = geometry.width - window->BorderLeft - window->BorderRight;
        window->GZZHeight = geometry.height - window->BorderTop - window->BorderBottom;
        /* A window born zoomed has no earlier 100% placement. Its canonical
         * geometry is the valid, fitted geometry actually returned to the
         * client, including the constructor's implicit min/max constraints. */
        capture_canonical(window);
    }
    UNLOCK_REFRESH(window->WScreen);
    return success;
}

BOOL WindowFitPreflight(struct Screen *screen, ULONG scale,
    const struct WindowFitFrameMetrics *frames, ULONG framecount,
    const struct WindowLayoutFit *layout, struct WindowFitTransaction **result,
    struct ScreenScaleResult *diagnostic, struct IntuitionBase *IntuitionBase)
{
    struct WindowFitTransaction *transaction;
    struct Window *window;
    ULONG count = 0, index = 0;
    *result = NULL;
    if (layout && (!layout->window || layout->window->WScreen != screen ||
        !IW(layout->window)->ScaleAware || !IW(layout->window)->ScaleGeometryEnabled ||
        layout->minwidth <= 0 || layout->minheight <= 0 ||
        layout->minwidth > 32767 || layout->minheight > 32767))
    {
        ScaleResultSetStage(diagnostic, SSR_STAGE_GEOMETRY, NULL);
        ScaleResultReason(diagnostic, SSR_REASON_INVALID_CONTEXT);
        return FALSE;
    }
    for (window = screen->FirstWindow; window; window = window->NextWindow) count++;
    if ((UQUAD)count * sizeof(struct FitPlan) + sizeof(*transaction) > (IPTR)-1) return FALSE;
    transaction = AllocVec(sizeof(*transaction) + count * sizeof(struct FitPlan), MEMF_PUBLIC | MEMF_CLEAR);
    if (!transaction)
    { ScaleResultReason(diagnostic, SSR_REASON_NO_MEMORY); return FALSE; }
    transaction->screen = screen; transaction->count = count;
    transaction->factor = ScaleComposeFactor(scale);
    transaction->scale = scale;
    transaction->layout = layout;
    transaction->restoreOffscreen = transaction->factor == 1 &&
        (GetPrivIBase(IntuitionBase)->IControlPrefs.ic_Flags & ICF_OFFSCREENLAYERS) &&
        (screen->LayerInfo.Flags & LIFLG_SUPPORTS_OFFSCREEN_LAYERS);
    for (window = screen->FirstWindow; window; window = window->NextWindow)
    {
        struct FitPlan *plan = &transaction->plans[index++];
        plan->window = window; plan->legacy = legacy_window(window);
        current_geometry(window, &plan->old); plan->desired = plan->old;
        plan->oldMinContext = IW(window)->LayoutMinContext;
        plan->oldMinWidth = IW(window)->LayoutMinWidth;
        plan->oldMinHeight = IW(window)->LayoutMinHeight;
        if (layout)
        {
            plan->newMinContext = plan->oldMinContext;
            plan->newMinWidth = plan->oldMinWidth; plan->newMinHeight = plan->oldMinHeight;
            if (window == layout->window)
            {
                plan->newMinContext = layout->context;
                plan->newMinWidth = layout->minwidth; plan->newMinHeight = layout->minheight;
            }
        }
        /* A normal scale/font publication invalidates every measured minimum,
         * even when a retained immutable generation happens to be reused. */
        if (IW(window)->ScaleGeometryEnabled)
        {
            ULONG j;
            for (j = 0; frames && j < framecount && frames[j].window != window; ++j) ;
            if (!frames || j == framecount)
            {
                ScaleResultSetStage(diagnostic, SSR_STAGE_FRAME, window);
                ScaleResultReason(diagnostic, SSR_REASON_INVALID_CONTEXT);
                ScaleResultDetail(diagnostic, SSR_DETAIL_FRAME_METRICS, 0, 0, 0, NULL);
                FreeVec(transaction); return FALSE;
            }
            plan->frame = (struct AwareGeometryFrame){frames[j].left, frames[j].top,
                frames[j].right, frames[j].bottom};
        }
    }
    /* Plans below report their own window; no frame stage stays attached. */
    ScaleResultSetStage(diagnostic, SSR_STAGE_GEOMETRY, NULL);
    if (layout && find_plan(transaction, layout->window) < 0)
    {
        ScaleResultSetStage(diagnostic, SSR_STAGE_GEOMETRY, layout->window);
        ScaleResultReason(diagnostic, SSR_REASON_INVALID_CONTEXT);
        FreeVec(transaction); return FALSE;
    }
    for (index = 0; index < count; index++)
        if (!build_plan(transaction, index, diagnostic)) { FreeVec(transaction); return FALSE; }
    *result = transaction;
    return TRUE;
}

/* Apply parents before children even when the screen list is front-to-back.
 * Geometry was validated against the parent's planned physical extent. */
static BOOL apply_plan(struct WindowFitTransaction *transaction, ULONG index,
    BOOL rollback, struct IntuitionBase *IntuitionBase)
{
    struct FitPlan *plan = &transaction->plans[index];
    struct Window *window = plan->window;
    struct FitGeometry *geometry = rollback ? &plan->old : &plan->desired;
    BOOL success;
    if (plan->state == 2) return TRUE;
    if (window->parent)
    {
        LONG parentIndex = find_plan(transaction, window->parent);
        if (parentIndex < 0 || !apply_plan(transaction, parentIndex, rollback, IntuitionBase))
            return FALSE;
    }
    if (plan->legacy && (window->LeftEdge != geometry->left || window->TopEdge != geometry->top ||
            window->Width != geometry->width || window->Height != geometry->height))
    {
        IW(window)->FitAutomatic++;
        success = DoMoveSizeWindow(window, geometry->left, geometry->top,
            geometry->width, geometry->height, FALSE, IntuitionBase);
        IW(window)->FitAutomatic--;
        if (!success) return FALSE;
        window->RelLeftEdge = geometry->relleft; window->RelTopEdge = geometry->reltop;
    }
    plan->state = 2;
    return TRUE;
}

BOOL WindowFitApply(struct WindowFitTransaction *transaction, BOOL rollback,
    struct IntuitionBase *IntuitionBase)
{
    ULONG index;
    BOOL success = TRUE;
    struct Screen *screen = transaction->screen;
    for (index = 0; index < transaction->count; index++) transaction->plans[index].state = 0;
    IS(screen)->FitBatch++;
    for (index = 0; index < transaction->count; index++)
        if (!apply_plan(transaction, index, rollback, IntuitionBase)) { success = FALSE; break; }
    IS(screen)->FitBatch--;
    return success;
}

void WindowFitFinish(struct WindowFitTransaction *transaction, BOOL notify,
    struct IntuitionBase *IntuitionBase)
{
    ULONG index;
    if (!transaction) return;
    if (notify)
        for (index = 0; index < transaction->count; index++)
        {
            struct FitPlan *plan = &transaction->plans[index];
            struct Window *window = plan->window;
            if (window->LeftEdge != plan->old.left || window->TopEdge != plan->old.top ||
                window->Width != plan->old.width || window->Height != plan->old.height)
                NotifyWindowGeometry(window,
                    window->Width != plan->old.width || window->Height != plan->old.height,
                    IntuitionBase);
        }
    FreeVec(transaction);
}
