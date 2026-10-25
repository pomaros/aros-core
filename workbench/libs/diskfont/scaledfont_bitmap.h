/*
 * Copyright (C) 2026, John KNIPPER <code@jkn.me>.
 * Licensed under the AROS Public License, version 1.1.
 * Monochrome nearest-neighbour scaling. Shared with the host sanitizer fixture.
 */
#ifndef DISKFONT_SCALEDFONT_BITMAP_H
#define DISKFONT_SCALEDFONT_BITMAP_H

static long long ScaleFontMetric(LONG value, ULONG height, ULONG sourceheight)
{
    if (value < 0)
        return -((-(long long)value * height + sourceheight / 2) / sourceheight);
    return ((long long)value * height + sourceheight / 2) / sourceheight;
}

/* A NULL storage validates and measures without reading bitmap pixels. */
static BOOL ScaleBitmapFont(const struct TextFont *source, UWORD height,
                           struct TextFont *target, UBYTE *storage)
{
    ULONG count, i, bits = 0, sourcebits, row, x;
    ULONG *locations;
    WORD *spaces, *kerns;
    UBYTE *pixels;
    long long metric;

    if (!source || !height || !source->tf_YSize ||
        source->tf_HiChar < source->tf_LoChar ||
        source->tf_Baseline >= source->tf_YSize ||
        (source->tf_Style & FSF_COLORFONT) ||
        !source->tf_CharLoc || !source->tf_CharData || !source->tf_Modulo)
        return FALSE;
    count = (ULONG)source->tf_HiChar - source->tf_LoChar + 2;
    sourcebits = (ULONG)source->tf_Modulo * 8;
    locations = (ULONG *)storage;
    spaces = storage ? (WORD *)(storage + count * sizeof(ULONG)) : NULL;
    kerns = storage ? spaces + count : NULL;
    pixels = storage ? (UBYTE *)(kerns + count) : NULL;
    for (i = 0; i < count; ++i)
    {
        ULONG loc = ((const ULONG *)source->tf_CharLoc)[i];
        ULONG offset = loc >> 16, width = loc & 65535;
        ULONG scaled = ScaleFontMetric(width, height, source->tf_YSize);
        if (offset + width > sourcebits || scaled > 65535 || bits + scaled > 65535)
            return FALSE;
        if (storage) locations[i] = (bits << 16) | scaled;
        bits += scaled;
        metric = ScaleFontMetric(source->tf_CharSpace ?
            ((const WORD *)source->tf_CharSpace)[i] : source->tf_XSize,
            height, source->tf_YSize);
        if (metric < -32768 || metric > 32767) return FALSE;
        if (storage) spaces[i] = metric;
        metric = ScaleFontMetric(source->tf_CharKern ?
            ((const WORD *)source->tf_CharKern)[i] : 0,
            height, source->tf_YSize);
        if (metric < -32768 || metric > 32767) return FALSE;
        if (storage) kerns[i] = metric;
    }
    target->tf_Modulo = ((bits + 15) & ~15UL) / 8;
    /* Even a font consisting of empty glyphs needs a valid bitmap. */
    if (!target->tf_Modulo) target->tf_Modulo = 2;
    metric = ScaleFontMetric(source->tf_XSize, height, source->tf_YSize);
    if (metric > 65535) return FALSE;
    target->tf_XSize = metric;
    target->tf_YSize = height;
    target->tf_Baseline = ScaleFontMetric(source->tf_Baseline, height, source->tf_YSize);
    if (target->tf_Baseline >= height) target->tf_Baseline = height - 1;
    target->tf_BoldSmear = ScaleFontMetric(source->tf_BoldSmear, height, source->tf_YSize);
    if (ScaleFontMetric(source->tf_BoldSmear, height, source->tf_YSize) > 65535)
        return FALSE;
    target->tf_LoChar = source->tf_LoChar;
    target->tf_HiChar = source->tf_HiChar;
    target->tf_Style = source->tf_Style & ~FSF_TAGGED;
    target->tf_Flags = source->tf_Flags & ~(FPF_ROMFONT | FPF_DISKFONT | FPF_DESIGNED | FPF_REMOVED);
    if (!storage) return TRUE;
    target->tf_CharLoc = locations;
    target->tf_CharSpace = spaces;
    target->tf_CharKern = kerns;
    target->tf_CharData = pixels;
    for (i = 0; i < count; ++i)
    {
        ULONG loc = ((const ULONG *)source->tf_CharLoc)[i];
        ULONG offset = loc >> 16, width = loc & 65535;
        ULONG destoffset = locations[i] >> 16, destwidth = locations[i] & 65535;
        for (row = 0; row < height; ++row)
        {
            ULONG sourcerow = row * source->tf_YSize / height;
            for (x = 0; x < destwidth; ++x)
            {
                ULONG sourcex = offset + x * width / destwidth;
                ULONG destx = destoffset + x;
                if (((const UBYTE *)source->tf_CharData)[sourcerow * source->tf_Modulo + sourcex / 8] & (128 >> (sourcex & 7)))
                    pixels[row * target->tf_Modulo + destx / 8] |= 128 >> (destx & 7);
            }
        }
    }
    return TRUE;
}
#endif
