/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    This file is part of AROS, distributed under the AROS Public License 1.1.
*/
#ifndef INTUITION_MENUSCALE_H
#define INTUITION_MENUSCALE_H
#include <exec/types.h>
struct Window;
struct Menu;
struct MenuItem;
struct DrawInfo;
struct Image;
struct RastPort;
struct IntuitionBase;
struct ScreenRenderContext;
struct MenuScaleGeneration;
struct MenuScaleView
{
    const struct ScreenRenderContext *context;
    struct Menu *menu;
    struct DrawInfo *dri;
    struct Image *checkmark, *amigakey, *submenuimage;
    LONG width, height, barheight;
    UWORD barhborder, barvborder, menuhborder, menuvborder;
    UWORD factor, scale;
    BOOL sharp, logical, typedDecorator;
};
/* MenuLock keeps client links/strings/resources borrowed through disposal.
 * All geometry/text nodes are private. Context metadata is immutable and kept
 * alive by the owner Window's Screen lifetime. No diskfont or DOS calls. */
const struct ScreenRenderContext *CaptureMenuScaleContext(struct Window *, struct IntuitionBase *);
struct MenuScaleGeneration *PrepareMenuScale(struct Window *, struct IntuitionBase *);
const struct MenuScaleView *MenuScaleView(struct MenuScaleGeneration *);
void CommitMenuScaleState(struct MenuScaleGeneration *);
void DisposeMenuScale(struct MenuScaleGeneration *);
LONG MenuScalePoint(const struct MenuScaleView *, LONG);
LONG MenuScaleMouse(const struct MenuScaleView *, LONG);
BOOL MenuScaleDrawSeparator(struct MenuScaleGeneration *, struct MenuItem *,
    struct RastPort *, LONG, LONG, struct IntuitionBase *);
struct mdpGetScaleDefSizeSysImage;
struct mdpDrawScaleSysImage;
BOOL MenuScaleVectorSize(struct mdpGetScaleDefSizeSysImage *msg,
    struct IntuitionBase *IntuitionBase);
BOOL MenuScaleVectorDraw(struct mdpDrawScaleSysImage *msg,
    struct IntuitionBase *IntuitionBase);

#endif
