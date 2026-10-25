/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License, version 1.1.
*/
#ifndef INTUITION_WINDOWFIT_H
#define INTUITION_WINDOWFIT_H
#include <exec/types.h>
struct Window;
struct Screen;
struct IntuitionBase;
struct WindowFitTransaction;
struct ScreenScaleResult;
struct NewWindow;
struct ScreenRenderContext;
/* Exact current generation, measured physical outer minima; never canonical. */
struct WindowLayoutFit
{
    struct Window *window;
    const struct ScreenRenderContext *context;
    LONG minwidth, minheight;
};
struct ScaleWindowGeometry;
struct WindowFitFrameMetrics
{
    struct Window *window;
    LONG left, top, right, bottom;
};
struct WindowFitGeometry
{
    LONG left, top, width, height, relleft, reltop;
    LONG physical_left, physical_top, physical_width, physical_height;
};
#define WINDOWFIT_X 1
#define WINDOWFIT_Y 2
#define WINDOWFIT_WIDTH 4
#define WINDOWFIT_HEIGHT 8
BOOL WindowFitViewport(struct Window *, LONG *, LONG *);
BOOL WindowFitConstrain(struct Window *, LONG *, LONG *, LONG *, LONG *, struct IntuitionBase *);
BOOL WindowFitPrepareOpen(struct Window *, struct IntuitionBase *);
void WindowFitRecordChange(struct Window *, ULONG);
BOOL WindowFitInitializeGeometry(struct Window *, const struct ScaleWindowGeometry *,
    LONG, LONG, BOOL, struct NewWindow *);
void WindowFitRecordLimits(struct Window *, ULONG, BOOL);
BOOL WindowFitPreflight(struct Screen *, ULONG, const struct WindowFitFrameMetrics *,
    ULONG, const struct WindowLayoutFit *, struct WindowFitTransaction **, struct ScreenScaleResult *,
    struct IntuitionBase *);
BOOL WindowFitApply(struct WindowFitTransaction *, BOOL, struct IntuitionBase *);
ULONG WindowFitCount(const struct WindowFitTransaction *);
BOOL WindowFitDesired(const struct WindowFitTransaction *, ULONG, struct Window **,
    struct WindowFitGeometry *);
void WindowFitInstall(struct WindowFitTransaction *, BOOL);
void WindowFitFinish(struct WindowFitTransaction *, BOOL, struct IntuitionBase *);
#endif
