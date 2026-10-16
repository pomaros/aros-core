/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#ifndef TILES_GROUP_H
#define TILES_GROUP_H
#include <stddef.h>
#include <stdint.h>
#define TILE_GROUP_MAX_MEMBERS 40u
#define TILE_GROUP_MAX_EXTENT 32767
#define TILE_GROUP_MAX_COLUMNS 32u
enum TileGroupAnchor { TILE_GROUP_FREE, TILE_GROUP_TOP, TILE_GROUP_BOTTOM, TILE_GROUP_LEFT, TILE_GROUP_RIGHT };
enum TileGroupShow { TILE_GROUP_ALWAYS, TILE_GROUP_LIFTED, TILE_GROUP_EDGE };
struct TileGroupConfig {
    enum TileGroupAnchor anchor;
    enum TileGroupShow show;
    unsigned columns;
    char title[64], lift[64];
};
struct TileGroupMember {
    int32_t x, y;
    unsigned width, height;
    int explicitPosition, isGroup;
};
struct TileGroupLayout {
    int32_t minX, minY;
    unsigned width, height, count;
    struct { int32_t x, y; } members[TILE_GROUP_MAX_MEMBERS];
};
/* Readable NULL-terminated ToolType strings/array required. At most128 entries,
 * each <=511 bytes. TILE=group is mandatory; duplicate known keys rejected.
 * Unrelated ToolTypes ignored. Defaults: free/always/4 columns; only a drawer
 * named Control defaults to LAmiga C. Failure zeros the output. */
int TileGroupConfigParse(const char *const *types, const char *drawerName,
    struct TileGroupConfig *config, char *error, size_t errorSize);
/* Caller translates DiskObject NO_ICON_POSITION into explicitPosition=0 and
 * supplies surface pixel dimensions. Explicit raw coordinates are preserved
 * relative to minX/minY, including overlap. Missing positions fill free cells.
 * Empty/nested groups, >40 members, invalid/overflowing extents fail closed.
 * No filesystem, allocation, graphics, or DiskObject ownership is taken. */
int TileGroupArrange(const struct TileGroupConfig *config,
    const struct TileGroupMember *members, unsigned count,
    unsigned cellWidth, unsigned cellHeight, struct TileGroupLayout *layout);
/* Full footprint must fit screen. Lifted SHOW=lifted uses pointer, or screen
 * centre if unavailable; anchor wins on its axis. Returns0/zero coordinates
 * on invalid input. No screen pointers retained. */
int TileGroupPlace(const struct TileGroupConfig *config,
    const struct TileGroupLayout *layout, unsigned screenWidth, unsigned screenHeight,
    int32_t preferredX, int32_t preferredY, int lifted, int hasPointer,
    int32_t pointerX, int32_t pointerY, int32_t *x, int32_t *y);
#endif
