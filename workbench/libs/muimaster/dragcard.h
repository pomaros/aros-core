/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef ZUNE_DRAGCARD_H
#define ZUNE_DRAGCARD_H
/* Deliberately opaque card: alpha is flattened against our own theme color,
 * never pixels read from the scene. The source snapshot remains immutable. */
static BOOL PrepareDragCardPixels(struct BitMapNode *node, const UBYTE background[3])
{
    ULONG x, count = node->bmn_Width * node->bmn_Height;
    const UBYTE *src = node->bmn_BitMapBuffer;
    UBYTE *dst;
    if (!src || node->bmn_CardPixels) return FALSE;
    dst = AllocVec(count * 4, MEMF_ANY);
    if (!dst) return FALSE;
    for (x = 0; x < count; ++x)
    {
        ULONG channel, alpha = src[x * 4];
        dst[x * 4] = 255;
        for (channel = 0; channel < 3; ++channel)
            dst[x * 4 + channel + 1] = (src[x * 4 + channel + 1] * alpha +
                background[channel] * (255 - alpha) + 127) / 255;
    }
    node->bmn_CardPixels = dst;
    return TRUE;
}
#endif
