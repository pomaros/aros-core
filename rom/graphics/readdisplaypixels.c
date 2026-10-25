/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License, version 1.1.

    Read the actual displayed storage, retaining the rendered software cursor.
*/
#include "graphics_intern.h"
#include <proto/oop.h>
#include <string.h>

/* Only contiguous native-order colour fields have an unambiguous intensity.
 * Limiting fields to sixteen bits keeps the rounded conversion in ULONG. */
static BOOL display_channel(ULONG mask, ULONG *shift, ULONG *maximum)
{
    ULONG n = 0;
    if (!mask) return FALSE;
    while (!(mask & 1)) { ++n; mask >>= 1; }
    if (mask > 65535 || (mask & (mask + 1))) return FALSE;
    *shift = n;
    *maximum = mask;
    return TRUE;
}

/****************************************************************************
    NAME
        ReadDisplayPixels

    FUNCTION
        Copy actual direct presentation storage into packed RGB bytes. This
        includes any software cursor already rendered into that storage.
        Backends without an audited direct presentation lease and a matching
        software cursor target refuse. An unbound initial pointer also refuses. No image
        is assembled from cursor metadata or from an off-screen backing bitmap.

        The caller must retain the bitmap and its Layers locks throughout this
        call. Mapping locks follow the Layers, cursor, driver ordering. All
        memory must be allocated before calling; this function dispatches only
        paired PreparePixels/ReleasePixels and otherwise reads/copies bytes.
        This is a CPU-consistent snapshot; it does not wait for vertical blank.

    INPUTS
        bitmap - retained displayed HIDD bitmap
        x, y, width, height - physical source rectangle
        rgb, stride, size - caller-owned output and its row pitch/capacity

    RESULT
        TRUE for a complete RGB snapshot. FALSE leaves output untouched when
        geometry, format, capacity or backend admission fails.
****************************************************************************/
AROS_LH8(BOOL, ReadDisplayPixels,
    AROS_LHA(struct BitMap *, bitmap, A0),
    AROS_LHA(LONG, x, D0),
    AROS_LHA(LONG, y, D1),
    AROS_LHA(ULONG, width, D2),
    AROS_LHA(ULONG, height, D3),
    AROS_LHA(UBYTE *, rgb, A1),
    AROS_LHA(ULONG, stride, D4),
    AROS_LHA(ULONG, size, D5),
    struct GfxBase *, GfxBase, 202, Graphics)
{
    AROS_LIBFUNC_INIT
    struct HIDD_PixelMap map = {0};
    OOP_Object *object;
    ULONG rs, gs, bs, rm, gm, bm, row, column;
    BOOL result = FALSE;
    IPTR destination, displayed, backing;

    destination = (IPTR)rgb;
    if (!bitmap || !IS_HIDD_BM(bitmap) || !rgb || x < 0 || y < 0 ||
        !width || !height || width > (~(ULONG)0) / 3 ||
        stride < width * 3 || height > size / stride ||
        destination > ~(IPTR)0 - size)
        return FALSE;
    object = HIDD_BM_OBJ(bitmap);
    if (!object || !HIDD_BM_PreparePixels(object, &map)) return FALSE;

    displayed = (IPTR)map.presentation;
    backing = (IPTR)map.pixels;
    if (!map.presentation || !map.cursorCookie || !map.cursorState ||
        map.cursorState->cursor_bm != map.driverCookie ||
        displayed > ~(IPTR)0 - map.presentationSize ||
        (destination < displayed + map.presentationSize && displayed < destination + size) ||
        (map.pixels && (backing > ~(IPTR)0 - map.size ||
         (destination < backing + map.size && backing < destination + size))) || !map.width || !map.height ||
        map.width > (~(ULONG)0) / 4 || map.presentationStride < map.width * 4 ||
        map.height > map.presentationSize / map.presentationStride ||
        (ULONG)x >= map.width || (ULONG)y >= map.height ||
        width > map.width - (ULONG)x || height > map.height - (ULONG)y ||
        (map.red & map.green) || (map.red & map.blue) || (map.green & map.blue) ||
        !display_channel(map.red, &rs, &rm) ||
        !display_channel(map.green, &gs, &gm) ||
        !display_channel(map.blue, &bs, &bm))
        goto release;

    for (row = 0; row < height; ++row)
    {
        const UBYTE *source = map.presentation + ((ULONG)y + row) * map.presentationStride + (ULONG)x * 4;
        UBYTE *target = rgb + row * stride;
        for (column = 0; column < width; ++column)
        {
            ULONG pixel;
            memcpy(&pixel, source + column * 4, sizeof(pixel));
            target[column * 3] = ((((pixel & map.red) >> rs) * 255) + rm / 2) / rm;
            target[column * 3 + 1] = ((((pixel & map.green) >> gs) * 255) + gm / 2) / gm;
            target[column * 3 + 2] = ((((pixel & map.blue) >> bs) * 255) + bm / 2) / bm;
        }
    }
    result = TRUE;
release:
    HIDD_BM_ReleasePixels(object, &map);
    return result;
    AROS_LIBFUNC_EXIT
}
