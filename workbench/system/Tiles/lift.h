/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#ifndef TILES_LIFT_H
#define TILES_LIFT_H
#include <exec/types.h>
struct Screen; struct BitMap; struct TileLift;
struct TileLiftItem { ULONG id; struct BitMap *bitmap; LONG left, top; ULONG width, height; };
#define TILE_LIFT_CLICK 1
#define TILE_LIFT_DISMISS 2
struct TileLiftEvent { ULONG type, id; LONG x, y; };
/* Caller keeps screen locked and bitmaps alive through Close. Metadata copied.
 * No callbacks/source waits/TileRender. Failure leaves no window or allocation. */
struct TileLift *TileLiftOpen(struct Screen *, const struct TileLiftItem *, ULONG count);
/* Small protected non-dimming panel. Item coordinates are panel-local;
 * input coordinates remain screen-relative. Screen and caches borrowed. */
struct TileLift *TilePanelOpen(struct Screen *, const struct TileLiftItem *, ULONG count,
                              LONG left, LONG top, ULONG width, ULONG height);
void TileLiftClose(struct TileLift *);
ULONG TileLiftSignals(const struct TileLift *);
BOOL TileLiftPump(struct TileLift *, struct TileLiftEvent *);
/* Caller broker supplies Escape or SELECTDOWN screen-coordinate clicks.
 * Other mouse codes must not call this function. Pump handles repaint only. */
BOOL TileLiftInput(struct TileLift *, LONG x, LONG y, BOOL escape, struct TileLiftEvent *);
/* TRUE only when an existing item changes bitmap; no implicit blit. */
BOOL TileLiftUpdateBitmap(struct TileLift *, ULONG id, struct BitMap *);
/* Cache-only repaint of item plus later stacking entries; no background write. */
void TileLiftRefreshItem(struct TileLift *, ULONG id);
void TileLiftRefresh(struct TileLift *);
void TileLiftFront(struct TileLift *);
#endif
