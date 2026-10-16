/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
   Distributed under the AROS Public License, version 1.1. */
#include <libraries/bluetooth.h>
#include <proto/bluetooth.h>
#include <proto/dos.h>
#include <dos/dostags.h>
#include <stdio.h>
#include <string.h>
#include "bluetoothprefs.h"

BOOL LaunchBluetoothAction(APTR device, const char *action)
{
    STRPTR address = NULL;
    char copy[18], command[128];
    unsigned i;
    if (!device) return FALSE;
    btLockReadBase();
    btGetAttrs(BGA_DEVICE, device, BDA_AddressString, &address, TAG_END);
    if (!address || strlen((char *)address) != 17) { btUnlockBase(); return FALSE; }
    memcpy(copy, address, sizeof(copy));
    btUnlockBase();
    /* The only interpolated data is a canonical device address. */
    for (i = 0; i < 17; i++)
        if (i % 3 == 2 ? copy[i] != ':' : !((copy[i] >= '0' && copy[i] <= '9') ||
            (copy[i] >= 'A' && copy[i] <= 'F') || (copy[i] >= 'a' && copy[i] <= 'f'))) return FALSE;
    snprintf(command, sizeof(command), "SYS:System/BluetoothAction %s %s", copy, action);
    return SystemTags((CONST_STRPTR)command, SYS_Asynch, TRUE,
        SYS_Input, (IPTR)BNULL, SYS_Output, (IPTR)BNULL, TAG_DONE) != -1;
}
