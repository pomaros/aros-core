/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL) Version 1.1.
    SPDX-License-Identifier: APL-1.1
*/
#ifndef WB_TILE_STATE_H
#define WB_TILE_STATE_H
#include <string.h>

struct WBTileState
{
    ULONG width, height, cadence, lastFrame, lastAlive;
    BOOL fresh, haveFrame;
    struct AppIconRegions regions;
};

static BOOL WBTileStateInitBounded(struct WBTileState *state, ULONG cells,
                           const struct AppIconRegions *regions, ULONG cadence, ULONG limit)
{
    ULONG w = cells >> 16, h = cells & 0xffff, i, j;
    if (w < 1 || w > limit || h < 1 || h > limit)
        return FALSE;
    memset(state, 0, sizeof(*state));
    state->width = w * WBAPPICON_CELL_PIXELS;
    state->height = h * WBAPPICON_CELL_PIXELS;
    state->cadence = cadence < 250 ? 250 : cadence;
    if (!regions)
        return TRUE;
    if (regions->air_Count > WBAPPICON_MAX_REGIONS)
        return FALSE;
    for (i = 0; i < regions->air_Count; ++i) {
        const struct AppIconRegion *r = &regions->air_Regions[i];
        if (!r->air_Name[0] || !memchr(r->air_Name, 0, sizeof(r->air_Name)) ||
            !r->air_Width || !r->air_Height ||
            (ULONG)r->air_Left + r->air_Width > state->width ||
            (ULONG)r->air_Top + r->air_Height > state->height)
            return FALSE;
        for (j = 0; j < i; ++j)
            if (!strcmp(r->air_Name, regions->air_Regions[j].air_Name))
                return FALSE;
    }
    state->regions = *regions;
    return TRUE;
}

static BOOL WBTileStateInit(struct WBTileState *state, ULONG cells,
                           const struct AppIconRegions *regions, ULONG cadence)
{
    return WBTileStateInitBounded(state,cells,regions,cadence,4);
}

static BOOL WBTileStateFresh(const struct WBTileState *state, ULONG now)
{
    return state->fresh && (ULONG)(now - state->lastAlive) < 5000;
}

static BOOL WBTileStateDue(const struct WBTileState *state, ULONG now)
{
    return !state->haveFrame || (ULONG)(now - state->lastFrame) >= state->cadence;
}

/* Half-open rectangles; declaration order resolves overlaps. */
static LONG WBTileStateHit(const struct WBTileState *state, LONG x, LONG y)
{
    ULONG i;
    for (i = 0; i < state->regions.air_Count; ++i) {
        const struct AppIconRegion *r = &state->regions.air_Regions[i];
        if (x >= r->air_Left && y >= r->air_Top &&
            x < (LONG)r->air_Left + r->air_Width &&
            y < (LONG)r->air_Top + r->air_Height)
            return i;
    }
    return -1;
}
#endif
