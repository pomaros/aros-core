/* Copyright (C) 2026 John Knipper
 * Licensed under the AROS Public License (APL) Version 1.1 */
#ifndef KEYRING_EDITOR_H
#define KEYRING_EDITOR_H
#include <libraries/mui.h>
#define MUIM_KeyringEditor_Refresh (TAG_USER | 0x105b0001)
#define MUIM_KeyringEditor_Lock (TAG_USER | 0x105b0002)
#define MUIM_KeyringEditor_Unlock (TAG_USER | 0x105b0003)
struct KeyringEditor_DATA { Object *state, *result; };
#endif
