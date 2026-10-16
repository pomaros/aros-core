/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1
 */
#ifndef TILES_RENDER_H
#define TILES_RENDER_H
#include "tiles_config.h"
struct RastPort;
/* Draw in [0,width)x[0,height), with the RastPort's current font. Pens 0..3
 * are background, text, accent and quiet respectively. No allocation/state,
 * no Zune; the caller owns its RastPort/bitmap and palette. Smaller surfaces
 * (including a titlebar) use compact geometry. Largest supported 1x1/2x1/2x2
 * layout fitting the configured cells is centred in the supplied surface.
 * Returns 0 before drawing for invalid dimensions/config/font; otherwise 1.
 * The RastPort's drawing pen/mode/cursor are changed. No Layer clipping needed.
 */
int TileRender(struct RastPort *rp, const struct TileConfig *config,
               const struct TileValue *value, unsigned width, unsigned height);
#endif
