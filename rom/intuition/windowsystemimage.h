#ifndef INTUITION_WINDOWSYSTEMIMAGE_H
#define INTUITION_WINDOWSYSTEMIMAGE_H
/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Distributed under the AROS Public License (APL) Version 1.1. */
#include <exec/types.h>
struct wdpGetScaleDefSizeSysImage;
struct wdpDrawScaleSysImage;
struct IntuitionBase;
BOOL WindowScaleVectorSize(struct wdpGetScaleDefSizeSysImage *, struct IntuitionBase *);
BOOL WindowScaleVectorDraw(struct wdpDrawScaleSysImage *, struct IntuitionBase *);
#endif
