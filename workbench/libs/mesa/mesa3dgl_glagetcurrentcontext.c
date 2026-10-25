/*
    Copyright (C) 2009-2019, The AROS Development Team. All rights reserved.
*/

#include <aros/debug.h>

#include <proto/exec.h>

#include "mesa3dgl_types.h"

/*****************************************************************************

    NAME */

      GLAContext glAGetCurrentContext(

/*  SYNOPSIS */
      )

/*  FUNCTION
        Returns the currently selected GL rendering context.

    INPUTS

    RESULT
        The GL rendering context which is currently active.

    BUGS

    INTERNALS

    HISTORY

*****************************************************************************/
{
    D(bug("[MESA3DGL] %s()\n", __func__));

    /* The task's current state tracker context names its GLA owner
     * (glACreateContext sets frontend_context); not Mesa's gl_context. */
    struct st_context *current = st_api_get_current();
    return current ? (GLAContext)current->frontend_context : NULL;
}

