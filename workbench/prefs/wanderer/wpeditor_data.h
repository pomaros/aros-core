#ifndef WPEDITOR_DATA_H
#define WPEDITOR_DATA_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    WPEditor's instance data, shared by the program's class and WandererEditor.mcc.
*/
#ifndef WPEDITOR_DATA_H_IN_SOURCE
/* The module dispatcher needs these; the class source has them. */
#include <exec/types.h>
#include <utility/tagitem.h>
#include <libraries/mui.h>
#include <zune/customclasses.h>
#include <zune/prefseditor.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/muimaster.h>
#include <proto/dos.h>
#include <proto/iffparse.h>
#include <prefs/prefhdr.h>
#include <prefs/wanderer.h>
#include <clib/alib_protos.h>
#include <stdio.h>
#include <string.h>
#include "locale.h"
#include "wpeditor.h"
#include "entryelements.h"
#include "../../system/Wanderer/wandererprefs.h"
#include "../../system/Wanderer/Classes/iconlist_attributes.h"
#include "../../system/Wanderer/iconwindow_attributes.h"
#endif

struct WPEditor_DATA
{
    struct WPEditor_AdvancedBackgroundWindow_DATA       *wped_AdvancedViewSettings_WindowData;

    struct WPEditor_ViewSettingsObject                  *wped_ViewSettings_Current;

    Object                                              *wped_FirstBGImSpecObj,
                                                        *wped_FirstBGAdvancedObj;

    ULONG                                               wped_DimensionsSet;

    Object                                              *wped_ViewSettings_GroupObj,
                                                        *wped_ViewSettings_SpacerObj,
                                                        *wped_c_NavigationMethod,
                                                        *wped_cm_ToolbarEnabled,
                                                        *wped_IconDropMode,
#if defined(DEBUG_CHANGESCREENTITLE)
                                                        *wped_s_screentitle,
#endif
                                                        *wped_toolbarpreview,
#if defined(DEBUG_NETWORKBROWSER)
                                                        *wped_cm_EnableNetworkBrowser,
#endif
#if defined(DEBUG_SHOWUSERFILES)
                                                        *wped_cm_EnableUserFiles,
#endif
                                                        *wped_toolbarGroup;
    struct Hook                                         wped_Hook_CloseAdvancedOptions;
};

#endif
