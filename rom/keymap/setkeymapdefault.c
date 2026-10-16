/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * This file is distributed under the AROS Public License, version 1.1.
 */
/*
    Copyright (C) 1995-2007, The AROS Development Team. All rights reserved.

    Desc:
*/
#include "keymap_intern.h"

#include <aros/debug.h>

/*****************************************************************************

    NAME */
#include <clib/keymap_protos.h>

        AROS_LH1(void, SetKeyMapDefault,

/*  SYNOPSIS */
        AROS_LHA(struct KeyMap *, keyMap, A0),

/*  LOCATION */
        struct Library *, KeymapBase, 5, Keymap)

/*  FUNCTION
        Sets the systemwide default keymap.

    INPUTS
        keyMap - pointer to KeyMap to set as system default.

    RESULT

    NOTES
        - This function should only be used by a keymap preferences editor.
        - Once you have set the keymap, you should NEVER deallocate it, as
          other apps might have got pointers to it via AskKeyMapDefault().
        - You should use the keymap.resource to check if the keymap has allready
          been added. If not, then remember to arbitrate before adding it to
          the keymap.resource list of keymaps.

    EXAMPLE

    BUGS
        All keymap.resource list access must hold kr_Semaphore. Keymap
        contents must be fully initialized before publication and immutable
        afterwards; publishing another default does not retire the old one.

    SEE ALSO
        AskKeyMapDefault()

    INTERNALS

    HISTORY
        27-11-96    digulla automatically created from
                            keymap_lib.fd and clib/keymap_protos.h

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    KeymapDefaultStore(KMBase(KeymapBase), keyMap);
    
    AROS_LIBFUNC_EXIT
} /* SetKeyMapDefault */
