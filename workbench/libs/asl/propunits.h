/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    This file is part of AROS and is distributed under the AROS Public License 1.1.
*/
#ifndef ASL_PROPUNITS_H
#define ASL_PROPUNITS_H

/* PropGClass stores PGA values in UWORDs. Pixels remain authoritative in the
 * list; only its private model boundary is quantized. Preserve exact pixels
 * through 65535 and use the whole native range above that. Both endpoints
 * are exact; dragging rounds to the nearest hidden-pixel/range interval.
 * A notification echo of the current unit preserves the exact pixel offset.
 * QUAD products also cover LONG_MAX pixel extents without overflow. */
struct AslPropUnits { LONG total, visible, top; };

static inline struct AslPropUnits AslPropEncode(LONG total, LONG visible, LONG top)
{
    struct AslPropUnits units;
    LONG hidden;
    if (total < 0) total = 0;
    if (visible < 0) visible = 0;
    if (visible > total) visible = total;
    hidden = total - visible;
    if (top < 0) top = 0;
    if (top > hidden) top = hidden;
    units.total = total <= 65535 ? total : 65535;
    units.visible = total <= 65535 ? visible : (LONG)(((QUAD)visible * 65535 + total / 2) / total);
    if (visible && !units.visible) units.visible = 1;
    if (hidden && units.visible == units.total) --units.visible;
    units.top = hidden ? (LONG)(((QUAD)top * (units.total - units.visible) + hidden / 2) / hidden) : 0;
    return units;
}

static inline LONG AslPropDecode(LONG total, LONG visible, LONG current, IPTR top)
{
    struct AslPropUnits units = AslPropEncode(total, visible, current);
    LONG range = units.total - units.visible, hidden;
    if (top == (IPTR)units.top) return current;
    if (visible < 0) visible = 0;
    if (visible > total) visible = total;
    hidden = total - visible;
    if (!range || hidden <= 0) return 0;
    if (top > (IPTR)range) top = range;
    return (LONG)(((QUAD)top * hidden + range / 2) / range);
}
#endif
