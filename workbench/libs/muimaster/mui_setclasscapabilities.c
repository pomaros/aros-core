/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <proto/exec.h>
#include "mui.h"
#include "muimaster_intern.h"
#include "support.h"
AROS_LH2(BOOL, MUI_SetClassCapabilities,
    AROS_LHA(Class *, cl, A0), AROS_LHA(ULONG, capabilities, D0),
    struct Library *, MUIMasterBase, 34, MUIMaster)
{
    AROS_LIBFUNC_INIT
    return zune_set_class_capabilities(cl, capabilities, MUIMasterBase);
    AROS_LIBFUNC_EXIT
}
