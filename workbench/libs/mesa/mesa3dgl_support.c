/*
    Copyright (C) 2009-2019, The AROS Development Team. All rights reserved.
*/

#include <aros/debug.h>

/* Before the rest: something further down defines SCHED_RR, which turns
 * the KRN_SchedType enum in aros/kernel.h into a syntax error. */
#ifdef MESA3DGL_HAVE_COREAPI
#include <aros/kernel.h>
#include <proto/kernel.h>
#include <hardware/bcm2708.h>
#endif

#include <proto/utility.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/intuition.h>
#include <proto/cybergraphics.h>
#include <proto/gallium.h>
#include <proto/dos.h>
#include <proto/icon.h>

#include <workbench/workbench.h>

#include <cybergraphx/cybergraphics.h>
#include <graphics/rpattr.h>

#include "mesa3dgl_support.h"

/* Render-scale divisor (ENV:VC4_RENDER_SCALE, 1-4): GL renders at 1/N
 * of the drawable size and the vc4 present path shows it through the
 * HVS hardware scaler — fragment cost drops by N^2. Only honoured when
 * the vc4 shim is linked in (weak marker below) AND the display side can
 * actually scale a plane; elsewhere the fallback blit can't scale and
 * would draw a small image in the corner. */
ULONG mesa3dgl_render_scale = 1;

/* Marker: the GalliumCoreAPI table exists only in builds that carry a
 * runtime-bound hw driver (raspi/vc4) — the render-scale feature is
 * driver-side scaling, so gate on it. */
#ifdef MESA3DGL_HAVE_COREAPI
extern const void *gallium_core_get_api(void);
#else
static inline const void *gallium_core_get_api(void) { return (const void *)0; }
#endif

/*
 * Second gate: only HVS4 builds a scaled plane, so anywhere else a
 * scaled request would leave a 1/N image in the top-left corner. Not a
 * build-time decision - one aarch64 build ships both gallium hidds - and
 * the driver has no attribute to ask without pulling oop.library in here.
 * An allow list: an unknown SoC renders unscaled until someone has
 * checked its HVS, rather than silently taking the scaled path.
 */
#ifdef MESA3DGL_HAVE_COREAPI
static BOOL MESA3DGLScalerAvailable(VOID)
{
    struct Library *KernelBase = OpenResource("kernel.resource");
    IPTR base;

    if (!KernelBase)
        return FALSE;

    base = (IPTR)KrnGetSystemAttr(KATTR_PeripheralBase);

    return (base == BCM2835_PERIPHYSBASE) || (base == BCM2836_PERIPHYSBASE);
}
#else
static inline BOOL MESA3DGLScalerAvailable(VOID) { return FALSE; }
#endif

/* Per-program override: VC4_RENDER_SCALE tooltype in the program's icon
 * (.info next to the executable). Runs in the caller's process context,
 * so GetProgramDir() and the process name identify the right program. */
static BOOL MESA3DGLReadScaleToolType(ULONG *scale)
{
    struct Library *IconBase;
    struct Process *me = (struct Process *)FindTask(NULL);
    struct DiskObject *dob;
    BPTR progdir, olddir;
    TEXT name[108];
    STRPTR val;
    BOOL found = FALSE;

    if (me->pr_Task.tc_Node.ln_Type != NT_PROCESS)
        return FALSE;

    progdir = GetProgramDir();
    if (progdir == BNULL)
        return FALSE;

    if (GetProgramName(name, sizeof(name)) == DOSFALSE || name[0] == '\0')
    {
        /* WB-launched: no CLI name set, the process carries the tool name */
        name[sizeof(name) - 1] = '\0';
        strncpy(name, me->pr_Task.tc_Node.ln_Name, sizeof(name) - 1);
    }

    IconBase = OpenLibrary("icon.library", 0);
    if (!IconBase)
        return FALSE;

    olddir = CurrentDir(progdir);
    dob = GetDiskObject(FilePart(name));
    if (dob)
    {
        val = FindToolType(dob->do_ToolTypes, "VC4_RENDER_SCALE");
        if (val && val[0] >= '1' && val[0] <= '4' && val[1] == '\0')
        {
            *scale = val[0] - '0';
            found = TRUE;
        }
        FreeDiskObject(dob);
    }
    CurrentDir(olddir);
    CloseLibrary(IconBase);

    return found;
}

