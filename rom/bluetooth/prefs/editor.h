/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#ifndef BLUETOOTH_EDITOR_H
#define BLUETOOTH_EDITOR_H
#include <libraries/mui.h>
#define MUIM_BluetoothEditor_Sync (TAG_USER | 0x105a0001)
struct BluetoothEditor_DATA {
    Object *panel;
    IPTR baseline;
    APTR snapshot;
    struct MUI_InputHandlerNode timer;
    BOOL timer_added;
};
#endif
