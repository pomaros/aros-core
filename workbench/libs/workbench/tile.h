/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL) Version 1.1.
    SPDX-License-Identifier: APL-1.1
*/
#ifndef WB_TILE_H
#define WB_TILE_H
#include "tile_state.h"
#include "tile_clock.h"

struct WBTile
{
    struct WBTileState state;
    char definitionPath[512];
    ULONG groupAnchor;
    BOOL group;
    struct Task *owner; /* identity only: never dereferenced */
    struct Library *graphics;
    struct timerequest timer;
    BOOL timerOpen;
    struct BitMap *bitmap, *fallbackBitmap;
    BOOL dirty;
    BOOL visible;
    ULONG visibilityTick;
    LONG geometryLeft, geometryTop;
    ULONG geometryWidth, geometryHeight, geometryTick;
    BOOL geometryValid, suspended;
    UQUAD registrationGeneration, frameGeneration;
    struct Screen *placementScreen; /* identity only, never dereferenced */
    LONG placementLeft, placementTop;
    ULONG placementTick;
    BOOL placementValid;
    BOOL pointerEnabled, hovered, pointerSentInside;
    LONG pointerX, pointerY, pointerSentX, pointerSentY;
    ULONG pointerTick, hoverCadence;
    struct AppIconTileMessage *pointerMessage; /* borrowed until reply; identity only */
};
BOOL WBTileCreate(struct AppIcon *, struct DiskObject *, struct TagItem *, struct WorkbenchBase *);
void WBTileFree(struct AppIcon *, struct WorkbenchBase *);
BOOL WBTilePublish(struct AppIconPublish *, struct WorkbenchBase *);
void WBTileRefreshDone(struct WorkbenchBase *);
BOOL WBTileQuery(struct AppIconQuery *, struct WorkbenchBase *);
BOOL WBTileQueryGeometry(struct AppIconGeometry *, struct WorkbenchBase *);
BOOL WBTileSnapshot(struct AppIconSnapshotRequest *, struct WorkbenchBase *);
BOOL WBTileSnapshotRefresh(APTR, struct WorkbenchBase *);
BOOL WBTileSnapshotFree(APTR, struct WorkbenchBase *);
BOOL WBTileSnapshotActivate(struct AppIconSnapshotActivate *, struct WorkbenchBase *);
BOOL WBTileSuspend(struct AppIcon *, struct WorkbenchBase *);
BOOL WBTileDetach(struct AppIcon *, struct WorkbenchBase *);
void WBTileOwnerRemoved(struct Task *, struct WorkbenchBase *);
#include "tile_messages.h"
#endif