static VOID MESA3DGLReadRenderScale(VOID)
{
    TEXT buf[8];

    mesa3dgl_render_scale = 1;
    if (!gallium_core_get_api() || !MESA3DGLScalerAvailable())
        return;

    if (GetVar("VC4_RENDER_SCALE", buf, sizeof(buf), 0) > 0
        && buf[0] >= '1' && buf[0] <= '4')
        mesa3dgl_render_scale = buf[0] - '0';

    /* Icon tooltype (per program) overrides the env variable (global) */
    MESA3DGLReadScaleToolType(&mesa3dgl_render_scale);
}

VOID MESA3DGLSelectRastPort(struct mesa3dgl_drawable * d, struct TagItem * tagList)
{
    D(bug("[MESA3DGL] %s()\n", __func__));

    d->Screen = (struct Screen *)GetTagData(GLA_Screen, 0, tagList);
    d->window = (struct Window *)GetTagData(GLA_Window, 0, tagList);
    d->visible_rp = (struct RastPort *)GetTagData(GLA_RastPort, 0, tagList);

    if (d->Screen)
    {
        D(bug("[MESA3DGL] %s: Screen @ 0x%p\n", __func__, d->Screen));
        if (d->window)
        {
            D(bug("[MESA3DGL] %s: Window @ 0x%p\n", __func__, d->window));
            if (!(d->visible_rp))
            {
                /* Use the windows rastport */
                d->visible_rp = d->window->RPort;
                D(bug("[MESA3DGL] %s: Windows RastPort @ 0x%p\n", __func__, d->visible_rp));
            }
        }
        else
        {
            if (!(d->visible_rp))
            {
                /* Use the screens rastport */
                d->visible_rp = &d->Screen->RastPort;
                D(bug("[MESA3DGL] %s: Screens RastPort @ 0x%p\n", __func__, d->visible_rp));
            }
        }
    }
    else
    {
        /* Not passed a screen */
        if (d->window)
        {
            D(bug("[MESA3DGL] %s: Window @ 0x%p\n", __func__, d->window));
            /* Use the windows Screen */
            d->Screen = d->window->WScreen;
            D(bug("[MESA3DGL] %s: Windows Screen @ 0x%p\n", __func__, d->Screen));

            if (!(d->visible_rp))
            {
                /* Use the windows rastport */
                d->visible_rp = d->window->RPort;
                D(bug("[MESA3DGL] %s: Windows RastPort @ 0x%p\n", __func__, d->visible_rp));
            }
        }
        else if (d->visible_rp)
        {
            /* Only Passed A Rastport */
            D(bug("[MESA3DGL] %s: Using RastPort only!\n"));
        }
        else
        {
            /* No target: render offscreen into a bitmap of GLA_Width by
             * GLA_Height (1 by 1 by default), a friend of the default public
             * screen's so the same pipe driver serves it, on a layer of its
             * own so that the rastport code below finds its bounds. */
            LONG width = GetTagData(GLA_Width, 1, tagList);
            LONG height = GetTagData(GLA_Height, 1, tagList);
            struct Screen *screen = LockPubScreen(NULL);

            if (width < 1) width = 1;
            if (height < 1) height = 1;
            if (screen)
            {
                d->offscreen_bm = AllocBitMap(width, height,
                    GetBitMapAttr(screen->RastPort.BitMap, BMA_DEPTH),
                    BMF_MINPLANES, screen->RastPort.BitMap);
                UnlockPubScreen(NULL, screen);
            }
            if (d->offscreen_bm)
                d->offscreen_li = NewLayerInfo();
            if (d->offscreen_li)
                d->offscreen_layer = CreateUpfrontLayer(d->offscreen_li,
                    d->offscreen_bm, 0, 0, width - 1, height - 1, LAYERSIMPLE, NULL);
            if (d->offscreen_layer)
                d->visible_rp = d->offscreen_layer->rp;
            else
                MESA3DGLFreeTarget(d);
            D(bug("[MESA3DGL] %s: offscreen %ldx%ld rp @ 0x%p\n", __func__,
                width, height, d->visible_rp));
        }
    }

    D(bug("[MESA3DGL] %s: Using RastPort @ 0x%p\n", __func__, d->visible_rp));

    /* Window/layer geometry at context creation: a fresh SDL window's
     * layer coming up collapsed (~1px) shows here. */
    D({
        struct Layer *l = d->visible_rp ? d->visible_rp->Layer : NULL;

        bug("[MESA3DGL] target: win=%p rp=%p layer=%p", d->window,
            d->visible_rp, l);
        if (d->window)
            bug(" win %ldx%ld at %ld,%ld", (LONG)d->window->Width,
                (LONG)d->window->Height, (LONG)d->window->LeftEdge,
                (LONG)d->window->TopEdge);
        if (l)
            bug(" layer bounds %ld,%ld..%ld,%ld", (LONG)l->bounds.MinX,
                (LONG)l->bounds.MinY, (LONG)l->bounds.MaxX,
                (LONG)l->bounds.MaxY);
        bug("\n");
    })
}

