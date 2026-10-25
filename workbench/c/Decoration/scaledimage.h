#ifndef DECORATION_SCALEDIMAGE_H
#define DECORATION_SCALEDIMAGE_H
/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Distributed under the AROS Public License (APL) Version 1.1. */
/* Scale each state independently so rounding cannot move pixels across a
 * normal/selected/inactive boundary. All output storage is privately owned. */
static struct DecorImage *checked_scale_image_cells(const struct DecorImage *source,
    ULONG cellWidth, ULONG cellHeight)
{
    struct DecorImage *image;
    ULONG cols, rows, sourceWidth, sourceHeight, x, y, col, row;
    UQUAD width, height;
    if (!source || !source->ok || !source->data || !source->w || !source->h ||
        !cellWidth || !cellHeight) return NULL;
    cols = source->subimagescols; rows = source->subimagesrows;
    if (!cols || !rows || cols > 64 || rows > 64 ||
        source->w % cols || source->h % rows ||
        (UQUAD)source->w * source->h * sizeof(ULONG) > 0x7fffffff) return NULL;
    sourceWidth = source->w / cols; sourceHeight = source->h / rows;
    width = (UQUAD)cellWidth * cols; height = (UQUAD)cellHeight * rows;
    if (width > 65535 || height > 65535 || width * height * sizeof(ULONG) > 0x7fffffff)
        return NULL;
    image = DNewDecorImageContainer(width, height);
    if (!image) return NULL;
    image->subimagescols = cols; image->subimagesrows = rows;
    for (row = 0; row < rows; ++row)
        for (col = 0; col < cols; ++col)
            for (y = 0; y < cellHeight; ++y)
                for (x = 0; x < cellWidth; ++x)
                {
                    ULONG sx = col * sourceWidth + (UQUAD)x * sourceWidth / cellWidth;
                    ULONG sy = row * sourceHeight + (UQUAD)y * sourceHeight / cellHeight;
                    image->data[(row * cellHeight + y) * image->w + col * cellWidth + x] =
                        source->data[sy * source->w + sx];
                }
    image->ok = TRUE;
    return image;
}
#endif
