/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 *
 * Where the native widgets sit on the Workbench, in the screen's logical
 * space (100 % units): Energy against the right edge, the status widget
 * left of it and Magnifier below the status widget, never overlapping, and
 * together never wider than three fifths of the screen, so that the desktop,
 * its icons and the Shell keep the rest at every scale.
 * Each widget derives its own slot from the screen alone, so the three
 * agree whatever order they re-place themselves in after a scale change.
 * A widget the user moved or sized keeps the user's geometry instead and
 * Intuition fits it to each scale.
 */
#ifndef APPLE_WIDGETPLACE_H
#define APPLE_WIDGETPLACE_H

#define WIDGET_GAP              8
#define WIDGET_ENERGY_WIDTH     792 /* Energy's 772-unit page in its frame */
#define WIDGET_COLUMN_WIDTH     632 /* the status widget and Magnifier */
#define WIDGET_COLUMN_MINIMUM   316
#define WIDGET_SHARE_NUMERATOR  3   /* of the screen width, at most */
#define WIDGET_SHARE_DENOMINATOR 5
#define WIDGET_ALIVE_HEIGHT     523
#define WIDGET_MAGNIFIER_HEIGHT 545

#define WIDGET_SLOT_ALIVE       0
#define WIDGET_SLOT_MAGNIFIER   1
#define WIDGET_SLOT_ENERGY      2

struct WidgetRect {
    LONG Left, Top, Width, Height;
};

/* The slot of one widget on a WIDTH x HEIGHT logical screen whose windows
 * start at TOP. The widgets share at most three fifths of the width. Energy
 * keeps its page width while the column keeps at least its minimum in that
 * share; in a narrower share both shrink in proportion and scroll.
 * The column's two heights shrink in proportion likewise. */
static inline BOOL WidgetSlot(ULONG slot, LONG width, LONG height, LONG top,
    struct WidgetRect *r)
{
    LONG room = width - 2 * WIDGET_GAP, tall = height - top;
    LONG energy = WIDGET_ENERGY_WIDTH, column = WIDGET_COLUMN_WIDTH;
    LONG alive = WIDGET_ALIVE_HEIGHT, magnifier = WIDGET_MAGNIFIER_HEIGHT;

    if (slot > WIDGET_SLOT_ENERGY || top < 0 || room < 2 || room > 65535 ||
        tall < WIDGET_GAP + 2 || tall > 65535)
        return FALSE;
    if (room > width * WIDGET_SHARE_NUMERATOR / WIDGET_SHARE_DENOMINATOR)
        room = width * WIDGET_SHARE_NUMERATOR / WIDGET_SHARE_DENOMINATOR;
    if (energy + column > room) {
        column = room - energy;
        if (column < WIDGET_COLUMN_MINIMUM) {
            column = room * WIDGET_COLUMN_WIDTH /
                (WIDGET_COLUMN_WIDTH + WIDGET_ENERGY_WIDTH);
            energy = room - column;
        }
    }
    if (alive + WIDGET_GAP + magnifier > tall) {
        alive = (tall - WIDGET_GAP) * WIDGET_ALIVE_HEIGHT /
            (WIDGET_ALIVE_HEIGHT + WIDGET_MAGNIFIER_HEIGHT);
        magnifier = tall - WIDGET_GAP - alive;
    }
    r->Top = top;
    if (slot == WIDGET_SLOT_ENERGY) {
        r->Left = width - WIDGET_GAP - energy;
        r->Width = energy;
        r->Height = tall;
    } else {
        r->Left = width - 2 * WIDGET_GAP - energy - column;
        r->Width = column;
        r->Height = alive;
        if (slot == WIDGET_SLOT_MAGNIFIER) {
            r->Top = top + alive + WIDGET_GAP;
            r->Height = magnifier;
        }
    }
    return r->Left >= 0 && r->Width > 0 && r->Height > 0;
}

/* The slot on SCREEN at CONTEXT: its logical extent, below the screen bar. */
static inline BOOL WidgetScreenSlot(const struct Screen *screen,
    const struct ScreenRenderContext *context, ULONG slot, struct WidgetRect *r)
{
    LONG scale = context ? context->src_Scale : 0;

    if (!screen || scale < 100 || scale > 400)
        return FALSE;
    return WidgetSlot(slot, (LONG)screen->Width * 100 / scale,
        (LONG)screen->Height * 100 / scale,
        ((LONG)context->src_PhysicalBarHeight * 100 + scale - 1) / scale + 2, r);
}

struct WidgetPlace {
    BOOL Automatic;
    struct WidgetRect Logical;      /* the user's geometry, 100 % units */
    LONG Left, Top, Width, Height;  /* physical, as last opened or seen */
};

static inline void WidgetPlaceAutomatic(struct WidgetPlace *p)
{
    p->Automatic = TRUE;
    p->Logical.Left = p->Logical.Top = p->Logical.Width = p->Logical.Height = 0;
    p->Left = p->Top = p->Width = p->Height = -1;
}

/* An explicit place, 100 % units; a size of 0 is the widget's own default. */
static inline void WidgetPlaceUser(struct WidgetPlace *p, LONG left, LONG top,
    LONG width, LONG height)
{
    WidgetPlaceAutomatic(p);
    p->Automatic = FALSE;
    p->Logical.Left = left;
    p->Logical.Top = top;
    p->Logical.Width = width;
    p->Logical.Height = height;
}

static inline void WidgetPlaceCopy(struct WidgetPlace *to, const struct WidgetPlace *from)
{
    to->Automatic = from->Automatic;
    to->Logical.Left = from->Logical.Left;
    to->Logical.Top = from->Logical.Top;
    to->Logical.Width = from->Logical.Width;
    to->Logical.Height = from->Logical.Height;
    to->Left = from->Left;
    to->Top = from->Top;
    to->Width = from->Width;
    to->Height = from->Height;
}

/* The rectangle a new window of SLOT opens at, 100 % units. */
static inline BOOL WidgetPlaceRect(const struct WidgetPlace *p,
    const struct Screen *screen, const struct ScreenRenderContext *context,
    ULONG slot, struct WidgetRect *r)
{
    if (p->Automatic)
        return WidgetScreenSlot(screen, context, slot, r);
    r->Left = p->Logical.Left;
    r->Top = p->Logical.Top;
    r->Width = p->Logical.Width;
    r->Height = p->Logical.Height;
    return TRUE;
}

/* The window as opened: later differences at this scale are the user's. */
static inline void WidgetPlaceOpened(struct WidgetPlace *p, const struct Window *w)
{
    p->Left = w->LeftEdge;
    p->Top = w->TopEdge;
    p->Width = w->Width;
    p->Height = w->Height;
}

/* A move or resize at an unchanged scale. Once the user has moved or sized
 * the window, its geometry is the user's at every later scale. */
static inline void WidgetPlaceChanged(struct WidgetPlace *p, const struct Window *w,
    ULONG scale)
{
    LONG half = (LONG)scale / 2;

    if (scale < 100 || (w->LeftEdge == p->Left && w->TopEdge == p->Top &&
        w->Width == p->Width && w->Height == p->Height))
        return;
    p->Automatic = FALSE;
    p->Logical.Left = ((LONG)w->LeftEdge * 100 + half) / (LONG)scale;
    p->Logical.Top = ((LONG)w->TopEdge * 100 + half) / (LONG)scale;
    p->Logical.Width = ((LONG)w->Width * 100 + half) / (LONG)scale;
    p->Logical.Height = ((LONG)w->Height * 100 + half) / (LONG)scale;
    WidgetPlaceOpened(p, w);
}
#endif
