/*
    Copyright (C) 2009-2026, The AROS Development Team. All rights reserved.
*/

#include <aros/debug.h>

#include "util/os_misc.h"
#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"

#include "util/u_inlines.h"


#include <proto/exec.h>
#include <proto/gallium.h>

#include "mesa3dgl_types.h"
#include "mesa3dgl_support.h"
#include "mesa3dgl_gallium.h"



/*****************************************************************************

    NAME */

      void glASwapBuffers(

/*  SYNOPSIS */
      GLAContext ctx)

/*  FUNCTION
        Swaps the back with front buffers. MUST BE used to display the effect
        of rendering onto the target RastPort, since GLA always work in
        double buffer mode.

    INPUTS
        ctx - GL rendering context on which swap is to be performed.

    RESULT

    BUGS

    INTERNALS

    HISTORY

*****************************************************************************/
{
    struct mesa3dgl_context *_ctx = (struct mesa3dgl_context *)ctx;
    /* The drawable the context draws to now (its own one after
     * glAMakeCurrent, an EGL surface's after glAMakeCurrentDrawables). */
    struct mesa3dgl_drawable *d = _ctx ? _ctx->draw : NULL;

    D(bug("[MESA3DGL] %s()\n", __func__));

    if (!d)
        return;
    if (d->framebuffer->render_resource)
    {
        struct pipe_context *pipe = _ctx->st->pipe;
        struct pipe_resource *zs =
            d->framebuffer->textures[ST_ATTACHMENT_DEPTH_STENCIL];

        /* Depth/stencil is undefined after a swap: invalidate it before
         * the flush, so a tiler skips storing it (as Mesa's DRI does). */
        if (zs && pipe->invalidate_resource)
            pipe->invalidate_resource(pipe, zs);

        /* Flush rendering cache before blitting */
        st_context_flush(_ctx->st, ST_FLUSH_FRONT, NULL, NULL, NULL);

        /* An offscreen target is read back with glReadPixels, never shown */
        if (d->offscreen_bm)
            return;

        /* gallium.library tries the whole-window DisplayResourceRP method
         * first (vc4 presents zero-copy there) and falls back to the
         * per-cliprect DisplayResource loop (softpipe). */
        BltPipeResourceRastPort(d->driver, d->framebuffer->render_resource, 0, 0,
            d->visible_rp, d->left, d->top,
            d->framebuffer->width, d->framebuffer->height);
    }


    MESA3DGLCheckAndUpdateBufferSize(d);
}
