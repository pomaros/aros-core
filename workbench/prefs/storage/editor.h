/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#ifndef STORAGE_EDITOR_H
#define STORAGE_EDITOR_H
#include <libraries/mui.h>
#define MUIM_StorageEditor_Refresh (TAG_USER | 0x10590001)
#define MUIM_StorageEditor_Rescan (TAG_USER | 0x10590002)
#define MUIM_StorageEditor_Snapshots (TAG_USER | 0x10590003)
#define MUIM_StorageEditor_DeleteSnapshot (TAG_USER | 0x10590004)
struct StorageEditor_DATA
{
    Object *status;
    Object *volumes;
    Object *snapshots;
    Object *snapshot_status;
};
#endif
