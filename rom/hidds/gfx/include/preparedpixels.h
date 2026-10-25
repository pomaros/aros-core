/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Author: John KNIPPER <code@jkn.me>
    SPDX-License-Identifier: APL-1.1
*/
#ifndef HIDD_PREPAREDPIXELS_H
#define HIDD_PREPAREDPIXELS_H

#include <exec/types.h>
#include <hidd/preparedcursor.h>

/* Audited, task-bound mapping for a frozen bitmap. Caller retains bitmap and
 * all Layers locks until ReleasePixels. Prepare/Release may dispatch and lock;
 * the interval between them permits raw copies only. No methods or callbacks
 * are needed to publish/restore either CPU storage or presentation storage.
 * Unknown driver subclasses must refuse instead of inheriting this capability.
 * Only 32-bit native-order truecolour is supported. Masks are raw native ULONG
 * masks, including 10-bit RGB. The cursor and its save-under are frozen until
 * release; the client prepares both replacements before any raw publication.
 */
struct HIDD_PixelMap
{
    UBYTE *pixels;
    ULONG width, height, stride, size;
    ULONG red, green, blue, alpha;
    UBYTE *presentation;       /* Optional direct scanout, same native format. */
    ULONG presentationStride, presentationSize;
    APTR driverCookie;        /* Opaque to clients; used only by paired release. */
    struct HIDD_CursorState *cursorState; /* Borrowed, frozen until release. */
    APTR cursorCookie;
    APTR cursorSemaphore;
    UBYTE *cursorBackup, *cursorARGB;
    ULONG cursorX, cursorY, cursorW, cursorH, cursorARGBStride;
    /* Written by a refusing PreparePixels or RenderPreparedCursor: the
     * HIDD_PIXELS_REFUSE_* cause, its values and the refusing class name (a
     * static string the caller copies before the next call). */
    ULONG refusal;
    LONG refusalValue[2];
    CONST_STRPTR refusalClass;
};

/* Zero is "the driver stated no cause". Values per cause: */
#define HIDD_PIXELS_REFUSE_NONE          0
#define HIDD_PIXELS_REFUSE_UNSUPPORTED   1  /* the class has no prepared mapping */
#define HIDD_PIXELS_REFUSE_SUBCLASS      2  /* a subclass of the audited class; it must declare itself */
#define HIDD_PIXELS_REFUSE_STORAGE       3  /* [0] bytes per pixel, [1] 1 if CPU storage exists */
#define HIDD_PIXELS_REFUSE_COLORMODEL    4  /* [0] vHidd_ColorModel_* */
#define HIDD_PIXELS_REFUSE_SWAPPED       5  /* byte-swapped pixel format */
#define HIDD_PIXELS_REFUSE_GEOMETRY      6  /* [0] stride, [1] height overflow 32 bits */
#define HIDD_PIXELS_REFUSE_STORAGE_SIZE  7  /* [0] storage bytes, [1] bytes the map needs */
#define HIDD_PIXELS_REFUSE_SCANOUT       8  /* [0] HIDD_SCANOUT_*, [1] the scanout's value */
#define HIDD_PIXELS_REFUSE_DISPLAY       9  /* a CursorFB owner that is not a Display */
#define HIDD_PIXELS_REFUSE_CURSOR        10 /* drawn pointer save-under: [0] bpp, [1] 1 if buffers exist */
#define HIDD_PIXELS_REFUSE_CURSOR_RENDER 11 /* [0] HIDD_CURSOR_REFUSE_* */
#define HIDD_PIXELS_REFUSE_FOREIGN       12 /* the map belongs to another lease */
/* HIDD_PIXELS_REFUSE_SCANOUT [0]: how the visible bitmap differs from scanout */
#define HIDD_SCANOUT_NO_FRAMEBUFFER 1
#define HIDD_SCANOUT_CALLBACK       2 /* presentation is a driver callback */
#define HIDD_SCANOUT_NOT_OWNED      3 /* the boot console still owns the framebuffer */
#define HIDD_SCANOUT_PANNED         4 /* [1] x offset (or y if x is 0) */
#define HIDD_SCANOUT_WIDTH          5 /* [1] scanout width */
#define HIDD_SCANOUT_HEIGHT         6 /* [1] scanout height */
#define HIDD_SCANOUT_DEPTH          7 /* [1] scanout bytes per pixel */
#define HIDD_SCANOUT_MASKS          8 /* [1] scanout red mask */
#define HIDD_SCANOUT_PITCH          9 /* [1] scanout bytes per line */
/* HIDD_PIXELS_REFUSE_CURSOR_RENDER [0] */
#define HIDD_CURSOR_REFUSE_LEASE    1 /* no cursor lease, state or staging */
#define HIDD_CURSOR_REFUSE_ALIAS    2 /* proposed resources alias the live pointer */
#define HIDD_CURSOR_REFUSE_TARGET   3 /* the live pointer targets another bitmap */
#define HIDD_CURSOR_REFUSE_SHAPE    4 /* proposed save-under missing or too small */
#define HIDD_CURSOR_REFUSE_RENDERER 5 /* the staging renderer could not be created */
#define HIDD_CURSOR_REFUSE_BACKUP   6 /* the frozen pointer has no save-under */
#endif
