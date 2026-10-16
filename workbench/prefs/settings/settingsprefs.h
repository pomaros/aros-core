#ifndef SETTINGSPREFS_H
#define SETTINGSPREFS_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    SYS/settings.prefs: the settings window's own settings, one SETW chunk,
    described by SYS:Prefs/Schemas/settings.schema and kept through
    settings.library like any other setting.
*/
#include <exec/types.h>

#define ID_SETW MAKE_ID('S','E','T','W')

struct SettingsWindowPrefs
{
    UBYTE swp_Level;        /* 0 novice, 1 average, 2 expert */
    UBYTE swp_ShowHidden;   /* 1: settings above the level shown, marked */
    UBYTE swp_Reserved[2];
};

#endif /* SETTINGSPREFS_H */
