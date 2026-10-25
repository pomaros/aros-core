#ifndef INTUITION_WINDOWSCALE_H
#define INTUITION_WINDOWSCALE_H
/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <exec/types.h>
struct IntScreen;
struct TextFont;
struct IntuitionBase;
struct Window;
struct WindowScaleShapes;
struct WindowLayoutFit;
struct ScreenScaleResult;
BOOL ApplyPreparedScreenScale(struct IntScreen *, struct TextFont *,
    struct TextFont *, ULONG, const struct WindowLayoutFit *, struct ScreenScaleResult *, struct IntuitionBase *);
void FreeWindowScaleShapes(struct Window *, struct IntuitionBase *);
#endif
