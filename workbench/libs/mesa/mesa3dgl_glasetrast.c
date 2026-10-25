/*
    Copyright (C) 2011-2019, The AROS Development Team. All rights reserved.
*/

#include <aros/debug.h>

#include <string.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include <proto/graphics.h>

#include "mesa3dgl_support.h"
#include "mesa3dgl_gallium.h"

/*****************************************************************************

    NAME */

      BOOL glASetRast(

/*  SYNOPSIS */
      GLAContext ctx,
      struct TagItem * tagList)

/*  FUNCTION

        Sets a new rendering target for an existing context

    INPUTS

        ctx -
        tagList - a pointer to tags to be used during creation.

    TAGS

        GLA_Window - pointer to Window onto which scene is to be rendered.

        GLA_Width, GLA_Height - without GLA_Window, GLA_Screen or
                     GLA_RastPort: an offscreen target of that size, as
                     glACreateContext makes one.

        Without a target or a size the context keeps its target.

    RESULT

        TRUE when the context draws to the new target. FALSE when the target
        could not be prepared or its depth does not fit the context's visual;
        the context then keeps its old target, untouched.

    BUGS

    INTERNALS

    HISTORY

*****************************************************************************/
{
    struct mesa3dgl_context * _ctx = (struct mesa3dgl_context *)ctx;
    struct mesa3dgl_drawable staged;

    D(bug("[MESA3DGL] %s()\n", __func__));

    if (!_ctx)
        return FALSE;
    /* A window, rastport or screen, or an offscreen size; else nothing to do */
    if (!GetTagData(GLA_Screen, 0, tagList) && !GetTagData(GLA_Window, 0, tagList) &&
        !GetTagData(GLA_RastPort, 0, tagList) && !GetTagData(GLA_Width, 0, tagList) &&
        !GetTagData(GLA_Height, 0, tagList))
        return TRUE;

    /* Prepared on the side; the old target goes only once the new one is
     * complete and fits the visual. */
    memset(&staged, 0, sizeof(staged));
    if (!MESA3DGLPrepareTarget(&staged, tagList))
        return FALSE;
    if (!MESA3DGLVisualAccepts(&_ctx->stvis, staged.BitsPerPixel))
    {
        MESA3DGLFreeTarget(&staged);
        return FALSE;
    }
    MESA3DGLCommitTarget(&_ctx->own, &staged);
    MESA3DGLCheckAndUpdateBufferSize(&_ctx->own);
    return TRUE;
}
