/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Author: John KNIPPER <code@jkn.me>
    SPDX-License-Identifier: APL-1.1
*/
#ifndef LAYERS_PREPAREDPIXELS_H
#define LAYERS_PREPAREDPIXELS_H
#include <exec/types.h>
struct BitMap;
struct PreparedPixels;
struct HIDD_PixelMap;
struct HIDD_CursorState;
struct LayerGeometryRefusal;
/* Components are high-aligned 16-bit values: 10-bit channels retain all bits. */
struct PreparedPixel; /* Defined by public graphics/layertransaction.h. */
/* Caller freezes bitmap lifetime, geometry and drawing until release. Prepare
 * captures immutable raw old bytes and, if writable, a separate new staging
 * raster. Failure does not publish bytes. Commit/Restore ONLY raw-copy mapped
 * storage: no allocation, dispatch, lock or hook. Release unlocks/frees only.
 * Seal is required after final staged writes; it precomposes the cursor.
 * refusal (optional, borrowed until release) receives LGR_PIXELS with kind
 * as lgr_Pixels from a refusing prepare, read, write or seal of this lease;
 * CopyPreparedPixels, a format probe, never writes it. */
struct PreparedPixels *PreparePixels(struct BitMap *bitmap, BOOL writable,
    struct LayerGeometryRefusal *refusal, ULONG kind);
ULONG PreparedPixelsWidth(const struct PreparedPixels *pixels);
ULONG PreparedPixelsHeight(const struct PreparedPixels *pixels);
/* Decode the bitmap's current pen into this mapped format during prepare. */
BOOL PreparedPenColor(const struct PreparedPixels *, struct BitMap *, ULONG pen,
    struct PreparedPixel *);
BOOL ReadOldPixels(const struct PreparedPixels *, ULONG x, ULONG y,
    ULONG width, ULONG height, struct PreparedPixel *, ULONG strideBytes);
BOOL ReadStagedPixels(const struct PreparedPixels *, ULONG x, ULONG y,
    ULONG width, ULONG height, struct PreparedPixel *, ULONG strideBytes);
BOOL WritePreparedPixels(struct PreparedPixels *, ULONG x, ULONG y,
    ULONG width, ULONG height, const struct PreparedPixel *, ULONG strideBytes);
/* Raw copy requires identical masks. Overlap within a staging raster is safe. */
BOOL CopyPreparedPixels(struct PreparedPixels *dst, ULONG dx, ULONG dy,
    const struct PreparedPixels *src, ULONG sx, ULONG sy,
    ULONG width, ULONG height, BOOL stagedSource);
/* Pre-Seal registration. Map is borrowed until lease release; NULL refuses
 * without changing the existing binding. Caller retains proposed ownership. */
struct HIDD_PixelMap *PreparePixelsCursor(struct PreparedPixels *, struct HIDD_CursorState *);
BOOL SealPreparedPixels(struct PreparedPixels *);
VOID CommitPreparedPixels(struct PreparedPixels *);
VOID RestorePreparedPixels(struct PreparedPixels *);
VOID ReleasePreparedPixels(struct PreparedPixels *);
#endif
