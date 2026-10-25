/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    This file is part of AROS, distributed under the AROS Public License 1.1.
*/
#ifndef _ICONVOLUMELIST_PRIVATE_H_
#define _ICONVOLUMELIST_PRIVATE_H_

#include "iconlist.h"

extern struct MUI_CustomClass *IconVolumeListScaleClass;

/*** Instance data **********************************************************/
struct IconVolumeList_DATA
{
    STRPTR ivld_HiddenVolumes;
};

#endif /* _ICONVOLUMELIST_PRIVATE_H_ */
