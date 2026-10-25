/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef ZUNE_DRAGPIXELS_H
#define ZUNE_DRAGPIXELS_H

/* Prepare an immutable physical ghost before publishing the node. The client
 * retains ownership of its logical source bitmap and mask. */
static BOOL PrepareDragPixels(struct BitMapNode *node, BOOL sourcealpha, ULONG scale)
{
    struct RastPort rp;
    UBYTE *pixels = NULL, *scaled = NULL;
    ULONG width, height, x, y;
    UQUAD bytes, targetbytes;
    QUAD left, top;
    if (scale < 1 || scale > 4 || node->bmn_Width <= 0 || node->bmn_Height <= 0) return FALSE;
    if (node->bmn_Width > (LONG)GetBitMapAttr(node->bmn_BitMap, BMA_WIDTH) ||
        node->bmn_Height > (LONG)GetBitMapAttr(node->bmn_BitMap, BMA_HEIGHT)) return FALSE;
    width = node->bmn_Width; height = node->bmn_Height;
    /* Cybergraphics pixel-array strides are UWORD byte counts. */
    if (width > 16383 / scale || height > 32767 / scale) return FALSE;
    if (node->bmn_Mask) return FALSE;
    left = (QUAD)node->bmn_Left * scale; top = (QUAD)node->bmn_Top * scale;
    if (left < (-2147483647LL-1) || left > 2147483647LL ||
        top < (-2147483647LL-1) || top > 2147483647LL) return FALSE;
    bytes = (UQUAD)width * height * 4; targetbytes = bytes * scale * scale;
    /* A transient ghost must not consume unbounded image memory. */
    if (targetbytes > 64UL * 1024 * 1024) return FALSE;
    pixels = AllocVec((ULONG)bytes, MEMF_ANY);
    if (!pixels) return FALSE;
    InitRastPort(&rp); rp.BitMap = node->bmn_BitMap;
    if (ReadPixelArray(pixels, 0, 0, width * 4, &rp, 0, 0, width, height, RECTFMT_ARGB) != width * height)
    {
        DeinitRastPort(&rp); FreeVec(pixels); return FALSE;
    }
    DeinitRastPort(&rp);
    if (scale > 1)
    {
        scaled = AllocVec((ULONG)targetbytes, MEMF_ANY);
        if (!scaled) { FreeVec(pixels); return FALSE; }
        for (y = 0; y < height * scale; ++y) for (x = 0; x < width * scale; ++x)
        {
            UBYTE *src = pixels + ((y / scale) * width + x / scale) * 4;
            UBYTE *dst = scaled + (y * width * scale + x) * 4;
            dst[0] = sourcealpha ? src[0] : 255;
            dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3];
        }
        FreeVec(pixels); pixels = scaled;
    }
    if (!sourcealpha && scale == 1)
        for (y = 0; y < height; ++y) for (x = 0; x < width; ++x)
            pixels[(y * width + x) * 4] = 255;
    node->bmn_BitMapBuffer = pixels;
    node->bmn_Width = width * scale; node->bmn_Height = height * scale;
    node->bmn_Left = left; node->bmn_Top = top;
    return TRUE;
}
#endif
