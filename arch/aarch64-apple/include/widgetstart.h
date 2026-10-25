/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 *
 * When a native widget started from the Startup-Sequence may open its
 * window. The kernel launcher accepts the desktop only while Wanderer's
 * backdrop is the newest window of the Workbench screen, 64 observations in
 * a row (coldstart_proof.c run_wanderer); it then opens the Shell and sets
 * the global variable WIDGET_DESKTOP_VAR. A widget window opened before that
 * is the newest instead, the launcher gives up and returns to the loader,
 * and every task stops behind the painted desktop. A widget therefore opens
 * once the variable exists, or after SECONDS without it. Its place stays its
 * own widgetplace.h slot: this is only when, never where.
 * Uses intuition.library and dos.library; the program opens both.
 */
#ifndef APPLE_WIDGETSTART_H
#define APPLE_WIDGETSTART_H

#define WIDGET_DESKTOP_VAR "Sys/DesktopAccepted"

#ifndef WIDGET_DESKTOP_VAR_ONLY
/* The Workbench screen, locked, once the desktop is accepted; after
 * SECONDS without it (or on CTRL-C) the screen as found, which may be NULL.
 * The screen is locked once only. */
static inline struct Screen *WidgetWaitForDesktop(LONG seconds)
{
    struct Screen *screen = NULL;
    LONG second;
    char value[4];

    for (second = 0; second < seconds; ++second) {
        if (!screen)
            screen = LockPubScreen("Workbench");
        if (screen && GetVar(WIDGET_DESKTOP_VAR, value, sizeof(value), GVF_GLOBAL_ONLY) > 0)
            return screen;
        if (SetSignal(0, 0) & SIGBREAKF_CTRL_C)
            break;
        Delay(50);
    }
    return screen;
}
#endif
#endif
