/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
   Distributed under the AROS Public License, version 1.1.
   Own executable segment: a page may close while connect/pair is pending. */
#include <libraries/bluetooth.h>
#include <proto/bluetooth.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>
struct Library *BluetoothBase;

int main(int argc, char **argv)
{
    struct List *hardware, *devices;
    struct Node *radio, *device = NULL, *candidate;
    BOOL ok = FALSE;
    if (argc != 3) return 20;
    BluetoothBase = OpenLibrary((CONST_STRPTR)"bluetooth.library", 1);
    if (!BluetoothBase) return 20;
    btLockReadBase();
    btGetAttrs(BGA_STACK, NULL, BSA_HardwareList, &hardware, TAG_END);
    for (radio = hardware->lh_Head; radio->ln_Succ && !device; radio = radio->ln_Succ)
    {
        btGetAttrs(BGA_HARDWARE, radio, BHA_DeviceList, &devices, TAG_END);
        for (candidate = devices->lh_Head; candidate->ln_Succ; candidate = candidate->ln_Succ)
        {
            STRPTR address = NULL;
            btGetAttrs(BGA_DEVICE, candidate, BDA_AddressString, &address, TAG_END);
            if (address && !strcmp((char *)address, argv[1])) { device = candidate; break; }
        }
    }
    btUnlockBase();
    if (device)
    {
        if (!strcmp(argv[2], "connectpair"))
        {
            if (btConnectDevice(device))
            {
                ok = btPairDevice(device, TAG_END);
                if (!ok) btDisconnectDevice(device);
            }
        }
        else if (!strcmp(argv[2], "pair")) ok = btPairDevice(device, TAG_END);
        else if (!strcmp(argv[2], "connect")) ok = btConnectDevice(device);
        else if (!strcmp(argv[2], "disconnect")) ok = btDisconnectDevice(device);
    }
    CloseLibrary(BluetoothBase);
    return ok ? 0 : 20;
}
