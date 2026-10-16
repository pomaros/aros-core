/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1
 */
#include "tiles_render.h"
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <proto/graphics.h>
#include <stdio.h>
#include <string.h>

static int terminated(const char *s, size_t capacity)
{ return memchr(s, 0, capacity) != NULL; }
static void box(struct RastPort *rp, int x, int y, int w, int h, unsigned pen)
{
    if (w <= 0 || h <= 0) return;
    SetAPen(rp, pen);
    RectFill(rp, x, y, x + w - 1, y + h - 1);
}
static void line(struct RastPort *rp, int x, int y, int xx, int yy, unsigned pen)
{ SetAPen(rp, pen); Move(rp, x, y); Draw(rp, xx, yy); }
/* TextExtent accounts for glyph bearings and italic overhang, not only the
 * cursor advance. Prefix shortening never draws outside the supplied box. */
static void text(struct RastPort *rp, const char *s, int x, int y, int w, int h)
{
    struct TextExtent extent;
    size_t n = strlen(s);
    if (w <= 0 || h <= 0) return;
    while (n) {
        TextExtent(rp, s, n, &extent);
        int tw = extent.te_Extent.MaxX - extent.te_Extent.MinX + 1;
        int th = extent.te_Extent.MaxY - extent.te_Extent.MinY + 1;
        if (tw > 0 && th > 0 && tw <= w && th <= h) {
            SetAPen(rp, 1);
            Move(rp, x - extent.te_Extent.MinX,
                 y + (h - th) / 2 - extent.te_Extent.MinY);
            Text(rp, s, n);
            return;
        }
        --n;
    }
}
static void glyph(struct RastPort *rp, const char *name, int x, int y, int size, unsigned pen)
{
    int c = size / 2, q = size / 4;
    if (size < 4) return;
    if (!strcmp(name, "wifi")) {
        line(rp, x, y + q, x + c, y, pen);
        line(rp, x + c, y, x + size - 1, y + q, pen);
        line(rp, x + q, y + c, x + c, y + q, pen);
        line(rp, x + c, y + q, x + size - q - 1, y + c, pen);
        box(rp, x + c - 1, y + size - 3, 3, 3, pen);
    } else if (!strcmp(name, "volume") || !strcmp(name, "sound")) {
        box(rp, x, y + q, q, size - 2 * q, pen);
        line(rp, x + q, y + q, x + c, y, pen);
        line(rp, x + c, y, x + c, y + size - 1, pen);
        line(rp, x + c, y + size - 1, x + q, y + size - q - 1, pen);
        line(rp, x + size - 1, y + q, x + size - 1, y + size - q - 1, pen);
    } else {
        /* Neutral geometric glyph for identifiers without a resident symbol. */
        line(rp, x + c, y, x + size - 1, y + c, pen);
        line(rp, x + size - 1, y + c, x + c, y + size - 1, pen);
        line(rp, x + c, y + size - 1, x, y + c, pen);
        line(rp, x, y + c, x + c, y, pen);
    }
}
/* Raster ellipses avoid floating point and keep every span inside its box. */
static void ellipse(struct RastPort *rp, int cx, int cy, int rx, int ry, unsigned pen)
{
    if (rx < 1 || ry < 1) return;
    for (int dy = -ry; dy <= ry; ++dy) {
        int dx = rx;
        while (dx && (long long)dx*dx*ry*ry + (long long)dy*dy*rx*rx > (long long)rx*rx*ry*ry) --dx;
        box(rp, cx-dx, cy+dy, 2*dx+1, 1, pen);
    }
}
static void eyes(struct RastPort *rp, const struct TileValue *v,
                 int x, int y, int w, int h, unsigned width, unsigned height)
{
    int rx = (w-4)/5, ry = (h-4)/2;
    if (rx < 2 || ry < 2) return;
    if (ry > rx*2) ry = rx*2;
    int cy = y+h/2, pupil = (rx < ry ? rx : ry)/3;
    if (pupil < 1) pupil = 1;
    long px = v->pointerX, py = v->pointerY;
    if (px < 0) px = 0;
    if (py < 0) py = 0;
    if (px >= (long)width) px = width-1;
    if (py >= (long)height) py = height-1;
    for (int i=0; i<2; ++i) {
        int cx = x + (i ? 3*w/4 : w/4);
        int dx = v->hovered ? (int)((px*2-(width-1))*(rx-pupil-1)/(long)(width-1)) : 0;
        int dy = v->hovered ? (int)((py*2-(height-1))*(ry-pupil-1)/(long)(height-1)) : 0;
        /* Half-axis motion keeps the entire pupil inside the white ellipse. */
        ellipse(rp,cx,cy,rx,ry,1);
        ellipse(rp,cx+dx/2,cy+dy/2,pupil,pupil,2);
    }
}
int TileRender(struct RastPort *rp, const struct TileConfig *config,
               const struct TileValue *value, unsigned width, unsigned height)
{
    int x, y, w, h, pad, g, textX, textW, valueY, valueH;
    unsigned layoutW, layoutH, pen;
    char numeric[16];
    const char *display;
    if (!rp || !rp->Font || !config || !value || width < 12 || height < 12 ||
        width > 4096 || height > 4096 || config->cellsW < 1 || config->cellsW > 4 ||
        config->cellsH < 1 || config->cellsH > 4 || config->templateKind < TILE_TOGGLE ||
        config->templateKind > TILE_WEATHER || !terminated(config->label, sizeof(config->label)) ||
        !terminated(config->glyph, sizeof(config->glyph)) || !terminated(value->text, sizeof(value->text))) return 0;
    if ((config->templateKind == TILE_GAUGE || config->templateKind == TILE_SLIDER) &&
        (value->number < 0 || value->number > 100)) return 0;
    if (config->templateKind == TILE_TOGGLE && value->boolean != 0 && value->boolean != 1) return 0;
    layoutW = config->cellsW >= 2 ? 2 : 1;
    layoutH = layoutW == 2 && config->cellsH >= 2 ? 2 : 1;
    w = width * layoutW / config->cellsW;
    h = height * layoutH / config->cellsH;
    if (w < 12 || h < 12) return 0;
    x = ((int)width - w) / 2; y = ((int)height - h) / 2;
    pad = w < 32 || h < 32 ? 1 : 4;
    display = value->text;
    pen = 2;
    if (config->templateKind == TILE_TOGGLE) {
        display = value->boolean ? "On" : "Off";
        pen = value->boolean ? 2 : 3;
    } else if (config->templateKind == TILE_GAUGE || config->templateKind == TILE_SLIDER) {
        snprintf(numeric, sizeof(numeric), "%ld%%", value->number); display = numeric;
    }
    SetDrMd(rp, JAM1);
    box(rp, 0, 0, width, height, 0);
    line(rp, x, y, x + w - 1, y, 3);
    line(rp, x, y + h - 1, x + w - 1, y + h - 1, 3);
    line(rp, x, y, x, y + h - 1, 3);
    line(rp, x + w - 1, y, x + w - 1, y + h - 1, 3);
    if (config->templateKind == TILE_EYES) {
        eyes(rp,value,x+pad,y+pad,w-2*pad,h-2*pad,width,height);
        return 1;
    }
    if (config->templateKind == TILE_WEATHER) {
        if (value->number < -100 || value->number > 100) return 0;
        snprintf(numeric,sizeof(numeric),"%ld C",value->number);
        text(rp,config->label,x+pad,y+pad,w-2*pad,h/4);
        text(rp,numeric,x+pad,y+h/4,w-2*pad,h/3);
        text(rp,value->text,x+pad,y+2*h/3,w-2*pad,h/3-pad);
        /* Distinct sun/cloud/rain glyph in the available right-hand margin. */
        if (w >= 96 && h >= 48) {
            int gx=x+w-18, gy=y+h/3;
            if (!strcmp(value->text,"sunny")) ellipse(rp,gx,gy,5,5,2);
            else {
                box(rp,gx-7,gy-2,14,5,3);
                box(rp,gx-3,gy-5,7,3,3);
                if (!strcmp(value->text,"rain") || !strcmp(value->text,"storm"))
                    for (int j=-4;j<=4;j+=4) line(rp,gx+j,gy+5,gx+j-2,gy+9,2);
                if (!strcmp(value->text,"snow")) box(rp,gx-1,gy+6,3,3,1);
            }
        }
        return 1;
    }
    if (layoutW == 1) {
        g = (w < h ? w : h) / 3;
        if (g > 28) g = 28;
        glyph(rp, config->glyph, x + (w - g) / 2, y + pad, g, pen);
        textX = x + pad; textW = w - 2 * pad;
        valueY = y + pad + g; valueH = h - g - 3 * pad;
    } else {
        g = h / (layoutH == 2 ? 4 : 2);
        if (g > w / 4) g = w / 4;
        if (g > 28) g = 28;
        glyph(rp, config->glyph, x + pad, y + pad, g, pen);
        textX = x + g + 2 * pad; textW = w - g - 3 * pad;
        int labelH = layoutH == 2 ? h / 4 : (h - 2 * pad) / 2;
        text(rp, config->label, textX, y + pad, textW, labelH);
        valueY = y + pad + labelH;
        valueH = layoutH == 2 ? h / 4 : h - labelH - 2 * pad;
    }
    if (config->templateKind == TILE_GAUGE || config->templateKind == TILE_SLIDER) {
        int barH = h >= 48 ? 8 : 3;
        int barX = x + pad, barY = y + h - pad - barH;
        int barW = w - 2 * pad;
        if (valueY + valueH > barY - pad) valueH = barY - pad - valueY;
        box(rp, barX, barY, barW, barH, 3);
        box(rp, barX, barY, barW * value->number / 100, barH, 2);
        if (config->templateKind == TILE_SLIDER) {
            int knob = barX + (barW - 3) * value->number / 100;
            box(rp, knob, barY - 1, 3, barH + 2, 1);
        }
    }
    text(rp, display, textX, valueY, textW, valueH);
    return 1;
}
