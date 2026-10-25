/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    This file is part of AROS, distributed under the AROS Public License 1.1.
*/
#ifndef _ICONDRAWERLIST_PRIVATE_H_
#define _ICONDRAWERLIST_PRIVATE_H_

#include "iconlist.h"

extern struct MUI_CustomClass *IconDrawerListScaleClass;

/*** Instance data **********************************************************/
struct IconDrawerList_DATA
{
    char *drawer;
};

#endif /* _ICONDRAWERLIST_PRIVATE_H_ */
