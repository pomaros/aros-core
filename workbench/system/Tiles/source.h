/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. All rights reserved.
 * SPDX-License-Identifier: APL-1.1 */
#ifndef TILES_SOURCE_H
#define TILES_SOURCE_H
#include <exec/types.h>
#include <exec/ports.h>
#include <rexx/storage.h>
#define TILE_SOURCE_LIMIT 40
#define TILE_SOURCE_TIMEOUT 1000UL
struct TileRequest {
    struct RexxMsg *message;
    APTR owner;
    ULONG started;
    BOOL setting, expired;
};
struct TileSources {
    struct MsgPort *replies;
    struct TileRequest requests[TILE_SOURCE_LIMIT];
};
typedef void (*TileSourceResult)(APTR owner, BOOL setting, BOOL success,
                               CONST_STRPTR text, APTR context);
BOOL TileSourcesInit(struct TileSources *sources);
BOOL TileSourceSend(struct TileSources *sources, APTR owner, BOOL setting,
                    CONST_STRPTR portName, CONST_STRPTR command, ULONG now);
BOOL TileSourcePending(struct TileSources *sources, APTR owner);
void TileSourcesCollect(struct TileSources *sources, TileSourceResult callback, APTR context);
void TileSourcesExpire(struct TileSources *sources, ULONG now,
                       TileSourceResult callback, APTR context);
ULONG TileSourcesNext(struct TileSources *sources, ULONG now, ULONG fallback);
BOOL TileSourcesIdle(struct TileSources *sources);
BOOL TileSourcesClose(struct TileSources *sources);
BOOL TileSetCommand(CONST_STRPTR pattern, CONST_STRPTR value, STRPTR output, ULONG capacity);
#endif
