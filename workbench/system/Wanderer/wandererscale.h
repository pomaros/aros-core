/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    This file is part of AROS, distributed under the AROS Public License 1.1.
*/
#ifndef WANDERER_SCALE_H
#define WANDERER_SCALE_H
#include <exec/types.h>
/* Preferences and saved coordinates remain in points. Render units are
 * derived directly from these immutable values, including after a reset. */
static inline LONG WandererScalePoint(LONG value, ULONG scale)
{
    QUAD product = (QUAD)value * scale;
    return product < 0 ? -(LONG)((-product + 50) / 100) :
        (LONG)((product + 50) / 100);
}
static inline LONG WandererUnscalePoint(LONG value, ULONG scale)
{
    QUAD product = (QUAD)value * 100;
    return product < 0 ? -(LONG)((-product + scale / 2) / scale) :
        (LONG)((product + scale / 2) / scale);
}
#endif
