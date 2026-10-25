/*
    Copyright (C) 2026 John Knipper.
    Distributed under the AROS Public License, version 1.1.
*/
#ifndef DISPLAYSCALE_SNAPSHOT_H
#define DISPLAYSCALE_SNAPSHOT_H

#define SCALE_SNAPSHOT_CAPACITY 128
struct ScaleWindowSnapshot
{
    IPTR identity, port;
    LONG x, y, width, height, innerWidth, innerHeight;
    LONG minWidth, minHeight, maxWidth, maxHeight;
    UWORD border[4], canvasHeight;
};
struct ScaleSnapshot
{
    IPTR context;
    ULONG scale, factor, physicalFont, logicalFont, barHeight, count;
    LONG rasterWidth, rasterHeight;
    struct ScaleWindowSnapshot windows[SCALE_SNAPSHOT_CAPACITY];
};

/* Caller pins the public screen and allocates the result before entering.
 * Copy all window state under IBase, emit only after unlocking. No retained
 * client resource/Window pointer is dereferenced after the snapshot. A capacity
 * overflow refuses the entire observation, never a partial PASS. */
static BOOL CaptureScaleSnapshot(struct Screen *screen, struct ScaleSnapshot *sample)
{
    struct Window *window;
    const struct ScreenRenderContext *context;
    IPTR value = 0;
    ULONG lock = LockIBase(0);
    BOOL valid = FALSE;
    memset(sample, 0, sizeof(*sample));
    if (!GetAttr(SA_RenderContext, (Object *)screen, &value) || !value) goto done;
    context = (const struct ScreenRenderContext *)value;
    if (!context->src_LogicalDrawInfo || !context->src_PhysicalDrawInfo ||
        context->src_LogicalDrawInfo->dri_Screen != screen ||
        context->src_PhysicalDrawInfo->dri_Screen != screen ||
        !context->src_LogicalDrawInfo->dri_Font ||
        !context->src_PhysicalDrawInfo->dri_Font ||
        screen->RastPort.Font != context->src_PhysicalDrawInfo->dri_Font)
        goto done;
    sample->context = value;
    sample->scale = context->src_Scale;
    sample->factor = context->src_LegacyScaleFactor;
    sample->logicalFont = context->src_LogicalDrawInfo->dri_Font->tf_YSize;
    sample->physicalFont = context->src_PhysicalDrawInfo->dri_Font->tf_YSize;
    sample->barHeight = context->src_PhysicalBarHeight;
    sample->rasterWidth = screen->Width;
    sample->rasterHeight = screen->Height;
    for (window = screen->FirstWindow; window; window = window->NextWindow)
    {
        struct ScaleWindowSnapshot *row;
        if (sample->count == SCALE_SNAPSHOT_CAPACITY || !window->RPort) goto done;
        row = &sample->windows[sample->count++];
        row->identity = (IPTR)window;
        row->port = (IPTR)window->UserPort;
        row->x = window->LeftEdge; row->y = window->TopEdge;
        row->width = window->Width; row->height = window->Height;
        row->border[0] = window->BorderLeft; row->border[1] = window->BorderTop;
        row->border[2] = window->BorderRight; row->border[3] = window->BorderBottom;
        row->innerWidth = window->Width - window->BorderLeft - window->BorderRight;
        row->innerHeight = window->Height - window->BorderTop - window->BorderBottom;
        row->minWidth = window->MinWidth; row->minHeight = window->MinHeight;
        row->maxWidth = window->MaxWidth; row->maxHeight = window->MaxHeight;
        /* Client SetFont/CloseFont does not take IBase. Read the cached scalar
         * height; never follow the client-owned font or title pointers. */
        row->canvasHeight = window->RPort->TxHeight;
    }
    valid = TRUE;
done:
    UnlockIBase(lock);
    return valid;
}
#endif