BOOL MESA3DGLStandardInit(struct mesa3dgl_drawable * d, struct TagItem *tagList)
{
    LONG requestedwidth = 0, requestedheight = 0;
    LONG requestedright = 0, requestedbottom = 0;
    LONG defaultleft = 0, defaulttop = 0;
    LONG defaultright = 0, defaultbottom = 0;

    D(bug("[MESA3DGL] %s(d @ 0x%p, taglist @ 0x%p)\n", __func__, d, tagList));

    MESA3DGLReadRenderScale();

    /* Set the defaults based on window information */
    if (d->window)
    {
        if(!(d->window->Flags & WFLG_GIMMEZEROZERO))
        {
            defaultleft     = d->window->BorderLeft;
            defaulttop      = d->window->BorderTop;
            defaultright    = d->window->BorderRight;
            defaultbottom   = d->window->BorderBottom;
        }
    }

    D(bug("[MESA3DGL] %s: Using RastPort @ 0x%p\n", __func__, d->visible_rp));

    {
        struct RastPort *clone = CloneRastPort(d->visible_rp);

        /* The clone replaces the target's own rastport; without it there is
         * no rastport the drawable owns. */
        if (!clone || !clone->Layer)
        {
            if (clone)
                FreeRastPort(clone);
            return FALSE;
        }
        d->visible_rp = clone;
    }

    D(bug("[MESA3DGL] %s: Cloned RastPort @ 0x%p\n", __func__, d->visible_rp));

    /* We assume left and top are given or if there is a window, set to border left/top
       or if there is no window set to 0 */
    d->left = GetTagData(GLA_Left, defaultleft, tagList);
    d->top = GetTagData(GLA_Top, defaulttop, tagList);

    requestedright = GetTagData(GLA_Right, -1, tagList);
    requestedbottom = GetTagData(GLA_Bottom, -1, tagList);
    requestedwidth = GetTagData(GLA_Width, -1 , tagList);
    requestedheight = GetTagData(GLA_Height, -1 , tagList);

    /* Calculate rastport dimensions */
    d->visible_rp_width =
        d->visible_rp->Layer->bounds.MaxX - d->visible_rp->Layer->bounds.MinX + 1;

    d->visible_rp_height =
        d->visible_rp->Layer->bounds.MaxY - d->visible_rp->Layer->bounds.MinY + 1;

    /* right will be either passed or calculated from width or 0 */
    d->right = 0;
    if (requestedright < 0)
    {
        if (requestedwidth >= 0)
        {
            requestedright = d->visible_rp_width - d->left - requestedwidth;
            if (requestedright < 0) requestedright = 0;
        }
        else
            requestedright = defaultright; /* Set the default here, not in GetDataData */
    }
    d->right = requestedright;

    /* bottom will be either passed or calculated from height or 0 */
    d->bottom = 0;
    if (requestedbottom < 0)
    {
        if (requestedheight >= 0)
        {
            requestedbottom = d->visible_rp_height - d->top - requestedheight;
            if (requestedbottom < 0) requestedbottom = 0;
        }
        else
            requestedbottom = defaultbottom; /* Set the default here, not in GetDataData */
    }
    d->bottom = requestedbottom;

    /* Init screen information */
    if (d->Screen)
        d->BitsPerPixel  = GetCyberMapAttr(d->Screen->RastPort.BitMap, CYBRMATTR_BPPIX) * 8;
    else if (d->offscreen_bm)
        d->BitsPerPixel  = GetCyberMapAttr(d->offscreen_bm, CYBRMATTR_BPPIX) * 8;

    D(bug("[MESA3DGL] %s: Context Base dimensions set -:\n", __func__));
    D(bug("[MESA3DGL] %s:    d->visible_rp_width        = %d\n", __func__, d->visible_rp_width));
    D(bug("[MESA3DGL] %s:    d->visible_rp_height       = %d\n", __func__, d->visible_rp_height));
    D(bug("[MESA3DGL] %s:    d->left                    = %d\n", __func__, d->left));
    D(bug("[MESA3DGL] %s:    d->right                   = %d\n", __func__, d->right));
    D(bug("[MESA3DGL] %s:    d->top                     = %d\n", __func__, d->top));
    D(bug("[MESA3DGL] %s:    d->bottom                  = %d\n", __func__, d->bottom));

    return TRUE;
}

