/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#ifndef BATTERY_EDITOR_H
#define BATTERY_EDITOR_H
#include <libraries/mui.h>
#define MUIM_BatteryEditor_Refresh (TAG_USER | 0x10580001)
struct BatteryEditor_DATA
{
    Object *status;
    struct Library *power;
    struct MsgPort *port;
    APTR subscription;
    struct MUI_InputHandlerNode handler, timer;
    BOOL handler_added;
};
#endif
