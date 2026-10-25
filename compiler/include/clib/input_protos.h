#ifndef CLIB_INPUT_PROTOS_H
#define CLIB_INPUT_PROTOS_H

/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License, version 1.1.
    Copyright © 1995-2001, The AROS Development Team. All rights reserved.
    $Id$

    Desc: Prototypes for input.device
    Lang: english
*/

#ifndef AROS_LIBCALL_H
#   include <aros/libcall.h>
#endif

/*
    Prototypes
*/

AROS_LP0(UWORD, PeekQualifier,
	struct Device *, InputBase, 7, Input);

#endif /* CLIB_INPUT_PROTOS_H */
