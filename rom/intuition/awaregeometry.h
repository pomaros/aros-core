/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License, version 1.1.

    Detached geometry calculations for windows which give Intuition ownership
    of their scale-dependent physical rectangle. A canonical value keeps the
    percentage at which it was explicitly supplied; automatic fitting never
    replaces it, so an odd pixel at 200 percent survives repeated round trips.
*/
#ifndef INTUITION_AWAREGEOMETRY_H
#define INTUITION_AWAREGEOMETRY_H
#include <exec/types.h>

enum
{
    AG_LEFT, AG_TOP, AG_WIDTH, AG_HEIGHT,
    AG_MINWIDTH, AG_MINHEIGHT, AG_MAXWIDTH, AG_MAXHEIGHT, AG_COUNT
};
struct AwareGeometryValue { LONG value; UWORD scale; };
struct AwareGeometry { struct AwareGeometryValue axis[AG_COUNT]; };
struct AwareGeometryFrame { LONG left, top, right, bottom; };
struct AwareGeometryPlan
{
    LONG left, top, width, height;
    LONG minwidth, minheight, maxwidth, maxheight;
};

static inline BOOL aware_geometry_percentage(ULONG scale)
{
    return scale >= 100 && scale <= 400 && scale % 25 == 0;
}

static inline BOOL aware_geometry_value(const struct AwareGeometryValue *reference,
    ULONG scale, LONG *result)
{
    QUAD product, rounded;
    if (!aware_geometry_percentage(scale) ||
        !aware_geometry_percentage(reference->scale)) return FALSE;
    product = (QUAD)reference->value * scale;
    rounded = product < 0 ?
        -((-product + reference->scale / 2) / reference->scale) :
        (product + reference->scale / 2) / reference->scale;
    if (rounded < (-2147483647LL - 1) || rounded > 2147483647LL) return FALSE;
    *result = rounded;
    return TRUE;
}

static inline BOOL aware_geometry_axis(LONG requested, LONG extent, LONG available,
    LONG minimum, LONG maximum, LONG *position, LONG *size)
{
    if (available <= 0 || minimum <= 0 || maximum < minimum || minimum > available)
        return FALSE;
    if (extent < minimum) extent = minimum;
    if (extent > maximum) extent = maximum;
    if (extent > available) extent = available;
    if (requested < 0) requested = 0;
    if (requested > available - extent) requested = available - extent;
    *position = requested;
    *size = extent;
    return TRUE;
}

/* The viewport is parent-relative. The caller supplies the proposed frame,
 * never the old live border. On failure both canonical state and output stay
 * untouched; all arithmetic preceding the public SHORT bounds is wide. */
static inline BOOL aware_geometry_metrics(const struct AwareGeometry *canonical,
    ULONG scale, LONG availablewidth, LONG availableheight,
    const struct AwareGeometryFrame *frame, struct AwareGeometryPlan *result)
{
    struct AwareGeometryPlan plan;
    LONG values[AG_COUNT];
    QUAD minwidth, minheight;
    ULONG i;
    if (availablewidth <= 0 || availableheight <= 0 ||
        availablewidth > 32767 || availableheight > 32767 ||
        canonical->axis[AG_MINWIDTH].value < 0 ||
        canonical->axis[AG_MINHEIGHT].value < 0) return FALSE;
    if (!frame || frame->left < 0 || frame->top < 0 ||
        frame->right < 0 || frame->bottom < 0) return FALSE;
    for (i = 0; i < AG_COUNT; ++i)
    {
        if ((i == AG_MAXWIDTH || i == AG_MAXHEIGHT) && canonical->axis[i].value == -1)
        {
            if (!aware_geometry_percentage(canonical->axis[i].scale) ||
                !aware_geometry_percentage(scale)) return FALSE;
            values[i] = 32767;
        }
        else if (!aware_geometry_value(&canonical->axis[i], scale, &values[i])) return FALSE;
    }
    if (values[AG_WIDTH] <= 0 || values[AG_HEIGHT] <= 0 ||
        values[AG_MINWIDTH] < 0 || values[AG_MINHEIGHT] < 0) return FALSE;
    minwidth = (QUAD)frame->left + frame->right + 1;
    minheight = (QUAD)frame->top + frame->bottom + 1;
    if (minwidth > 32767 || minheight > 32767) return FALSE;
    plan.minwidth = values[AG_MINWIDTH] > minwidth ? values[AG_MINWIDTH] : minwidth;
    plan.minheight = values[AG_MINHEIGHT] > minheight ? values[AG_MINHEIGHT] : minheight;
    plan.maxwidth = values[AG_MAXWIDTH] < availablewidth ? values[AG_MAXWIDTH] : availablewidth;
    plan.maxheight = values[AG_MAXHEIGHT] < availableheight ? values[AG_MAXHEIGHT] : availableheight;
    if (plan.maxwidth > 32767) plan.maxwidth = 32767;
    if (plan.maxheight > 32767) plan.maxheight = 32767;
    plan.left = values[AG_LEFT]; plan.top = values[AG_TOP];
    plan.width = values[AG_WIDTH]; plan.height = values[AG_HEIGHT];
    *result = plan;
    return TRUE;
}

static inline BOOL aware_geometry_prepare(const struct AwareGeometry *canonical,
    ULONG scale, LONG availablewidth, LONG availableheight,
    const struct AwareGeometryFrame *frame, struct AwareGeometryPlan *result)
{
    struct AwareGeometryPlan plan;
    if (!aware_geometry_metrics(canonical, scale, availablewidth, availableheight, frame, &plan)) return FALSE;
    if (!aware_geometry_axis(plan.left, plan.width, availablewidth,
            plan.minwidth, plan.maxwidth, &plan.left, &plan.width) ||
        !aware_geometry_axis(plan.top, plan.height, availableheight,
            plan.minheight, plan.maxheight, &plan.top, &plan.height)) return FALSE;
    if (plan.left > 32767 || plan.top > 32767) return FALSE;
    *result = plan;
    return TRUE;
}
#endif
