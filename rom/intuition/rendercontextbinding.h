/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef INTUITION_RENDERCONTEXTBINDING_H
#define INTUITION_RENDERCONTEXTBINDING_H

/* Pure producer111 getter. Caller holds IBase, then screen LayerInfo, in that
 * order. This getter neither allocates nor advances a generation. */
const struct ScreenRenderContext *CurrentScreenRenderContext(struct Screen *screen);

static inline const struct ScreenRenderContext *WindowBoundRenderContext(struct Window *window)
{
    return IW(window)->ExpectedRenderContext;
}

/* An expected client layout snapshot never follows the current native frame
 * automatically. Its nested resources remain valid until ScreenClose. */
static inline BOOL BoundScreenRenderContext(const struct ScreenRenderContext *expected,
    struct Screen *screen)
{
    return expected && expected->src_LogicalDrawInfo &&
        expected->src_PhysicalDrawInfo &&
        expected->src_LogicalDrawInfo->dri_Screen == screen &&
        expected->src_PhysicalDrawInfo->dri_Screen == screen &&
        CurrentScreenRenderContext(screen) == expected;
}
#endif
