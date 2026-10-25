/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <aros/asmcall.h>
#include <exec/execbase.h>
#include <exec/lists.h>
#include "intservers.h"

/* This platform's kernel scheduler owns task quanta. The timer's nominal
 * VBlank heartbeat serves graphics/WaitTOF through the normal Exec vector;
 * it must not also decrement the scheduler's quantum. */
AROS_INTH3(VBlankServer, struct List *, intList, intMask, custom)
{
    AROS_INTFUNC_INIT
    return AROS_INTC3(IntServer, intList, intMask, custom);
    AROS_INTFUNC_EXIT
}
