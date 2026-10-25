/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Native widget content is rendered at physical resolution. A viewport may
 * scroll the content; its pixels are copied without compositor resampling.
 */
#ifndef APPLE_WIDGETSCALE_H
#define APPLE_WIDGETSCALE_H

struct WidgetCanvas {
    struct BitMap *bitmap;
    struct RastPort port;
    LONG width, height, left, top;
};

static inline LONG WidgetPixels(ULONG scale, LONG logical)
{
    return (logical * (LONG)scale + 50) / 100;
}

static inline const struct ScreenRenderContext *WidgetContext(struct Screen *screen)
{
    IPTR value = 0;
    if (!GetAttr(SA_RenderContext, (Object *)screen, &value))
        return NULL;
    return (const struct ScreenRenderContext *)value;
}

static inline BOOL WidgetCanvasCreate(struct WidgetCanvas *canvas,
    struct Screen *screen, LONG width, LONG height)
{
    if (width < 1 || height < 1 || width > 16384 || height > 16384)
        return FALSE;
    canvas->bitmap = AllocBitMap(width, height,
        GetBitMapAttr(screen->RastPort.BitMap, BMA_DEPTH), BMF_CLEAR,
        screen->RastPort.BitMap);
    if (!canvas->bitmap)
        return FALSE;
    InitRastPort(&canvas->port);
    canvas->port.BitMap = canvas->bitmap;
    canvas->width = width;
    canvas->height = height;
    canvas->left = canvas->top = 0;
    return TRUE;
}

static inline VOID WidgetCanvasPresent(struct WidgetCanvas *canvas, struct Window *window)
{
    LONG width = window->Width - window->BorderLeft - window->BorderRight;
    LONG height = window->Height - window->BorderTop - window->BorderBottom;
    LONG maxx = canvas->width > width ? canvas->width - width : 0;
    LONG maxy = canvas->height > height ? canvas->height - height : 0;
    if (canvas->left < 0) canvas->left = 0;
    if (canvas->top < 0) canvas->top = 0;
    if (canvas->left > maxx) canvas->left = maxx;
    if (canvas->top > maxy) canvas->top = maxy;
    if (width > canvas->width) width = canvas->width;
    if (height > canvas->height) height = canvas->height;
    if (width > 0 && height > 0)
        BltBitMapRastPort(canvas->bitmap, canvas->left, canvas->top,
            window->RPort, window->BorderLeft, window->BorderTop,
            width, height, 0xc0);
}

/* Raw cursor keys and both wheel axes: the same physical offsets are used
 * for hits. A viewport narrower than its page scrolls sideways. */
static inline BOOL WidgetCanvasScroll(struct WidgetCanvas *canvas, UWORD key, LONG step)
{
    switch (key) {
    case 0x4c: case 0x7a: canvas->top -= step; break;
    case 0x4d: case 0x7b: canvas->top += step; break;
    case 0x4f: case 0x7c: canvas->left -= step; break;
    case 0x4e: case 0x7d: canvas->left += step; break;
    default: return FALSE;
    }
    return TRUE;
}

static inline BOOL WidgetCanvasPoint(const struct WidgetCanvas *canvas,
    const struct Window *window, LONG x, LONG y, LONG *contentx, LONG *contenty)
{
    if (x < window->BorderLeft || y < window->BorderTop ||
        x >= window->Width - window->BorderRight ||
        y >= window->Height - window->BorderBottom)
        return FALSE;
    *contentx = x - window->BorderLeft + canvas->left;
    *contenty = y - window->BorderTop + canvas->top;
    return *contentx >= 0 && *contenty >= 0 &&
        *contentx < canvas->width && *contenty < canvas->height;
}

static inline VOID WidgetCanvasDelete(struct WidgetCanvas *canvas)
{
    if (canvas->bitmap) FreeBitMap(canvas->bitmap);
    canvas->bitmap = NULL;
}
#endif
