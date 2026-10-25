/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef INTUITION_VISUALOVERLAY_H
#define INTUITION_VISUALOVERLAY_H

/* Visual-only scene members have normal smart backing and move/close refresh,
 * but never take part in input, activation, parenting or client callbacks.
 * Reject incompatible requests before allocating any native resources. */
static BOOL ValidVisualOverlay(const struct NewWindow *nw, BOOL aware,
    BOOL hidden, const struct Layer *parent, const struct Region *shape,
    const struct Hook *shapehook, BOOL tileoverlay)
{
    const ULONG required = WFLG_BORDERLESS | WFLG_TOOLBOX;
    const ULONG forbidden = WFLG_ACTIVATE | WFLG_GIMMEZEROZERO |
        WFLG_BACKDROP | WFLG_REFRESHBITS | WFLG_SIZEGADGET |
        WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET |
        WFLG_REPORTMOUSE | WFLG_WINDOWACTIVE;
    return aware && hidden && !parent && !shape && !shapehook &&
        !tileoverlay && !nw->IDCMPFlags && !nw->FirstGadget &&
        (nw->Flags & required) == required && !(nw->Flags & forbidden);
}
#endif