VOID MESA3DGLRecalculateBufferWidthHeight(struct mesa3dgl_drawable * d)
{
    LONG newwidth = 0;
    LONG newheight = 0;
    
    D(bug("[MESA3DGL] %s(0x%p)\n", __func__, d));

    d->visible_rp_width =
        d->visible_rp->Layer->bounds.MaxX - d->visible_rp->Layer->bounds.MinX + 1;

    d->visible_rp_height =
        d->visible_rp->Layer->bounds.MaxY - d->visible_rp->Layer->bounds.MinY + 1;

    /* NOTE: when a window's layer bounds read ~1px while the window
     * itself is fully sized, faking the framebuffer size here does NOT
     * help — AROS clips all rendering/blitting to the layer, so
     * presentation stays confined to those few pixels. Such a bug has to
     * be fixed where the window and its layer are created, not here.
     * Left as the plain layer read. */


    /* Signed: borders wider than the layer leave no area, not 4 GB. */
    newwidth = (LONG)d->visible_rp_width - (LONG)d->left - (LONG)d->right;
    newheight = (LONG)d->visible_rp_height - (LONG)d->top - (LONG)d->bottom;

    if (newwidth < 0) newwidth = 0;
    if (newheight < 0) newheight = 0;

    /* Render-scale: draw at 1/N, present through the HVS scaler. Even
     * width keeps pixel formats happy; the present path upscales by
     * the exact src/dest ratio so rounding never skews the aspect. */
    if (mesa3dgl_render_scale > 1)
    {
        newwidth = (newwidth / mesa3dgl_render_scale) & ~1;
        newheight = newheight / mesa3dgl_render_scale;
        if (newwidth < 2) newwidth = 2;
        if (newheight < 2) newheight = 2;
    }
    
    
    if (((ULONG)newwidth != d->framebuffer->width) || ((ULONG)newheight != d->framebuffer->height))
    {
        /* rp_w/h are pre-scale/pre-border. */
        D(bug("[MESA3DGL] fb resize: %lux%lu -> %lux%lu (rp %lux%lu, scale %lu)\n",
            (ULONG)d->framebuffer->width, (ULONG)d->framebuffer->height,
            (ULONG)newwidth, (ULONG)newheight,
            (ULONG)d->visible_rp_width, (ULONG)d->visible_rp_height,
            (ULONG)mesa3dgl_render_scale));

        /* The drawing area size has changed. Buffer must change */
        D(bug("[MESA3DGL] %s: current height    =   %d\n", __func__, d->framebuffer->height));
        D(bug("[MESA3DGL] %s: current width     =   %d\n", __func__, d->framebuffer->width));
        D(bug("[MESA3DGL] %s: new height        =   %d\n", __func__, newheight));
        D(bug("[MESA3DGL] %s: new width         =   %d\n", __func__, newwidth));
        
        d->framebuffer->width = newwidth;
        d->framebuffer->height = newheight;
        d->framebuffer->resized = TRUE;
        
        if (d->window)
        {
            struct Rectangle rastcliprect;
            struct TagItem crptags[] =
            {
                { RPTAG_ClipRectangle      , (IPTR)&rastcliprect },
                { RPTAG_ClipRectangleFlags , (RPCRF_RELRIGHT | RPCRF_RELBOTTOM) },
                { TAG_DONE }
            };
        
            D(bug("[MESA3DGL] %s: Clipping Rastport to Window's dimensions\n", __func__));

            /* Clip the rastport to the visible area */
            rastcliprect.MinX = d->left;
            rastcliprect.MinY = d->top;
            rastcliprect.MaxX = d->left + d->framebuffer->width;
            rastcliprect.MaxY = d->top + d->framebuffer->height;
            SetRPAttrsA(d->visible_rp, crptags);
        }
    }
    D(bug("[MESA3DGL] %s: done\n", __func__));
}

