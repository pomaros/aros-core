/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License, version 1.1.

    Integer window composition geometry. Coordinates are half-open: the
    right and bottom edges belong to the next rectangle. The transform is
    shared by presentation, damage and input hit testing. All intermediate
    arithmetic is signed 64 bit, including negative off-screen origins.
*/
#ifndef GRAPHICS_SCALECOMPOSE_H
#define GRAPHICS_SCALECOMPOSE_H

#include <exec/types.h>

struct ScaleComposeRect { LONG left, top, right, bottom; };
struct ScaleComposeTransform { LONG x, y; UWORD factor; };

/* The public percentage is clamped separately by Intuition. Ties go up. */
static inline UWORD ScaleComposeFactor(ULONG percent)
{
    if (percent < 100) return 1;
    if (percent > 400) return 4;
    return (UWORD)((percent + 50) / 100);
}

static inline BOOL ScaleComposeLong(QUAD value)
{
    return value >= (-2147483647LL - 1) && value <= 2147483647LL;
}

static inline BOOL ScaleComposeDamage(const struct ScaleComposeTransform *t,
    const struct ScaleComposeRect *logical, struct ScaleComposeRect *physical)
{
    QUAD l, r, u, b;
    if (t->factor < 1 || t->factor > 4 ||
        logical->left >= logical->right || logical->top >= logical->bottom)
        return FALSE;
    l = (QUAD)t->x + (QUAD)logical->left * t->factor;
    r = (QUAD)t->x + (QUAD)logical->right * t->factor;
    u = (QUAD)t->y + (QUAD)logical->top * t->factor;
    b = (QUAD)t->y + (QUAD)logical->bottom * t->factor;
    if (!ScaleComposeLong(l) || !ScaleComposeLong(r) ||
        !ScaleComposeLong(u) || !ScaleComposeLong(b)) return FALSE;
    physical->left = l; physical->right = r;
    physical->top = u; physical->bottom = b;
    return TRUE;
}

/* Floor division is essential for captured drags left/above a window. */
static inline QUAD ScaleComposeFloor(QUAD value, UWORD factor)
{
    QUAD q = value / factor;
    if (value % factor < 0) --q;
    return q;
}

static inline BOOL ScaleComposeInput(const struct ScaleComposeTransform *t,
    LONG physicalX, LONG physicalY, LONG *logicalX, LONG *logicalY)
{
    QUAD x, y;
    if (t->factor < 1 || t->factor > 4) return FALSE;
    x = ScaleComposeFloor((QUAD)physicalX - t->x, t->factor);
    y = ScaleComposeFloor((QUAD)physicalY - t->y, t->factor);
    if (!ScaleComposeLong(x) || !ScaleComposeLong(y)) return FALSE;
    *logicalX = x; *logicalY = y;
    return TRUE;
}

/*
 * Copy one damaged logical rectangle into an already clipped visible
 * physical rectangle. Both buffers use native 32-bit pixel values and
 * row pitches in pixels. Source and destination must be independent
 * allocations; presentation never feeds back into the logical surface.
 * The caller holds the surface/layer lock throughout this operation.
 * Row padding and pixels outside clip/damage remain untouched.
 */
static inline BOOL ScaleComposePixels32(const ULONG *source,
    ULONG sourceWidth, ULONG sourceHeight, ULONG sourcePitch,
    ULONG *destination, ULONG destWidth, ULONG destHeight, ULONG destPitch,
    const struct ScaleComposeTransform *t,
    const struct ScaleComposeRect *damage,
    const struct ScaleComposeRect *clip)
{
    struct ScaleComposeRect d = *damage, p;
    LONG x, y;
    if (!source || !destination || source == destination ||
        t->factor < 1 || t->factor > 4 ||
        sourceWidth > 2147483647UL || sourceHeight > 2147483647UL ||
        destWidth > 2147483647UL || destHeight > 2147483647UL ||
        sourcePitch < sourceWidth || destPitch < destWidth ||
        (UQUAD)sourcePitch * sourceHeight > (IPTR)-1 / sizeof(ULONG) ||
        (UQUAD)destPitch * destHeight > (IPTR)-1 / sizeof(ULONG))
        return FALSE;
    if (d.left < 0) d.left = 0;
    if (d.top < 0) d.top = 0;
    if (d.right > (LONG)sourceWidth) d.right = sourceWidth;
    if (d.bottom > (LONG)sourceHeight) d.bottom = sourceHeight;
    if (d.left >= d.right || d.top >= d.bottom) return TRUE;
    if (!ScaleComposeDamage(t, &d, &p)) return FALSE;
    if (p.left < clip->left) p.left = clip->left;
    if (p.top < clip->top) p.top = clip->top;
    if (p.right > clip->right) p.right = clip->right;
    if (p.bottom > clip->bottom) p.bottom = clip->bottom;
    if (p.left < 0) p.left = 0;
    if (p.top < 0) p.top = 0;
    if (p.right > (LONG)destWidth) p.right = destWidth;
    if (p.bottom > (LONG)destHeight) p.bottom = destHeight;
    for (y = p.top; y < p.bottom; ++y)
    {
        ULONG sy = ((QUAD)y - t->y) / t->factor;
        for (x = p.left; x < p.right; ++x)
        {
            ULONG sx = ((QUAD)x - t->x) / t->factor;
            destination[(IPTR)y * destPitch + x] =
                source[(IPTR)sy * sourcePitch + sx];
        }
    }
    return TRUE;
}
#endif
