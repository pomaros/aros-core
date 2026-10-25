/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef INTUITION_SCREENRENDERCONTEXT_H
#define INTUITION_SCREENRENDERCONTEXT_H
#include <exec/types.h>
#include <intuition/screens.h>
#include <intuition/classusr.h>
#include <intuition/scrdecorclass.h>
struct IntScreen;
struct IntuitionBase;
struct PreparedScreenRenderContext;
/* Caller pins screen/theme/font lifetime with IBase, then screen LayerInfo.
 * Preparation owns all font references, attributes, images and allocation.
 * Install/rollback only publish pointers; borrowed generations survive until
 * ScreenClose. Release drops only an unpublished losing generation. */
struct PreparedScreenRenderContext *PrepareScreenRenderContext(struct IntScreen *,
    struct TextFont *, struct TextFont *, ULONG, const struct ScreenScaleMetrics *,
    struct IntuitionBase *);
const struct ScreenRenderContext *PreparedScreenRenderContextView(
    const struct PreparedScreenRenderContext *);
const struct ScreenRenderContext *CurrentScreenRenderContext(struct Screen *);
const struct ScreenScaleMetrics *ScreenRenderContextLogicalMetrics(
    const struct ScreenRenderContext *);
BOOL ScreenRenderContextBorders(const struct ScreenRenderContext *, BOOL logical,
    LONG *left, LONG *top, LONG *right, LONG *bottom);
Object *ScreenRenderContextDepthImage(const struct ScreenRenderContext *);
void InstallScreenRenderContext(struct PreparedScreenRenderContext *, BOOL rollback);
void ReleaseScreenRenderContext(struct PreparedScreenRenderContext *, BOOL committed,
    struct IntuitionBase *);
BOOL InitializeScreenRenderContext(struct IntScreen *, struct IntuitionBase *);
void FreeScreenRenderContexts(struct IntScreen *, struct IntuitionBase *);
#endif
