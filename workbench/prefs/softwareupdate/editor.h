/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Distributed under the AROS Public License, version 1.1. */
#ifndef SOFTWARE_UPDATE_EDITOR_H
#define SOFTWARE_UPDATE_EDITOR_H
#include <libraries/mui.h>
#define MUIM_SoftwareUpdateEditor_Refresh (TAG_USER | 0x105a0001)
struct SoftwareUpdateEditor_DATA { Object *installed,*previous,*last,*pending,*delivered; };
#endif
