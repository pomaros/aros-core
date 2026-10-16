#ifndef _FPEDITOR_PRIVATE_H_
#define _FPEDITOR_PRIVATE_H_
/*
    Copyright (C) 2004-2026, The AROS Development Team. All rights reserved.
*/
#include <exec/types.h>
#include <prefs/font.h>
#include "prefs.h"

/*** Instance Data **********************************************************/
struct FPEditor_DATA
{
    struct FontPrefs  fped_FontPrefs[FP_COUNT];
    Object           *fped_IconsString,
                     *fped_ScreenString,
                     *fped_SystemString;
};

#endif /* _FPEDITOR_PRIVATE_H_ */
