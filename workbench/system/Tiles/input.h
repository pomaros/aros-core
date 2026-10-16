/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#ifndef TILES_INPUT_H
#define TILES_INPUT_H
#include <exec/types.h>
struct Screen;
struct TileInput;
enum TileInputKind { TILE_INPUT_ALL, TILE_INPUT_GROUP, TILE_INPUT_DROP, TILE_INPUT_CLICK, TILE_INPUT_PANEL_CLICK, TILE_INPUT_PANEL_DROP };
struct TileInputPanel { unsigned group; LONG left,top; ULONG width,height; };
struct TileInputEvent { enum TileInputKind kind; unsigned group; LONG x,y; };
/* Singleton owned by calling main task. Screen must remain locked until Close.
 * Up to40 copied/parsed key expressions; NULL/empty/none disables that group.
 * Caller opens commodities.library (CommoditiesBase) before Open/through Close.
 * Call all APIs from same main task. Callback allocates no application memory.
 * Queue holds16; overflow drops events while still swallowing modal buttons. */
struct TileInput *TileInputOpen(struct Screen *screen,const char *const *groupKeys,unsigned count);
void TileInputClose(struct TileInput *input);
ULONG TileInputSignals(struct TileInput *input);
void TileInputSetLifted(struct TileInput *input, BOOL lifted);
/* Later entries are on top. At most eight screen-coordinate rectangles.
 * Invalid publication returns FALSE and leaves the previous set intact. */
BOOL TileInputSetPanels(struct TileInput *, const struct TileInputPanel *, unsigned count);
BOOL TileInputNext(struct TileInput *input, struct TileInputEvent *event);
#endif