/* The rastport the context renders to, and an offscreen target's bitmap
 * and layer. */
VOID MESA3DGLFreeTarget(struct mesa3dgl_drawable * d)
{
    /* A window's frames may be on a display plane of the driver's (the DCP
     * layer): it goes with the target. ReleasePipeRastPort came with
     * gallium.library 26.1. */
    if (d->driver && d->visible_rp && !d->offscreen_layer &&
        (GalliumBase->lib_Version > 26 ||
         (GalliumBase->lib_Version == 26 && GalliumBase->lib_Revision >= 1)))
        ReleasePipeRastPort(d->driver, d->visible_rp);
    if (d->visible_rp && d->visible_rp != (d->offscreen_layer ? d->offscreen_layer->rp : NULL))
        FreeRastPort(d->visible_rp);
    d->visible_rp = NULL;
    if (d->offscreen_layer)
        DeleteLayer(0, d->offscreen_layer);
    if (d->offscreen_li)
        DisposeLayerInfo(d->offscreen_li);
    if (d->offscreen_bm)
    {
        WaitBlit();
        FreeBitMap(d->offscreen_bm);
    }
    d->offscreen_layer = NULL;
    d->offscreen_li = NULL;
    d->offscreen_bm = NULL;
}

/* A new target for an existing drawable, prepared on the side: STAGED
 * (zeroed by the caller) gets the window, rastport or offscreen bitmap and
 * its geometry. On failure nothing of it is left and the drawable it would
 * replace is untouched. */
BOOL MESA3DGLPrepareTarget(struct mesa3dgl_drawable * staged, struct TagItem * tagList)
{
    MESA3DGLSelectRastPort(staged, tagList);
    if (!staged->visible_rp)
        return FALSE;
    if (!MESA3DGLStandardInit(staged, tagList))
    {
        /* The rastport is still the window's, the screen's or the layer's:
         * only an offscreen bitmap and layer were made here. */
        staged->visible_rp = NULL;
        MESA3DGLFreeTarget(staged);
        return FALSE;
    }
    return TRUE;
}

/* D takes the target STAGED prepared; its old target goes, its framebuffer
 * stays (resized at the next validation if the size changed). */
VOID MESA3DGLCommitTarget(struct mesa3dgl_drawable * d, struct mesa3dgl_drawable * staged)
{
    struct mesa3dgl_framebuffer *framebuffer = d->framebuffer;
    APTR driver = d->driver;

    MESA3DGLFreeTarget(d);
    *d = *staged;
    d->framebuffer = framebuffer;
    d->driver = driver;
}

/* Whether a target of BPP bits per pixel fits a visual: the framebuffer's
 * colour format was chosen for the depth the context was made for. */
BOOL MESA3DGLVisualAccepts(const struct st_visual * stvis, ULONG bpp)
{
    if (bpp == 32)
        return stvis->color_format == PIPE_FORMAT_B8G8R8A8_UNORM;
    if (bpp == 16)
        return stvis->color_format == PIPE_FORMAT_B5G6R5_UNORM;
    return FALSE;
}

VOID MESA3DGLFreeContext(struct mesa3dgl_context * ctx)
{
    if (ctx)
    {
        FreeVec(ctx);
    }
}

