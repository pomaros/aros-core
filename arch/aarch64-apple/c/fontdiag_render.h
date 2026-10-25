/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#ifndef FONTDIAG_RENDER_H
#define FONTDIAG_RENDER_H

struct FontDiagRender
{
    const struct ScreenRenderContext *context;
    struct TextFont *raster;
    struct TextAttr logical;
};

/* The caller holds the public-screen visitor pin through reporting. IBase
 * serializes the raw raster/font fields with context publication. Resources
 * referenced by a published generation stay alive until ScreenClose. */
static BOOL FontDiag_CaptureRender(struct Screen *screen, struct FontDiagRender *sample)
{
    IPTR value = 0;
    ULONG lock = LockIBase(0);
    BOOL valid = FALSE;
    const struct ScreenRenderContext *context;

    memset(sample, 0, sizeof(*sample));
    if (GetAttr(SA_RenderContext, (Object *)screen, &value) && value)
    {
        context = (const struct ScreenRenderContext *)value;
        if (context->src_LogicalDrawInfo && context->src_PhysicalDrawInfo &&
            context->src_LogicalDrawInfo->dri_Screen == screen &&
            context->src_PhysicalDrawInfo->dri_Screen == screen &&
            context->src_LogicalDrawInfo->dri_Font &&
            context->src_PhysicalDrawInfo->dri_Font && screen->Font &&
            screen->RastPort.Font == context->src_PhysicalDrawInfo->dri_Font &&
            screen->Font->ta_YSize == context->src_BaseFont.tta_YSize)
        {
            sample->context = context;
            sample->raster = screen->RastPort.Font;
            sample->logical = *screen->Font;
            valid = TRUE;
        }
    }
    UnlockIBase(lock);
    return valid;
}
#endif
