/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License, version 1.1.
*/
#ifndef INTUITION_WINDOWTITLE_H
#define INTUITION_WINDOWTITLE_H
/* IntWindow.titlebuffer is Intuition's own bounded copy of Window->Title.
 * The borrowed client string is read only by the client's own OpenWindow or
 * SetWindowTitles call. Before publication the window is private; afterwards
 * the copy changes only under the screen's LayerInfo lock, which every
 * titlebuffer reader outside the window's own task holds. */
static inline BOOL WindowTitleUnchanged(struct Window *window, CONST_STRPTR title)
{
    ULONG i;
    if (!title) return FALSE;
    /* A copy truncated at TITLEBUFFERLEN never matches a longer title. */
    for (i = 0; i <= TITLEBUFFERLEN; ++i)
    {
        if (title[i] != (UBYTE)IW(window)->titlebuffer[i]) return FALSE;
        if (!title[i]) return TRUE;
    }
    return FALSE;
}
static inline void WindowTitleCopy(struct Window *window, CONST_STRPTR title)
{
    ULONG i = 0;
    if (title)
        for (; i < TITLEBUFFERLEN && title[i]; ++i)
            IW(window)->titlebuffer[i] = title[i];
    IW(window)->titlebuffer[i] = 0;
}
#endif
