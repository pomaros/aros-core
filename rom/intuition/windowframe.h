#ifndef INTUITION_WINDOWFRAME_H
#define INTUITION_WINDOWFRAME_H
/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Distributed under the AROS Public License (APL) Version 1.1. */
#include <exec/types.h>
struct Window;
struct Screen;
struct ScreenRenderContext;
struct IntuitionBase;
struct IntDrawInfo;
struct ScreenScaleResult;
BOOL WindowFrameBaseBorders(struct Window *, LONG *, LONG *, LONG *, LONG *);
ULONG WindowFrameScale(struct Window *);
LONG WindowFrameTitleBorder(struct Window *);
const struct ScreenRenderContext *WindowFrameContext(struct Window *);
BOOL InitializeWindowFrame(struct Window *, struct Screen *,
    const struct ScreenRenderContext *, struct IntuitionBase *);
void AdoptWindowFrameBuffer(struct Window *, BOOL);
BOOL PrepareInitialWindowFrameLayout(struct Window *, struct IntuitionBase *);
void CaptureWindowFrameSystemImages(struct Window *);
void RememberWindowFrameBorders(struct Window *);
void CaptureWindowFrameClientBorders(struct Window *);
BOOL WindowFrameOwnsImage(struct Window *, const void *);
/* A NULL result is allowed; otherwise a refusal names its exact check. */
APTR CreateWindowFrameImage(struct Window *, const struct ScreenRenderContext *,
    struct IntDrawInfo *, ULONG, LONG, struct ScreenScaleResult *,
    struct IntuitionBase *);
struct PreparedWindowFrame;
/* Preparing never changes the Window, its gadgets, or the current generation.
 * A committed generation and all images/font references live until WindowClose.
 * Install and rollback are pointer/scalar copies only. */
struct PreparedWindowFrame *PrepareWindowFrame(struct Window *,
    const struct ScreenRenderContext *, struct ScreenScaleResult *,
    struct IntuitionBase *);
BOOL WindowFrameBorders(const struct PreparedWindowFrame *,
    LONG *, LONG *, LONG *, LONG *);
BOOL PrepareWindowFrameLayout(struct PreparedWindowFrame *, LONG, LONG,
    struct ScreenScaleResult *, struct IntuitionBase *);
void InstallWindowFrame(struct PreparedWindowFrame *, BOOL);
void ReleaseWindowFrame(struct PreparedWindowFrame *, BOOL,
    struct IntuitionBase *);
void FreeWindowFrames(struct Window *, struct IntuitionBase *);
struct IntDrawInfo *WindowFrameDrawInfo(struct Window *);
struct IntDrawInfo *PreparedWindowFrameDrawInfo(const struct PreparedWindowFrame *);
IPTR PreparedWindowFrameUserBuffer(const struct PreparedWindowFrame *);
#endif
