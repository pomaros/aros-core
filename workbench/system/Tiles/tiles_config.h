/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1
 */
#ifndef TILES_CONFIG_H
#define TILES_CONFIG_H
#include <stddef.h>

enum TileTemplate { TILE_TOGGLE, TILE_GAUGE, TILE_SLIDER, TILE_VALUE, TILE_GLYPH, TILE_EYES, TILE_WEATHER };
#define TILE_CONFIG_WARN_SIZE 1u
#define TILE_TOOLTYPE_LIMIT 128u
#define TILE_TOOLTYPE_LENGTH 512u
struct TileConfig {
    enum TileTemplate templateKind;
    unsigned cellsW, cellsH;
    char label[64], glyph[32];
    char getPort[64], getCommand[256], setPort[64], setCommand[256];
    char env[128], panel[128];
    unsigned long every;
    unsigned warnings;
    unsigned pointerEnabled; /* HOVER=ON or FAST; disabled by default. */
    unsigned hoverCadence; /* 0 normal budget, 33 FAST (milliseconds). */
};
struct TileValue { char text[256]; long number; int boolean; long pointerX, pointerY; int hovered; };
/* Returns 1 on success. Failure zeros config and writes one bounded error.
 * Unknown unrelated ToolTypes are ignored; duplicate supported keys rejected.
 * SIZE errors fall back to 1x1 with TILE_CONFIG_WARN_SIZE. No silent truncation.
 * Exactly one GET or ENV source is required except EYES, which rejects both. SET requires one literal %v.
 * Input must be a readable NULL-terminated array of readable C strings. */
int TileConfigParse(const char *const *toolTypes, struct TileConfig *config,
                    char *error, size_t errorSize);
/* Strict typed source result: toggle on/off/1/0, gauge/slider 0..100,
 * value/glyph nonempty printable bytes; eyes empty local value; weather signed
 * integer Celsius -100..100 followed by | and sunny/cloudy/rain/snow/storm/fog. Leading/trailing spaces are trimmed.
 * Overlong text is rejected, never truncated. Failure zeros value. */
int TileValueParse(enum TileTemplate kind, const char *text, struct TileValue *value);
#endif
