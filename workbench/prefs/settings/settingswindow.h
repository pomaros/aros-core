#ifndef SETTINGSWINDOW_H
#define SETTINGSWINDOW_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#include <exec/types.h>
#include <libraries/mui.h>
#include "catalogue.h"

/*** Methods (private to the program) ***************************************/
#define MUIB_SettingsWindow              (TAG_USER | 0x10560000)
#define MUIM_SettingsWindow_Select       (MUIB_SettingsWindow | 0x01) /* sidebar */
#define MUIM_SettingsWindow_OpenRow      (MUIB_SettingsWindow | 0x02)
#define MUIM_SettingsWindow_Back         (MUIB_SettingsWindow | 0x03)
#define MUIM_SettingsWindow_Forward      (MUIB_SettingsWindow | 0x04)
#define MUIM_SettingsWindow_Search       (MUIB_SettingsWindow | 0x05)
#define MUIM_SettingsWindow_UpdateButtons (MUIB_SettingsWindow | 0x06)
struct MUIP_SettingsWindow_OpenRow { STACKED ULONG MethodID; STACKED IPTR row; };
/* The port's FIND: matching rows and gadgets, one per line, into buffer. */
#define MUIM_SettingsWindow_Level        (MUIB_SettingsWindow | 0x08) /* level or show-hidden changed */
/* The port's LEVEL: set the level by name; returns FALSE for an unknown name. */
#define MUIM_SettingsWindow_SetLevel     (MUIB_SettingsWindow | 0x09)
struct MUIP_SettingsWindow_SetLevel { STACKED ULONG MethodID; STACKED CONST_STRPTR name; };
#define MUIM_SettingsWindow_Find         (MUIB_SettingsWindow | 0x07)
/* "Settings as at boot": ENV: back to the snapshot taken at start-up. */
#define MUIM_SettingsWindow_BootUndo     (MUIB_SettingsWindow | 0x0a)
#define SETTINGS_BOOT_SNAPSHOT "RAM:Settings-At-Boot"
struct MUIP_SettingsWindow_Find { STACKED ULONG MethodID; STACKED CONST_STRPTR text;
    STACKED STRPTR buffer; STACKED ULONG size; };
/* CHECK: opens every row, indexes, searches for text and walks the levels,
 * one console line per finding; returns the number of failures. The
 * window is left showing the search and its first matching row. */
#define MUIM_SettingsWindow_Check        (MUIB_SettingsWindow | 0x0b)
struct MUIP_SettingsWindow_Check { STACKED ULONG MethodID; STACKED CONST_STRPTR text; };
/* SEARCH=: puts text into the search field, as typing it would. */
#define MUIM_SettingsWindow_SetSearch    (MUIB_SettingsWindow | 0x0c)
struct MUIP_SettingsWindow_SetSearch { STACKED ULONG MethodID; STACKED CONST_STRPTR text; };

/*** Attributes *************************************************************/
/* I: the loaded catalogue, owned by the caller and kept alive. */
#define MUIA_SettingsWindow_Catalogue    (MUIB_SettingsWindow | 0x80)
/* I: the class of the row to open first (PAGE=), or NULL. */
#define MUIA_SettingsWindow_Initial      (MUIB_SettingsWindow | 0x81)

extern struct MUI_CustomClass *SettingsWindow_CLASS;
#define SettingsWindowObject BOOPSIOBJMACRO_START(SettingsWindow_CLASS->mcc_Class)

#endif /* SETTINGSWINDOW_H */
