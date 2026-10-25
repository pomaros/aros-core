/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    This file is part of AROS, distributed under the AROS Public License 1.1.
*/
#ifndef _ICONLISTVIEW_PRIVATE_H_
#define _ICONLISTVIEW_PRIVATE_H_

#include <intuition/classusr.h>
#include <utility/hooks.h>

extern struct MUI_CustomClass *IconListviewScaleClass;

/*** Instance data **********************************************************/
struct IconListview_DATA
{
    Object *iconlist;
    Object *vert, *horiz, *button;
    struct Hook hook;
    struct Hook *layout_hook;
};

#endif /* _ICONLISTVIEW_PRIVATE_H_ */
