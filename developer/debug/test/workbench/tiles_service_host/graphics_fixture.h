/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1
 */
#ifndef TILES_GRAPHICS_FIXTURE_H
#define TILES_GRAPHICS_FIXTURE_H
#include <stddef.h>
#define JAM1 0
struct Rectangle { int MinX, MinY, MaxX, MaxY; };
struct TextExtent { int te_Width, te_Height; struct Rectangle te_Extent; };
struct TextFont { int width, height, bearing; };
struct RastPort { struct TextFont *Font; int pen, x, y; };
void SetAPen(struct RastPort *, unsigned);
void SetDrMd(struct RastPort *, unsigned);
void RectFill(struct RastPort *, int, int, int, int);
void Move(struct RastPort *, int, int);
void Draw(struct RastPort *, int, int);
void TextExtent(struct RastPort *, const char *, size_t, struct TextExtent *);
void Text(struct RastPort *, const char *, size_t);
#endif
