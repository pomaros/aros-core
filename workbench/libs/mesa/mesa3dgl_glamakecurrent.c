/*
    Copyright (C) 2009-2026, The AROS Development Team. All rights reserved.
*/

#include <aros/debug.h>

#include <proto/exec.h>

#include "mesa3dgl_types.h"
#include "mesa3dgl_support.h"

/*****************************************************************************

    NAME */

      void glAMakeCurrent(

/*  SYNOPSIS */
      GLAContext ctx)

/*  FUNCTION
        Make the selected GL rendering context active.

    INPUTS
        ctx - GL rendering context to be made active for all following GL
                calls.

    RESULT

    BUGS

    INTERNALS

    HISTORY

*****************************************************************************/
{
    struct mesa3dgl_context *_ctx = (struct mesa3dgl_context *)ctx;

    D(bug("[MESA3DGL] %s()\n", __func__));

    /* The context with its own drawable, as glAMakeCurrentDrawables does
     * for others; NULL detaches the task's current context. */
    glAMakeCurrentDrawables(ctx, _ctx ? (GLADrawable)&_ctx->own : NULL,
        _ctx ? (GLADrawable)&_ctx->own : NULL);
}
