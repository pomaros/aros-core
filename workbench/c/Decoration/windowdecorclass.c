/*
    Copyright (C) 2011-2026, The AROS Development Team.
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
*/

#define DEBUG 0
#include <aros/debug.h>

#include <clib/alib_protos.h>

#include <intuition/windecorclass.h>
#include <intuition/extensions.h>
#include <intuition/gadgetclass.h>
#include <graphics/rpattr.h>
#include <proto/arossupport.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>
#include <proto/exec.h>
#include <string.h>
#include <stddef.h>

#include <libraries/decorator.h>
#include <proto/decorator.h>

#include "windowdecorclass.h"
#include "screendecorclass.h"
#include "scaledimage.h"

/* Compatibility shims for the struct-based decorator.library calls */
static inline void ShadeLine(LONG pen, BOOL tc, BOOL usegradients, struct RastPort *rp, struct DecorImage *di,
                             ULONG basecolor, UWORD fact, UWORD offy, UWORD x0, UWORD y0, UWORD x1, UWORD y1, ULONG thickness)
{
    struct Rectangle bounds;
    ULONG i;
    for (i = 0; i < thickness; ++i)
    {
        /* Endpoints denote the start of their scaled logical pixel. Include
         * its final physical pixel on the long axis as well as widening the
         * stroke; otherwise integer scales leave gaps at border corners. */
        bounds.MinX = x0 + (x0 == x1 ? i : 0);
        bounds.MinY = y0 + (y0 == y1 ? i : 0);
        bounds.MaxX = x1 + (x0 == x1 ? i : thickness - 1);
        bounds.MaxY = y1 + (y0 == y1 ? i : thickness - 1);
        DShadeLine(pen, tc, usegradients, rp, di, basecolor, fact, offy, &bounds);
    }
}

static inline void FillPixelArrayGradient(LONG pen, BOOL tc, struct RastPort *rp, LONG xt, LONG yt, LONG xb, LONG yb,
                                          LONG xp, LONG yp, LONG w, LONG h, ULONG start_rgb, ULONG end_rgb, LONG angle,
                                          LONG dx, LONG dy)
{
    struct GradientSpec spec;
    spec.gs_xt = xt;
    spec.gs_yt = yt;
    spec.gs_xb = xb;
    spec.gs_yb = yb;
    spec.gs_xp = xp;
    spec.gs_yp = yp;
    spec.gs_w = w;
    spec.gs_h = h;
    spec.gs_StartRGB = start_rgb;
    spec.gs_EndRGB = end_rgb;
    spec.gs_Angle = angle;
    spec.gs_dx = dx;
    spec.gs_dy = dy;
    DFillPixelArrayGradient(pen, tc, rp, &spec);
}

/* Title text alignment values (override system header) */
#undef WD_DWTA_LEFT
#undef WD_DWTA_CENTER
#undef WD_DWTA_RIGHT
#define WD_DWTA_LEFT    0
#define WD_DWTA_CENTER  1
#define WD_DWTA_RIGHT   2

#define SETIMAGE_WIN(id) wd->img_##id=sd->di->img_##id

/* Renders a titlebar section element using the (possibly rescaled)
   titlebar image of the current draw, advancing and clipping like
   tiled title rendering */
static UWORD RenderWinBarElement(struct WindowData *wd, ULONG elemid, struct RastPort *rp,
                                 struct DecorImage *di, ULONG subimage, UWORD x, WORD dw,
                                 UWORD barh, LONG clipw)
{
    struct DecoratorElement elem = wd->dts->dts_Elements[elemid];

    elem.de_Image = di;
    return (UWORD)DRenderElement(&elem, rp, subimage, x, 0, dw, barh, clipw);
}

struct windecor_data
{
    struct DecorConfig * dc;

    /* Pointers to images used for sys images */
    struct DecorImage *img_size;
    struct DecorImage *img_close;
    struct DecorImage *img_depth;
    struct DecorImage *img_zoom;
    struct DecorImage *img_up;
    struct DecorImage *img_down;
    struct DecorImage *img_left;
    struct DecorImage *img_right;
    struct DecorImage *img_mui;
    struct DecorImage *img_popup;
    struct DecorImage *img_snapshot;
    struct DecorImage *img_iconify;
    struct DecorImage *img_lock;

    UWORD            TextAlign;
};

#define WINDOW_SCALE_IMAGE_LIST(X) \
    X(size) X(close) X(depth) X(zoom) X(up) X(down) X(left) X(right) \
    X(mui) X(popup) X(snapshot) X(iconify) X(lock) X(winbar_normal) \
    X(border_normal) X(border_deactivated) X(verticalcontainer) X(verticalknob) \
    X(horizontalcontainer) X(horizontalknob)

struct WindowScaleData
{
    struct windecor_data renderer;
    struct DecorConfig config;
    struct DecorThemeScreen theme;
    struct DecorImages images;
    struct DecorImage empty;
    const struct ScreenRenderContext *context;
    ULONG scale;
};

/* Theme resource names for refusal records: the THEME: file of each image. */
static const char *window_scale_image_name(const char *id)
{
    static const char *const names[][2] = {
        {"size", "System/Size/Default"}, {"close", "System/Close/Default"},
        {"depth", "System/Depth/Default"}, {"zoom", "System/Zoom/Default"},
        {"up", "System/ArrowUp/Default"}, {"down", "System/ArrowDown/Default"},
        {"left", "System/ArrowLeft/Default"}, {"right", "System/ArrowRight/Default"},
        {"mui", "System/MUI/Default"}, {"popup", "System/PopUp/Default"},
        {"snapshot", "System/Snapshot/Default"}, {"iconify", "System/Iconify/Default"},
        {"lock", "System/Lock/Default"}, {"winbar_normal", "System/Titlebar/Default"},
        {"border_normal", "System/Borders/Default"},
        {"border_deactivated", "System/Borders/Default_Deactivated"},
        {"verticalcontainer", "System/Container/Vertical"},
        {"verticalknob", "System/Knob/Vertical"},
        {"horizontalcontainer", "System/Container/Horizontal"},
        {"horizontalknob", "System/Knob/Horizontal"}};
    ULONG i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (!strcmp(names[i][0], id)) return names[i][1];
    return id;
}

/* The caller owns the record; it is written only on refusal. */
static void window_scale_refuse(struct wdpScaleRefusal *refusal, ULONG code,
    LONG value0, LONG value1, LONG value2, const char *name)
{
    ULONG i = 0;
    if (!refusal) return;
    refusal->wsr_Code = code;
    refusal->wsr_Value[0] = value0; refusal->wsr_Value[1] = value1;
    refusal->wsr_Value[2] = value2;
    if (name)
        for (; i + 1 < sizeof(refusal->wsr_Name) && name[i]; ++i)
            refusal->wsr_Name[i] = name[i];
    refusal->wsr_Name[i] = 0;
}

#define CHANGE_NO_CHANGE        0
#define CHANGE_SIZE_CHANGE      1 /* Whole gadget area has changed */
#define CHANGE_NO_SIZE_CHANGE   2 /* Size remained the same, contents has changed */

static ULONG HasPropGadgetChanged(struct CachedPropGadget *cached, struct wdpDrawBorderPropKnob *msg)
{
    /* More restrictive tests */
    
    /* If the size has changed */
    if (cached->width != (msg->wdp_PropRect->MaxX - msg->wdp_PropRect->MinX + 1))
        return CHANGE_SIZE_CHANGE;
    if (cached->height != (msg->wdp_PropRect->MaxY - msg->wdp_PropRect->MinY + 1))
        return CHANGE_SIZE_CHANGE;

    /* If there is no cached bitmap at all (this can happen NOT only at first call) */
    if (cached->bm == NULL)
        return CHANGE_SIZE_CHANGE;

    /* Less restrictive tests */

    /* If knob position has changed */
    if (cached->knobx != (msg->wdp_RenderRect->MinX - msg->wdp_PropRect->MinX))
        return CHANGE_NO_SIZE_CHANGE;
    if (cached->knoby != (msg->wdp_RenderRect->MinY - msg->wdp_PropRect->MinY))
        return CHANGE_NO_SIZE_CHANGE;
    
    /* If knob is release state has changed */
    if (cached->gadgetflags ^ (msg->wdp_Flags & WDF_DBPK_HIT))
        return CHANGE_NO_SIZE_CHANGE;

    /* If the window activity status has changed */
    if (cached->windowflags ^ (msg->wdp_Window->Flags & (WFLG_WINDOWACTIVE | WFLG_TOOLBOX)))
        return CHANGE_NO_SIZE_CHANGE;

    /* If knob size has changed */
    if (cached->knobwidth != (msg->wdp_RenderRect->MaxX - msg->wdp_RenderRect->MinX + 1))
        return CHANGE_NO_SIZE_CHANGE;
    if (cached->knobheight != (msg->wdp_RenderRect->MaxY - msg->wdp_RenderRect->MinY + 1))
        return CHANGE_NO_SIZE_CHANGE;

    return CHANGE_NO_CHANGE;
}

static VOID CachePropGadget(struct CachedPropGadget *cached, struct wdpDrawBorderPropKnob *msg,
    struct BitMap *bitmap)
{
    cached->bm         = bitmap;
    cached->width      = msg->wdp_PropRect->MaxX - msg->wdp_PropRect->MinX + 1;
    cached->height     = msg->wdp_PropRect->MaxY - msg->wdp_PropRect->MinY + 1;
    cached->windowflags= (msg->wdp_Window->Flags & (WFLG_WINDOWACTIVE | WFLG_TOOLBOX));
    cached->gadgetflags= (msg->wdp_Flags & WDF_DBPK_HIT);
    cached->knobwidth  = msg->wdp_RenderRect->MaxX - msg->wdp_RenderRect->MinX + 1;
    cached->knobheight = msg->wdp_RenderRect->MaxY - msg->wdp_RenderRect->MinY + 1;
    cached->knobx      = msg->wdp_RenderRect->MinX - msg->wdp_PropRect->MinX;
    cached->knoby      = msg->wdp_RenderRect->MinY - msg->wdp_PropRect->MinY;
}

static ULONG HasTitleBarChanged(struct CachedTitleBar *cached, struct Window *window)
{
    ULONG len = 0;
    
    /* More restrictive tests */

    /* If the window size has changed */
    if (cached->width != window->Width)
        return CHANGE_SIZE_CHANGE;
    if (cached->height != window->Height)
        return CHANGE_SIZE_CHANGE;
    /* A new screen font keeps the outer size but not the bar height: the
     * cached bitmap is exactly BorderTop rows high. */
    if (cached->bordertop != window->BorderTop)
        return CHANGE_SIZE_CHANGE;

    /* If there is no cached bitmap at all (this can happen NOT only at first call) */
    if (cached->bm == NULL)
        return CHANGE_SIZE_CHANGE;
    
    /* Less restrictive tests */

    /* If the window activity status has changed */
    if (cached->windowflags ^ (window->Flags & (WFLG_WINDOWACTIVE | WFLG_TOOLBOX)))
        return CHANGE_NO_SIZE_CHANGE;

    /* If the window title/title contents has changed */
    if (window->Title)
        len = strlen(window->Title);

    if (cached->titlelen != len)
        return CHANGE_NO_SIZE_CHANGE;
    
    if (cached->title)
    {
        /* At this point:
            - length of both buffer is guarateed to be the same, see previous test
            - both buffers are allocated
         */
        ULONG i;
        for (i = 0; i < len; i++)
            if (cached->title[i] != window->Title[i])
                return CHANGE_NO_SIZE_CHANGE;
    }

    return CHANGE_NO_CHANGE;
}

static VOID CacheTitleBar(struct CachedTitleBar *cached, struct Window *window, struct BitMap *bitmap)
{
    ULONG len = 0;

    cached->bm          = bitmap;
    cached->width       = window->Width;
    cached->height      = window->Height;
    cached->bordertop   = window->BorderTop;
    cached->windowflags = (window->Flags & (WFLG_WINDOWACTIVE | WFLG_TOOLBOX));
    
    /* Cache the title */
    if (window->Title)
        len = strlen(window->Title);

    if (cached->titlelen != len)
    {
        if (cached->title)
            FreeVec(cached->title);
        cached->title = AllocVec(len + 1, MEMF_ANY | MEMF_CLEAR);
    }
    cached->titlelen    = len;
    strncpy(cached->title, window->Title, len);
}

static ULONG HasTitleBarShapeChanged(struct CachedTitleBarShape *cached, struct Window *window)
{
    /* More restrictive tests */

    /* If the window size has changed */
    if (cached->width != window->Width)
        return CHANGE_SIZE_CHANGE;
    if (cached->height != window->Height)
        return CHANGE_SIZE_CHANGE;

    /* If there is no cached shape at all (this can happen NOT only at first call) */
    if (cached->shape == NULL)
        return CHANGE_SIZE_CHANGE;

    /* Less restrictive tests */

    /* If the window activity status has changed */
    if (cached->windowflags ^ (window->Flags & (WFLG_WINDOWACTIVE | WFLG_TOOLBOX)))
        return CHANGE_NO_SIZE_CHANGE;

    return CHANGE_NO_CHANGE;
}

static VOID CacheTitleBarShape(struct CachedTitleBarShape *cached, struct Window *window, struct Region *shape)
{
    cached->shape       = shape;
    cached->width       = window->Width;
    cached->height      = window->Height;
    cached->windowflags = (window->Flags & (WFLG_WINDOWACTIVE | WFLG_TOOLBOX));
}

static int WriteTiledImageShape(BOOL fill, struct Window *win, struct DecorImageLUT8 *lut8, struct DecorImage *di, int sx, int sy, int sw, int sh, int xp, int yp, int dw, int dh)
{
    int     w = dw;
    int     x = xp;
    int     ddw;

    if (!di->ok) return xp;

    if ((sw == 0) || (dw == 0)) return xp;

    if (win)
    {
        if (x > win->Width) return xp;
        if ((x + w) > win->Width) w = win->Width - x;
    }

    while (w > 0)
    {
        ddw = sw;
        if (w < ddw) ddw = w;
        DWriteAlphaPixelArray(di, lut8, sx, sy, x, yp, ddw, dh);
        w -= ddw;
        x += ddw;
    }
    return x;
}

static void getrightgadgetsdimensions(struct windecor_data *data, struct Window *win, LONG *xs, LONG *xe)
{
    struct Gadget *g;

    LONG    x0 = 1000000;
    LONG    x1 = 0;
    UWORD   type;

    for (g = win->FirstGadget; g; g = g->NextGadget)
    {
        if ((g->Flags & GFLG_RELRIGHT) == GFLG_RELRIGHT)
        {
            type = g->GadgetType & GTYP_SYSTYPEMASK;
            if (data->dc->CloseGadgetOnRight)
            {
                if (((type & (GTYP_CLOSE | GTYP_WDEPTH | GTYP_WZOOM)) != 0) || (g->Activation & GACT_TOPBORDER))
                {
                    if (g->Width > 0)
                    {
                        if ((win->Width + g->LeftEdge) < x0) x0 = win->Width + g->LeftEdge;
                        if ((win->Width + g->LeftEdge + g->Width) > x1) x1 = win->Width + g->LeftEdge + g->Width;
                    }
                }
            }
            else
            {
                if (((type & (GTYP_WDEPTH | GTYP_WZOOM)) != 0)  || (g->Activation & GACT_TOPBORDER))
                {
                    if (g->Width > 0)
                    {
                        if ((win->Width + g->LeftEdge) < x0) x0 = win->Width + g->LeftEdge;
                        if ((win->Width + g->LeftEdge + g->Width) > x1) x1 = win->Width + g->LeftEdge + g->Width;
                    }
                }
            }
        }
    }
    if (x0 == 1000000) x0 = 0;
    *xs = x0;
    *xe = x1;
}

static void getleftgadgetsdimensions(struct windecor_data *data, struct Window *win, LONG *xs, LONG *xe)
{
    struct Gadget *g;

    LONG w = 0;
    LONG x0 = 1000000;
    LONG x1 = 0;
    for (g = win->FirstGadget; g; g = g->NextGadget)
    {
        w++;
        if (((g->Flags & GFLG_RELRIGHT) == 0) && (g->Activation & GACT_TOPBORDER))
        {
            if ((g->GadgetType & GTYP_WDRAGGING) == 0)
            {
                if (g->Width > 0)
                {
                    if (g->LeftEdge < x0) x0 = g->LeftEdge;
                    if ((g->LeftEdge + g->Width) > x1) x1 = g->LeftEdge + g->Width;
                }
            }
        }
    }
    if (x0 == 1000000) x0 = 0;
    *xs = x0;
    *xe = x1;
}

static void DrawShapePartialTitleBar(struct WindowData *wd, struct DecorImageLUT8 *shape, struct windecor_data *data, struct Window *window)
{
    LONG                xl0, xl1, xr0, xr1, defwidth;
    ULONG               textlen = 0, titlelen = 0, textpixellen = 0;
    struct TextExtent   te;

    BOOL                hastitle;
    BOOL                hastitlebar;
    LONG                dy;

    struct RastPort    *rp = &window->WScreen->RastPort;
    hastitle = window->Title != NULL ? TRUE : FALSE;
    hastitlebar = (window->BorderTop > 0) ? TRUE : FALSE;

    getleftgadgetsdimensions(data, window, &xl0, &xl1);
    getrightgadgetsdimensions(data, window, &xr0, &xr1);

    defwidth = (xl0 != xl1) ? data->dc->BarPreGadget_s : data->dc->BarPre_s;
    if(xr1 == 0)
    {
        xr1 = window->Width - data->dc->BarPre_s;
        xr0 = window->Width - data->dc->BarPre_s;
    }

    defwidth += (xl1 - xl0);

    defwidth += data->dc->BarJoinGB_s;
    defwidth += data->dc->BarJoinBT_s;
    defwidth += data->dc->BarJoinTB_s;
    defwidth += data->dc->BarJoinBG_s;
    defwidth += (xr1 - xr0);
    defwidth += (xr0 != xr1) ? data->dc->BarPostGadget_s : data->dc->BarPost_s;

    if (defwidth >= window->Width) hastitle = FALSE;
 
    if (hastitle)
    {
        titlelen = strlen(window->Title);
        textlen = TextFit(rp, window->Title, titlelen, &te, NULL, 1, window->Width-defwidth, window->BorderTop - 2);
        if (textlen)
        {
            textpixellen = te.te_Extent.MaxX - te.te_Extent.MinX + 1;
        }
    }

 
    if (wd->img_winbar_normal->ok && hastitlebar)
    {
        UWORD x = 0;
        UWORD barh =  window->BorderTop;
        struct DecorImage * img_winbar_normal = wd->img_winbar_normal;

        if (data->dc->BarHeight != barh)
            img_winbar_normal = DScaleDecorImage(wd->img_winbar_normal, wd->img_winbar_normal->w, 2 * barh);

        if (window->Flags & (WFLG_WINDOWACTIVE | WFLG_TOOLBOX))
            dy = 0;
        else
            dy = barh; /* Offset into source image */

        if (xl0 != xl1)
        {
            x = WriteTiledImageShape(data->dc->FillTitleBar, window, shape,img_winbar_normal, data->dc->BarPreGadget_o, dy, data->dc->BarPreGadget_s, barh, x, 0, data->dc->BarPreGadget_s, barh);
            if ((xl1-xl0) > 0) x = WriteTiledImageShape(data->dc->FillTitleBar, window, shape, img_winbar_normal, data->dc->BarLGadgetFill_o, dy, data->dc->BarLGadgetFill_s, barh, x, 0, xl1-xl0, barh);
        }
        else
        {
            x = WriteTiledImageShape(data->dc->FillTitleBar, window, shape, img_winbar_normal, data->dc->BarPre_o, dy, data->dc->BarPre_s, barh, x, 0, data->dc->BarPreGadget_s, barh);
        }
        x = WriteTiledImageShape(data->dc->FillTitleBar, window, shape, img_winbar_normal, data->dc->BarJoinGB_o, dy, data->dc->BarJoinGB_s, barh, x, 0, data->dc->BarJoinGB_s, barh);
        if (hastitle && (textlen > 0))
        {
            switch(data->TextAlign)
            {
                case WD_DWTA_CENTER:
                    //BarLFill
                    x = WriteTiledImageShape(data->dc->FillTitleBar, window, shape, img_winbar_normal, data->dc->BarLFill_o, dy, data->dc->BarLFill_s, barh, x, 0, 60, barh);
                    break;
                case WD_DWTA_RIGHT:
                    //BarLFill
                    break;
                default:
                case WD_DWTA_LEFT:
                    break;
            }
            x = WriteTiledImageShape(data->dc->FillTitleBar, window, shape, img_winbar_normal, data->dc->BarJoinBT_o, dy, data->dc->BarJoinBT_s, barh, x, 0, data->dc->BarJoinBT_s, barh);
            if (textpixellen > 0) x = WriteTiledImageShape(data->dc->FillTitleBar, window, shape, img_winbar_normal, data->dc->BarTitleFill_o, dy, data->dc->BarTitleFill_s, barh, x, 0, textpixellen, barh);
            x = WriteTiledImageShape(data->dc->FillTitleBar, window, shape, img_winbar_normal, data->dc->BarJoinTB_o, dy, data->dc->BarJoinTB_s, barh, x, 0, data->dc->BarJoinTB_s, barh);
        }
        x = WriteTiledImageShape(data->dc->FillTitleBar, window, shape, img_winbar_normal, data->dc->BarRFill_o, dy, data->dc->BarRFill_s, barh, x, 0, xr0 - x - data->dc->BarJoinBG_s, barh);
        x = WriteTiledImageShape(data->dc->FillTitleBar, window, shape, img_winbar_normal, data->dc->BarJoinBG_o, dy, data->dc->BarJoinBG_s, barh, x, 0, data->dc->BarJoinBG_s, barh);
        if ((xr1-xr0) > 0) x = WriteTiledImageShape(data->dc->FillTitleBar, window, shape, img_winbar_normal, data->dc->BarRGadgetFill_o, dy, data->dc->BarRGadgetFill_s, barh, x, 0, xr1-xr0, barh);
        if (xr0 != xr1)
        {
            x = WriteTiledImageShape(data->dc->FillTitleBar, window, shape, img_winbar_normal, data->dc->BarPostGadget_o, dy, data->dc->BarPostGadget_s, barh, x, 0, data->dc->BarPostGadget_s, barh);
        }
        else
        {
            x = WriteTiledImageShape(data->dc->FillTitleBar, window, shape, img_winbar_normal, data->dc->BarPost_o, dy, data->dc->BarPost_s, barh, x, 0, data->dc->BarPost_s, barh);
        }

        if (data->dc->BarHeight != barh)
            DDisposeImageContainer(img_winbar_normal);
    }
}

static VOID DrawPartialTitleBar(struct WindowData *wd, struct windecor_data *data, struct Window *window, struct RastPort *dst_rp, struct DrawInfo *dri, UWORD *pens)
{
    LONG                xl0, xl1, xr0, xr1, defwidth;
    ULONG               textlen = 0, titlelen = 0, textpixellen = 0;
    struct TextExtent   te;
    struct RastPort    *rp;
    struct DecorImage    *di = NULL;

    BOOL                hastitle;
    BOOL                hastitlebar;
    UWORD               textstart = 0;
    ULONG               color, s_col, e_col, arc;
    LONG                dy;
    LONG                pen = -1;
    ULONG               changetype = CHANGE_NO_CHANGE;
    struct BitMap       *cachedtitlebarbitmap = NULL;
    
    changetype = HasTitleBarChanged(&wd->tbar, window);
    if (changetype == CHANGE_SIZE_CHANGE)
    {
        if (wd->tbar.bm)
            FreeBitMap(wd->tbar.bm);
        wd->tbar.bm = NULL;
    }
    cachedtitlebarbitmap = wd->tbar.bm;
    
    if (changetype == CHANGE_NO_CHANGE)
    {
        BltBitMapRastPort(cachedtitlebarbitmap, 0, 0, dst_rp, 0, 0, window->Width, window->BorderTop, 0xc0);
        return;
    }
    
    /* Regenerate the bitmap */
    rp = CreateRastPort();
    if (rp)
    {
        /* Reuse the bitmap if there was no size change (ie. only change of state) */
        if (changetype == CHANGE_NO_SIZE_CHANGE)
            rp->BitMap = cachedtitlebarbitmap;
        else
            rp->BitMap = AllocBitMap(window->Width, window->BorderTop, 1, 0, window->WScreen->RastPort.BitMap);

        if (rp->BitMap == NULL)
        {
            FreeRastPort(rp);
            return;
        }

        SetFont(rp, dri->dri_Font);
    }
    else
        return;

    hastitle = window->Title != NULL ? TRUE : FALSE;
    hastitlebar = (window->BorderTop > 0) ? TRUE : FALSE;

    if (wd->img_border_normal->ok) di = wd->img_border_normal;

    if (di == NULL) data->dc->UseGradients = TRUE;

    color = 0x00cccccc;

    if (window->Flags & (WFLG_WINDOWACTIVE | WFLG_TOOLBOX))
    {
        s_col = data->dc->ActivatedGradientColor_s;
        e_col = data->dc->ActivatedGradientColor_e;
        arc = data->dc->ActivatedGradientColor_a;
        dy = 0;
        pen = wd->ActivePen;
    } else {
        s_col = data->dc->DeactivatedGradientColor_s;
        e_col = data->dc->DeactivatedGradientColor_e;
        arc = data->dc->DeactivatedGradientColor_a;
        dy = data->dc->BarHeight;
        if (!data->dc->UseGradients)
        {
            if (wd->img_border_deactivated->ok) di = wd->img_border_deactivated;
        }
        pen = wd->DeactivePen;
    }


    if (data->dc->FillTitleBar)
    {
        if (data->dc->UseGradients) FillPixelArrayGradient(pen, wd->truecolor, rp, 0, 0, window->Width - 1, window->Height - 1, 0, 0, window->Width, window->BorderTop, s_col, e_col, arc, 0, 0);
        else DHorizVertRepeatDecorImage(di, color, 0, 0, rp, 0, 0, window->Width, window->BorderTop);
    }

    getleftgadgetsdimensions(data, window, &xl0, &xl1);
    getrightgadgetsdimensions(data, window, &xr0, &xr1);
    defwidth = (xl0 != xl1) ? data->dc->BarPreGadget_s : data->dc->BarPre_s;
    if(xr1 == 0)
    {
        xr1 = window->Width - data->dc->BarPre_s;
        xr0 = window->Width - data->dc->BarPre_s;
    }

    defwidth += (xl1 - xl0);

    defwidth += data->dc->BarJoinGB_s;
    defwidth += data->dc->BarJoinBT_s;
    defwidth += data->dc->BarJoinTB_s;
    defwidth += data->dc->BarJoinBG_s;
    defwidth += (xr1 - xr0);
    defwidth += (xr0 != xr1) ? data->dc->BarPostGadget_s : data->dc->BarPost_s;

    if (defwidth >= window->Width) hastitle = FALSE;

    if (hastitle)
    {
        titlelen = strlen(window->Title);
        textlen = TextFit(rp, window->Title, titlelen, &te, NULL, 1, window->Width-defwidth, window->BorderTop - 2);
        if (textlen)
        {
            textpixellen = te.te_Extent.MaxX - te.te_Extent.MinX + 1;
        }
    }

    if (wd->img_winbar_normal->ok && hastitlebar)
    {
        UWORD x = 0;
        UWORD barh =  window->BorderTop;
        ULONG act;
        struct DecorImage * img_winbar_normal = wd->img_winbar_normal;

        if (data->dc->BarHeight != barh)
            img_winbar_normal = DScaleDecorImage(wd->img_winbar_normal, wd->img_winbar_normal->w, 2 * barh);

        /* Titlebar section subimage row: 0 = active, 1 = inactive */
        if (window->Flags & (WFLG_WINDOWACTIVE | WFLG_TOOLBOX))
            act = 0;
        else
            act = 1;

        if (xl0 != xl1)
        {
            x = RenderWinBarElement(wd, DECOR_ELEM_WinBarPreGadget, rp, img_winbar_normal, act, x, data->dc->BarPreGadget_s, barh, window->Width);
            if ((xl1-xl0) > 0) x = RenderWinBarElement(wd, DECOR_ELEM_WinBarLGadgetFill, rp, img_winbar_normal, act, x, xl1-xl0, barh, window->Width);
        }
        else
        {
            x = RenderWinBarElement(wd, DECOR_ELEM_WinBarPre, rp, img_winbar_normal, act, x, data->dc->BarPreGadget_s, barh, window->Width);
        }
        x = RenderWinBarElement(wd, DECOR_ELEM_WinBarJoinGB, rp, img_winbar_normal, act, x, data->dc->BarJoinGB_s, barh, window->Width);
        if (hastitle && (textlen > 0))
        {
            switch(data->TextAlign)
            {
                case WD_DWTA_CENTER:
                    //BarLFill
                    x = RenderWinBarElement(wd, DECOR_ELEM_WinBarLFill, rp, img_winbar_normal, act, x, 60, barh, window->Width);
                    break;
                case WD_DWTA_RIGHT:
                    //BarLFill
                    break;
                default:
                case WD_DWTA_LEFT:
                    break;
            }
            x = RenderWinBarElement(wd, DECOR_ELEM_WinBarJoinBT, rp, img_winbar_normal, act, x, data->dc->BarJoinBT_s, barh, window->Width);
            textstart = x;
            if (textpixellen > 0) x = RenderWinBarElement(wd, DECOR_ELEM_WinBarTitleFill, rp, img_winbar_normal, act, x, textpixellen, barh, window->Width);
            x = RenderWinBarElement(wd, DECOR_ELEM_WinBarJoinTB, rp, img_winbar_normal, act, x, data->dc->BarJoinTB_s, barh, window->Width);
        }
        /* Part right to window title */
        x = RenderWinBarElement(wd, DECOR_ELEM_WinBarRFill, rp, img_winbar_normal, act, x, xr0 - x - data->dc->BarJoinBG_s, barh, window->Width);
        x = RenderWinBarElement(wd, DECOR_ELEM_WinBarJoinBG, rp, img_winbar_normal, act, x, data->dc->BarJoinBG_s, barh, window->Width);
        if ((xr1-xr0) > 0) x = RenderWinBarElement(wd, DECOR_ELEM_WinBarRGadgetFill, rp, img_winbar_normal, act, x, xr1-xr0, barh, window->Width);
        if (xr0 != xr1)
        {
            x = RenderWinBarElement(wd, DECOR_ELEM_WinBarPostGadget, rp, img_winbar_normal, act, x, data->dc->BarPostGadget_s, barh, window->Width);
        }
        else
        {
            x = RenderWinBarElement(wd, DECOR_ELEM_WinBarPost, rp, img_winbar_normal, act, x, data->dc->BarPost_s, barh, window->Width);
        }

        if (data->dc->BarHeight != barh)
            DDisposeImageContainer(img_winbar_normal);
    }

    if ((textlen > 0) && hastitle)
    {
        SetAPen(rp, pens[(window->Flags & (WFLG_WINDOWACTIVE | WFLG_TOOLBOX)) ? FILLTEXTPEN : TEXTPEN]);
        SetDrMd(rp, JAM1);
        UWORD   tx = textstart;
        UWORD   tymax = window->BorderTop - (dri->dri_Font->tf_YSize - dri->dri_Font->tf_Baseline) - 1;
        UWORD   ty = (window->BorderTop + dri->dri_Font->tf_Baseline - 1) >> 1;
        if (ty > tymax) ty = tymax;

        if (wd->scaleData)
        {
            LONG unit = (wd->scaleData->scale + 50) / 100;
            LONG baseline = dri->dri_Font->tf_Baseline +
                ((LONG)window->BorderTop - dri->dri_Font->tf_YSize) / 2;
            LONG maximum = (LONG)window->BorderTop - dri->dri_Font->tf_YSize +
                dri->dri_Font->tf_Baseline - unit;
            LONG dx, dy;
            if (baseline > maximum) baseline = maximum;
            SetSoftStyle(rp, FS_NORMAL, AskSoftStyle(rp));
            if (wd->truecolor && (data->dc->TitleOutline || data->dc->TitleShadow))
            {
                SetRPAttrs(rp, RPTAG_PenMode, FALSE,
                    RPTAG_FgColor, data->dc->TitleColorShadow, TAG_DONE);
                if (data->dc->TitleOutline)
                {
                    for (dy = -unit; dy <= unit; dy += unit)
                        for (dx = -unit; dx <= unit; dx += unit)
                            if (dx || dy)
                            {
                                Move(rp, (LONG)tx + dx, baseline + dy);
                                Text(rp, window->Title, textlen);
                            }
                }
                else
                {
                    Move(rp, tx + unit, baseline + unit);
                    Text(rp, window->Title, textlen);
                }
                SetRPAttrs(rp, RPTAG_PenMode, FALSE,
                    RPTAG_FgColor, data->dc->TitleColorText, TAG_DONE);
            }
            Move(rp, tx, baseline);
            Text(rp, window->Title, textlen);
        }
        else if (!wd->truecolor || ((data->dc->TitleOutline == FALSE) && (data->dc->TitleShadow == FALSE)))
        {
            Move(rp, tx, ty);
            Text(rp, window->Title, textlen);
        }
        else if (data->dc->TitleOutline)
        {

                SetSoftStyle(rp, FSF_BOLD, AskSoftStyle(rp));
                SetRPAttrs(rp, RPTAG_PenMode, FALSE, RPTAG_FgColor, data->dc->TitleColorShadow, TAG_DONE);

                Move(rp, tx + 1, ty ); Text(rp, window->Title, textlen);
                Move(rp, tx + 2, ty ); Text(rp, window->Title, textlen);
                Move(rp, tx , ty ); Text(rp, window->Title, textlen);
                Move(rp, tx, ty + 1);  Text(rp, window->Title, textlen);
                Move(rp, tx, ty + 2);  Text(rp, window->Title, textlen);
                Move(rp, tx + 1, ty + 2);  Text(rp, window->Title, textlen);
                Move(rp, tx + 2, ty + 1);  Text(rp, window->Title, textlen);
                Move(rp, tx + 2, ty + 2);  Text(rp, window->Title, textlen);

                SetRPAttrs(rp, RPTAG_PenMode, FALSE, RPTAG_FgColor, data->dc->TitleColorText, TAG_DONE);
                Move(rp, tx + 1, ty + 1);
                Text(rp, window->Title, textlen);
                SetSoftStyle(rp, FS_NORMAL, AskSoftStyle(rp));
        }
        else
        {
                SetRPAttrs(rp, RPTAG_PenMode, FALSE, RPTAG_FgColor, data->dc->TitleColorShadow, TAG_DONE);
                Move(rp, tx + 1, ty + 1 );
                Text(rp, window->Title, textlen);

                SetRPAttrs(rp, RPTAG_PenMode, FALSE, RPTAG_FgColor, data->dc->TitleColorText, TAG_DONE);
                Move(rp, tx, ty);
                Text(rp, window->Title, textlen);

        }
    }

    BltBitMapRastPort(rp->BitMap, 0, 0, dst_rp, 0, 0, window->Width, window->BorderTop, 0xc0);

    /* Cache the actual bitmap */
    CacheTitleBar(&wd->tbar, window, rp->BitMap);

    FreeRastPort(rp);
}

static VOID DisposeWindowSkinning(struct windecor_data *data)
{

}

static BOOL InitWindowSkinning(struct windecor_data *data, struct DecorImages * di, struct DecorConfig * dc)
{
    if ((!di) || (!dc))
        return FALSE;
        
    data->dc = dc;

    data->TextAlign = WD_DWTA_LEFT;

    /* Set pointers to gadget images, used only to get gadget sizes as they
       are requested prior to creation of menu object */
    data->img_size      = di->img_size;
    data->img_close     = di->img_close;
    data->img_depth     = di->img_depth;
    data->img_zoom      = di->img_zoom;
    data->img_up        = di->img_up;
    data->img_down      = di->img_down;
    data->img_left      = di->img_left;
    data->img_right     = di->img_right;
    data->img_mui       = di->img_mui;
    data->img_popup     = di->img_popup;
    data->img_snapshot  = di->img_snapshot;
    data->img_iconify   = di->img_iconify;
    data->img_lock      = di->img_lock;

    if (data->img_close && data->img_depth && data->img_zoom && data->img_up &&
        data->img_down && data->img_left && data->img_right)
        return TRUE;

    DisposeWindowSkinning(data);
    return FALSE;
}

/**************************************************************************************************/
static IPTR windecor_new(Class *cl, Object *obj, struct opSet *msg)
{
    struct windecor_data *data;

    D(bug("[Decoration:Win] %s(tags @ 0x%p)\n", __func__, msg->ops_AttrList));

    obj = (Object *)DoSuperMethodA(cl, obj, (Msg)msg);
    if (obj)
    {
         data = INST_DATA(cl, obj);

         struct DecorImages * di = (struct DecorImages *) GetTagData(WDA_DecorImages, (IPTR)NULL, msg->ops_AttrList);
         struct DecorConfig * dc = (struct DecorConfig *) GetTagData(WDA_DecorConfig, (IPTR)NULL, msg->ops_AttrList);

        D(bug("[Decoration:Win] %s: DecorImages @ 0x%p\n", __func__, di));
        D(bug("[Decoration:Win] %s: DecorConfig @ 0x%p\n", __func__, dc));

         if (!InitWindowSkinning(data, di, dc))
         {
             CoerceMethod(cl, obj, OM_DISPOSE);
             obj = NULL;
         }
     }

    return (IPTR)obj;
}

/**************************************************************************************************/
static IPTR windecor_dispose(Class *cl, Object *obj, struct opSet *msg)
{
    struct windecor_data *data = INST_DATA(cl, obj);

    DisposeWindowSkinning(data);

    return 1;
}

/**************************************************************************************************/
static IPTR windecor_get(Class *cl, Object *obj, struct opGet *msg)
{
    switch(msg->opg_AttrID)
    {
        case WDA_PreparedScaleWindow:
            *msg->opg_Storage = OCLASS(obj) == cl;
            break;
        case WDA_TrueColorOnly:
            *msg->opg_Storage = TRUE;
            break;
        default:
            return DoSuperMethodA(cl, obj, (Msg)msg);
    }
    return 1;
}

/**************************************************************************************************/
/* The gadgets drawn over the title bar, whose background is the cached bar. */
static BOOL titlebar_image(ULONG which)
{
    switch (which)
    {
        case CLOSEIMAGE: case MUIIMAGE: case POPUPIMAGE: case SNAPSHOTIMAGE:
        case LOCKIMAGE: case ICONIFYIMAGE: case DEPTHIMAGE: case ZOOMIMAGE:
            return TRUE;
    }
    return FALSE;
}

/* Restore the cached title bar under a title gadget. The bitmap holds only
 * the bar (cached width x bordertop): a rectangle elsewhere in the window,
 * such as the size gadget's corner, would read far outside it. */
static VOID reblit_titlebar(struct WindowData *wd, struct RastPort *rp,
    LONG left, LONG top, LONG width, LONG height)
{
    LONG right, bottom;
    if (!wd || !wd->tbar.bm || !rp || width <= 0 || height <= 0) return;
    right = left + width; bottom = top + height;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > (LONG)wd->tbar.width) right = wd->tbar.width;
    if (bottom > (LONG)wd->tbar.bordertop) bottom = wd->tbar.bordertop;
    if (left >= right || top >= bottom) return;
    BltBitMapRastPort(wd->tbar.bm, left, top, rp, left, top, right - left, bottom - top, 0xc0);
}

static IPTR windecor_draw_sysimage(Class *cl, Object *obj, struct wdpDrawSysImage *msg)
{
    struct RastPort        *rp = msg->wdp_RPort;
    struct WindowData      *wd = (struct WindowData *) msg->wdp_UserBuffer;
    struct DecoratorElement *elem;
    LONG                    state = msg->wdp_State;
    LONG                    left = msg->wdp_X;
    LONG                    top = msg->wdp_Y;
    LONG                    width = msg->wdp_Width;
    LONG                    height = msg->wdp_Height;
    ULONG                   elemid;
    BOOL                    checkok = FALSE;
    BOOL                    titlegadget = FALSE;

    /* Gadget centering offsets are carried by the elements as pads */
    switch(msg->wdp_Which)
    {
        case SIZEIMAGE:
            elemid = DECOR_ELEM_WinSize;
            checkok = TRUE;
            break;

        case CLOSEIMAGE:
            elemid = DECOR_ELEM_WinClose;
            checkok = TRUE;
            titlegadget = TRUE;
            break;

        case MUIIMAGE:
            elemid = DECOR_ELEM_WinMUI;
            checkok = TRUE;
            titlegadget = TRUE;
            break;

        case POPUPIMAGE:
            elemid = DECOR_ELEM_WinPopup;
            checkok = TRUE;
            titlegadget = TRUE;
            break;

        case SNAPSHOTIMAGE:
            elemid = DECOR_ELEM_WinSnapshot;
            checkok = TRUE;
            titlegadget = TRUE;
            break;

        case LOCKIMAGE:
            elemid = DECOR_ELEM_WinLock;
            checkok = TRUE;
            titlegadget = TRUE;
            break;

        case ICONIFYIMAGE:
            elemid = DECOR_ELEM_WinIconify;
            checkok = TRUE;
            titlegadget = TRUE;
            break;

        case DEPTHIMAGE:
            elemid = DECOR_ELEM_WinDepth;
            checkok = TRUE;
            titlegadget = TRUE;
            break;

        case ZOOMIMAGE:
            elemid = DECOR_ELEM_WinZoom;
            checkok = TRUE;
            titlegadget = TRUE;
            break;

        case UPIMAGE:
            elemid = DECOR_ELEM_WinUp;
            break;

        case DOWNIMAGE:
            elemid = DECOR_ELEM_WinDown;
            break;

        case LEFTIMAGE:
            elemid = DECOR_ELEM_WinLeft;
            break;

        case RIGHTIMAGE:
            elemid = DECOR_ELEM_WinRight;
            break;

        default:
            return DoSuperMethodA(cl, obj, (Msg)msg);
    }

    elem = &wd->dts->dts_Elements[elemid];
    if (!elem->de_Image || (checkok && !elem->de_Image->ok))
        return DoSuperMethodA(cl, obj, (Msg)msg);

    /* Reblit title bar (title gadgets have no pad offsets) */
    if (titlegadget)
        reblit_titlebar(wd, rp, left, top, width, height);

    if (titlegadget)
        DRenderElement(elem, rp, state, left, top, -1, height, 0);
    else
        DRenderElement(elem, rp, state, left, top, -1, -1, 0);

    return TRUE;
}

/**************************************************************************************************/
static IPTR windecor_draw_winborder(Class *cl, Object *obj, struct wdpDrawWinBorder *msg)
{
    struct windecor_data   *data = INST_DATA(cl, obj);
    struct RastPort        *rp = msg->wdp_RPort;
    struct Window          *window = msg->wdp_Window;
    struct WindowData      *wd = (struct WindowData *) msg->wdp_UserBuffer;
    struct DecorImage        *di = NULL;
    UWORD                  *pens = msg->wdp_Dri->dri_Pens;
    ULONG                   bc, color, s_col, e_col, arc;
    UWORD                   bl, bt, br, bb, ww, wh;
    LONG    pen = -1;
    LONG unit = wd->scaleData ? (wd->scaleData->scale + 50) / 100 : 1;

    D(bug("[Decoration:Win] %s(0x%p)\n", __func__, obj);)
    D(bug("[Decoration:Win] %s: window @ 0x%p\n", __func__, window);)

    if (wd->scaleData) data = &wd->scaleData->renderer;

    if (wd->img_border_normal->ok)
        di = wd->img_border_normal;

    if (di == NULL)
        data->dc->UseGradients = TRUE;

    BOOL    tc = wd->truecolor;

    LONG    dpen = pens[SHADOWPEN];
    LONG    lpen = pens[SHINEPEN];
    LONG    mpen = pens[SHINEPEN];

    bl = window->BorderLeft;
    bt = window->BorderTop;
    bb = window->BorderBottom;
    br = window->BorderRight;
    ww = window->Width;
    wh = window->Height;

    color = 0x00cccccc;

    if (window->Flags & (WFLG_WINDOWACTIVE | WFLG_TOOLBOX))
    {
        D(bug("[Decoration:Win] %s: ACTIVE\n", __func__);)
        pen = wd->ActivePen;
        s_col = data->dc->ActivatedGradientColor_s;
        e_col = data->dc->ActivatedGradientColor_e;
        arc = data->dc->ActivatedGradientColor_a;
        bc = data->dc->BaseColors_a;
    } else {
        D(bug("[Decoration:Win] %s: INACTIVE\n", __func__);)
        pen = wd->DeactivePen;
        s_col = data->dc->DeactivatedGradientColor_s;
        e_col = data->dc->DeactivatedGradientColor_e;
        arc = data->dc->DeactivatedGradientColor_a;
        bc = data->dc->BaseColors_d;
        if (!data->dc->UseGradients)
        {
            if (wd->img_border_deactivated->ok) di = wd->img_border_deactivated;
        }
    }

    /* Draw title bar */
    DrawPartialTitleBar(wd, data, window, rp, msg->wdp_Dri, pens);

    /* Draw left, right and bottom frames */
    if (!(msg->wdp_Flags & WDF_DWB_TOP_ONLY))
    {
        UBYTE * buf = NULL;
        int         overlap = 0;

        if (data->dc->WinFrameStyle > 0)
            overlap = unit * 2;
        
        if (data->dc->UseGradients)
        {
            /* Create one pixel wide buffer */
            buf = AllocVec(1 * window->Height * 3, MEMF_ANY | MEMF_CLEAR);
            
            if (!buf) return FALSE;
            /* Fill the buffer with gradient */
            DFillMemoryBufferRGBGradient(buf, pen, 0, 0, window->Width - 1, window->Height - 1, 0, 0,
                1, window->Height , s_col, e_col, arc);
        }
    
        if (data->dc->UseGradients)
        {
            /* Reuse the buffer for blitting frames */
            if (window->BorderLeft > overlap) DHorizRepeatBuffer(buf, window->BorderTop, pen, wd->truecolor, rp,
                                            0, window->BorderTop,
                                            window->BorderLeft, window->Height - window->BorderTop);
            if (window->BorderRight > overlap) DHorizRepeatBuffer(buf, window->BorderTop, pen, wd->truecolor, rp,
                                            window->Width - window->BorderRight, window->BorderTop,
                                            window->BorderRight, window->Height - window->BorderTop);
            if (window->BorderBottom > overlap) DHorizRepeatBuffer(buf, window->Height - window->BorderBottom, pen, wd->truecolor, rp,
                                            0, window->Height - window->BorderBottom,
                                            window->Width, window->BorderBottom);
        }
        else
        {
            if (window->BorderLeft > overlap) DHorizVertRepeatDecorImage(di, color, 0, window->BorderTop, rp,
                                            0, window->BorderTop,
                                            window->BorderLeft - 1, window->Height - window->BorderTop);
            if (window->BorderRight > overlap) DHorizVertRepeatDecorImage(di, color, window->Width - window->BorderRight , window->BorderTop, rp,
                                            window->Width - window->BorderRight , window->BorderTop,
                                            window->BorderRight, window->Height - window->BorderTop);
            if (window->BorderBottom > overlap) DHorizVertRepeatDecorImage(di, color, 0, window->Height - window->BorderBottom, rp,
                                            0, window->Height - window->BorderBottom,
                                            window->Width, window->BorderBottom);
        }

        /* Shading borders */
        if (data->dc->WinFrameStyle > 0)
        {
            int bbt = bt;

            if (bl > 0) ShadeLine(dpen, tc, data->dc->UseGradients, rp, di, bc, data->dc->ShadeValues_d, bbt, 0, bbt, 0, wh - unit, unit);
            if (bb > 0) ShadeLine(dpen, tc, data->dc->UseGradients, rp, di, bc, data->dc->ShadeValues_d, wh - unit, 0, wh - unit, ww - unit, wh - unit, unit);
            if (br > 0) ShadeLine(dpen, tc, data->dc->UseGradients, rp, di, bc, data->dc->ShadeValues_d, bbt , ww - unit, bbt , ww - unit, wh - unit, unit);
            if (bl > unit) ShadeLine(dpen, tc, data->dc->UseGradients, rp, di, bc, data->dc->ShadeValues_d, bbt, bl - unit, bbt, bl - unit, wh - bb, unit);
            if (bb > unit) ShadeLine(dpen, tc, data->dc->UseGradients, rp, di, bc, data->dc->ShadeValues_d, wh - bb, bl - unit, wh - bb, ww - br, wh - bb, unit);
            if (br > unit) ShadeLine(dpen, tc, data->dc->UseGradients, rp, di, bc, data->dc->ShadeValues_d, bbt , ww - br, bbt , ww - br, wh - bb, unit);
            if (bl > unit * 2) ShadeLine(lpen, tc, data->dc->UseGradients, rp, di, bc, data->dc->ShadeValues_l, bbt, unit, bbt, unit, wh - unit * 2, unit);
            if (bl > unit * 3) {
                if (bb > unit) ShadeLine(mpen, tc, data->dc->UseGradients, rp, di, bc, data->dc->ShadeValues_m, bbt, bl - unit * 2, bbt, bl - unit * 2, wh - bb + unit, unit);
                else ShadeLine(mpen, tc, data->dc->UseGradients, rp, di, bc, data->dc->ShadeValues_m, bbt, bl - unit * 2, bbt, bl - unit * 2, wh - bb, unit);
            }
            if (br > unit * 2) ShadeLine(mpen, tc, data->dc->UseGradients, rp, di, bc, data->dc->ShadeValues_m, bbt, ww - unit * 2, bbt, ww - unit * 2, wh - unit * 2, unit);
            if (bb > unit * 2) ShadeLine(mpen, tc, data->dc->UseGradients, rp, di, bc, data->dc->ShadeValues_m, wh - unit * 2, unit, wh - unit * 2, ww - unit * 2, wh - unit * 2, unit);
            if (bb > unit * 3) {
                if ((bl > 0) && (br > 0)) ShadeLine(lpen, tc, data->dc->UseGradients, rp, di, bc, data->dc->ShadeValues_l, wh - bb + unit, bl, wh - bb + unit, ww - br, wh - bb + unit, unit);
            }
            if (br > unit * 3) {
                if (bb > unit) ShadeLine(lpen, tc, data->dc->UseGradients, rp, di, bc, data->dc->ShadeValues_l, bbt, ww - br + unit, bbt, ww - br + unit, wh - bb + unit, unit);
            }
        }
        FreeVec(buf);
    }
    return TRUE;
}

/**************************************************************************************************/
static IPTR windecor_layout_bordergadgets(Class *cl, Object *obj, struct wdpLayoutBorderGadgets *msg,
    struct wdpScaleRefusal *refusal)
{
    struct windecor_data   *data = INST_DATA(cl, obj);
    struct Window          *window = msg->wdp_Window;
    struct Gadget          *gadget = msg->wdp_Gadgets;
    struct Gadget          *draggadget = NULL;
    struct WindowData      *wd = (struct WindowData *) msg->wdp_UserBuffer;
    ULONG                   eb = msg->wdp_ExtraButtons;

    BOOL                    hasdepth;
    BOOL                    haszoom;
    BOOL                    hasclose;
    LONG                    width;
    LONG                    refheight = window->BorderTop;

    if (wd->scaleData) data = &wd->scaleData->renderer;

    if (wd->scaleData)
    {
        struct wdpLayoutScaleBorderGadgets base = {0};
        base.base = *msg;
        base.base.MethodID = WDM_LAYOUTSCALEBORDERGADGETS;
        base.context = wd->scaleData->context;
        base.logical = wd->scaleData->scale == 100;
        base.refusal = refusal;
        if (!DoSuperMethodA(cl, obj, (Msg)&base))
        {
            if (refusal && refusal->wsr_Code == WDP_REFUSE_NONE)
                window_scale_refuse(refusal, WDP_REFUSE_SUPERCLASS,
                    WDM_LAYOUTSCALEBORDERGADGETS, 0, 0, NULL);
            return FALSE;
        }
    }
    else DoSuperMethodA(cl, obj, (Msg)msg);

    hasclose = (window->Flags & WFLG_CLOSEGADGET) ? TRUE : FALSE;
    hasdepth = (window->Flags & WFLG_DEPTHGADGET) ? TRUE : FALSE;
    haszoom = ((window->Flags & WFLG_HASZOOM) || ((window->Flags & WFLG_SIZEGADGET) && hasdepth)) ? TRUE : FALSE;


    if ((msg->wdp_Flags & WDF_LBG_SYSTEMGADGET) != 0)
    {
        if (gadget->GadgetType == GTYP_CUSTOMGADGET)
        {
            switch(gadget->GadgetID)
            {
                case ETI_MUI:
                    if (wd->img_mui->ok)
                    {
                        if (data->dc->GadgetsThreeState) width = (wd->img_mui->w / 3); else width = (wd->img_mui->w >> 2);

                        gadget->Width = width;
                        gadget->TopEdge = (refheight - gadget->Height) / 2;

                        if (haszoom)
                        {
                            if (data->dc->GadgetsThreeState) width += (wd->img_zoom->w / 3); else width += (wd->img_zoom->w >> 2);
                        }
                        if (hasclose && data->dc->CloseGadgetOnRight)
                        {
                            if (data->dc->GadgetsThreeState) width += (wd->img_close->w / 3); else width += (wd->img_close->w >> 2);
                        }
                        if (hasdepth)
                        {
                            if (data->dc->GadgetsThreeState) width += (wd->img_depth->w / 3); else width += (wd->img_depth->w >> 2);
                            gadget->LeftEdge = -data->dc->BarPostGadget_s - width;
                        }
                        else
                        {
                            gadget->LeftEdge = -data->dc->BarPostGadget_s - width;
                        }
                        gadget->Flags &= ~GFLG_RELWIDTH;
                        gadget->Flags |= GFLG_RELRIGHT;

                    }
                    break;

                case ETI_PopUp:
                    if (wd->img_popup->ok)
                    {
                        if (data->dc->GadgetsThreeState) width = (wd->img_popup->w / 3); else width = (wd->img_popup->w >> 2);

                        gadget->Width = width;
                        gadget->TopEdge = (refheight - gadget->Height) / 2;

                        if ((eb & ETG_MUI) != 0)
                        {
                            if (wd->img_mui->ok)
                            {
                                if (data->dc->GadgetsThreeState) width += (wd->img_mui->w / 3); else width += (wd->img_mui->w >> 2);
                            }
                        }

                        if (haszoom)
                        {
                            if (data->dc->GadgetsThreeState) width += (wd->img_zoom->w / 3); else width += (wd->img_zoom->w >> 2);
                        }

                        if (hasclose && data->dc->CloseGadgetOnRight)
                        {
                            if (data->dc->GadgetsThreeState) width += (wd->img_close->w / 3); else width += (wd->img_close->w >> 2);
                        }
                        if (hasdepth)
                        {
                            if (data->dc->GadgetsThreeState) width += (wd->img_depth->w / 3); else width += (wd->img_depth->w >> 2);
                            gadget->LeftEdge = -data->dc->BarPostGadget_s - width;
                        }
                        else
                        {
                            gadget->LeftEdge = -data->dc->BarPostGadget_s - width;
                        }
                        gadget->Flags &= ~GFLG_RELWIDTH;
                        gadget->Flags |= GFLG_RELRIGHT;

                    }
                    break;

                case ETI_Snapshot:
                    if (wd->img_snapshot->ok)
                    {
                        if (data->dc->GadgetsThreeState) width = (wd->img_snapshot->w / 3); else width = (wd->img_snapshot->w >> 2);

                        gadget->Width = width;
                        gadget->TopEdge = (refheight - gadget->Height) / 2;

                        if ((eb & ETG_MUI) != 0)
                        {

                            if (wd->img_mui->ok)
                            {
                                if (data->dc->GadgetsThreeState) width += (wd->img_mui->w / 3); else width += (wd->img_mui->w >> 2);
                            }
                        }

                        if ((eb & ETG_POPUP) != 0)
                        {
                            if (wd->img_popup->ok)
                            {
                                if (data->dc->GadgetsThreeState) width += (wd->img_popup->w / 3); else width += (wd->img_popup->w >> 2);
                            }
                        }

                        if (haszoom)
                        {
                            if (data->dc->GadgetsThreeState) width += (wd->img_zoom->w / 3); else width += (wd->img_zoom->w >> 2);
                        }

                        if (hasclose && data->dc->CloseGadgetOnRight)
                        {
                            if (data->dc->GadgetsThreeState) width += (wd->img_close->w / 3); else width += (wd->img_close->w >> 2);
                        }

                        if (hasdepth)
                        {
                            if (data->dc->GadgetsThreeState) width += (wd->img_depth->w / 3); else width += (wd->img_depth->w >> 2);
                            gadget->LeftEdge = -data->dc->BarPostGadget_s - width;
                        }
                        else
                        {
                            gadget->LeftEdge = -data->dc->BarPostGadget_s - width;
                        }

                        gadget->Flags &= ~GFLG_RELWIDTH;
                        gadget->Flags |= GFLG_RELRIGHT;

                    }
                    break;

                case ETI_Iconify:
                    if (wd->img_iconify->ok)
                    {
                        if (data->dc->GadgetsThreeState) width = (wd->img_iconify->w / 3); else width = (wd->img_iconify->w >> 2);

                        gadget->Width = width;
                        gadget->TopEdge = (refheight - gadget->Height) / 2;

                        if ((eb & ETG_MUI) != 0)
                        {
                            if (wd->img_mui->ok)
                            {
                                if (data->dc->GadgetsThreeState) width += (wd->img_mui->w / 3); else width += (wd->img_mui->w >> 2);
                            }
                        }

                        if ((eb & ETG_POPUP) != 0)
                        {
                            if (wd->img_popup->ok)
                            {
                                if (data->dc->GadgetsThreeState) width += (wd->img_popup->w / 3); else width += (wd->img_popup->w >> 2);
                            }
                        }

                        if ((eb & ETG_SNAPSHOT) != 0)
                        {
                            if (wd->img_snapshot->ok)
                            {
                                if (data->dc->GadgetsThreeState) width += (wd->img_snapshot->w / 3); else width += (wd->img_snapshot->w >> 2);
                            }
                        }

                        if (haszoom)
                        {
                            if (data->dc->GadgetsThreeState) width += (wd->img_zoom->w / 3); else width += (wd->img_zoom->w >> 2);
                        }

                        if (hasclose && data->dc->CloseGadgetOnRight)
                        {
                            if (data->dc->GadgetsThreeState) width += (wd->img_close->w / 3); else width += (wd->img_close->w >> 2);
                        }
                        if (hasdepth)
                        {
                            if (data->dc->GadgetsThreeState) width += (wd->img_depth->w / 3); else width += (wd->img_depth->w >> 2);
                            gadget->LeftEdge = -data->dc->BarPostGadget_s - width;
                        }
                        else
                        {
                            gadget->LeftEdge = -data->dc->BarPostGadget_s - width;
                        }
                        gadget->Flags &= ~GFLG_RELWIDTH;
                        gadget->Flags |= GFLG_RELRIGHT;

                    }
                    break;

                case ETI_Lock:
                    if (wd->img_lock->ok)
                    {
                        if (data->dc->GadgetsThreeState) width = (wd->img_lock->w / 3); else width = (wd->img_lock->w >> 2);

                        gadget->Width = width;
                        gadget->TopEdge = (refheight - gadget->Height) / 2;

                        if ((eb & ETG_MUI) != 0)
                        {
                            if (wd->img_mui->ok)
                            {
                                if (data->dc->GadgetsThreeState) width += (wd->img_mui->w / 3); else width += (wd->img_mui->w >> 2);
                            }
                        }

                        if ((eb & ETG_POPUP) != 0)
                        {
                            if (wd->img_popup->ok)
                            {
                                if (data->dc->GadgetsThreeState) width += (wd->img_popup->w / 3); else width += (wd->img_popup->w >> 2);
                            }
                        }

                        if ((eb & ETG_SNAPSHOT) != 0)
                        {
                            if (wd->img_snapshot->ok)
                            {
                                if (data->dc->GadgetsThreeState) width += (wd->img_snapshot->w / 3); else width += (wd->img_snapshot->w >> 2);
                            }
                        }

                        if ((eb & ETG_ICONIFY) != 0)
                        {
                            if (wd->img_iconify->ok)
                            {
                                if (data->dc->GadgetsThreeState) width += (wd->img_iconify->w / 3); else width += (wd->img_iconify->w >> 2);
                            }
                        }

                        if (haszoom)
                        {
                            if (data->dc->GadgetsThreeState) width += (wd->img_zoom->w / 3); else width += (wd->img_zoom->w >> 2);
                        }

                        if (hasclose && data->dc->CloseGadgetOnRight)
                        {
                            if (data->dc->GadgetsThreeState) width += (wd->img_close->w / 3); else width += (wd->img_close->w >> 2);
                        }
                        if (hasdepth)
                        {
                            if (data->dc->GadgetsThreeState) width += (wd->img_depth->w / 3); else width += (wd->img_depth->w >> 2);
                            gadget->LeftEdge = -data->dc->BarPostGadget_s - width;
                        }
                        else
                        {
                            gadget->LeftEdge = -data->dc->BarPostGadget_s - width;
                        }
                        gadget->Flags &= ~GFLG_RELWIDTH;
                        gadget->Flags |= GFLG_RELRIGHT;

                    }
                    break;

            }
        }
        else
        {
            switch(gadget->GadgetType & GTYP_SYSTYPEMASK)
            {
                case GTYP_CLOSE:
                    if (data->dc->GadgetsThreeState) width = (wd->img_close->w / 3); else width = (wd->img_close->w >> 2);
                    gadget->Width = width;
                    wd->closewidth = width;
                    if (data->dc->CloseGadgetOnRight)
                    {
                        gadget->Flags &= ~GFLG_RELWIDTH;
                        gadget->Flags |= GFLG_RELRIGHT;
                        gadget->LeftEdge = -data->dc->BarPostGadget_s - width;
                    }
                    else
                    {
                        gadget->LeftEdge = data->dc->BarPreGadget_s;
                    }
                    gadget->TopEdge = (refheight - gadget->Height) / 2;
                    break;
    
                case GTYP_WDEPTH:
                    if (data->dc->GadgetsThreeState) width = (wd->img_depth->w / 3); else width = (wd->img_depth->w >> 2);
                    gadget->Width = width;
                    wd->depthwidth = width;
                    if (hasclose && data->dc->CloseGadgetOnRight)
                    {
                        if (data->dc->GadgetsThreeState) width += (wd->img_close->w / 3); else width += (wd->img_close->w >> 2);
                    }
                    gadget->LeftEdge = -data->dc->BarPostGadget_s - width;
                    gadget->TopEdge = (refheight - gadget->Height) / 2;
                    gadget->Flags &= ~GFLG_RELWIDTH;
                    gadget->Flags |= GFLG_RELRIGHT;
                    break;
    
                case GTYP_WZOOM:
                    if (data->dc->GadgetsThreeState) width = (wd->img_zoom->w / 3); else width = (wd->img_zoom->w >> 2);
                    gadget->Width = width;
                    wd->zoomwidth = width;
                    gadget->TopEdge = (refheight - gadget->Height) / 2;
                    if (hasclose && data->dc->CloseGadgetOnRight)
                    {
                        if (data->dc->GadgetsThreeState) width += (wd->img_close->w / 3); else width += (wd->img_close->w >> 2);
                    }
                    if (hasdepth)
                    {
                        if (data->dc->GadgetsThreeState) width += (wd->img_depth->w / 3); else width += (wd->img_depth->w >> 2);
                        gadget->LeftEdge = -data->dc->BarPostGadget_s - width;
                    }
                    else
                    {
                        gadget->LeftEdge = -data->dc->BarPostGadget_s - width;
                    }
                    gadget->Flags &= ~GFLG_RELWIDTH;
                    gadget->Flags |= GFLG_RELRIGHT;
    
                    break;
    
                case GTYP_WDRAGGING:
                    break;
    
            }
        }
        if (wd->scaleData && (gadget->Flags & GFLG_RELRIGHT) &&
            (gadget->GadgetType & GTYP_SYSTYPEMASK) != GTYP_SIZING &&
            (gadget->GadgetType & GTYP_SYSTYPEMASK) != GTYP_WDRAGGING)
            /* Relative coordinates subtract one physical pixel at use time.
             * Preserve the original logical-pixel inset at every scale. */
            gadget->LeftEdge -= (wd->scaleData->scale + 50) / 100 - 1;
        return TRUE;
    }

    int sysrgad = -data->dc->BarPostGadget_s - 1;

    if (data->dc->CloseGadgetOnRight && hasclose) sysrgad -= wd->closewidth;
    if (hasdepth) sysrgad -= wd->depthwidth;
    if (haszoom) sysrgad -= wd->zoomwidth;
    while(gadget)
    {
        if ((gadget->GadgetType & GTYP_SYSTYPEMASK) == 0)
        {
            switch(gadget->GadgetType & GTYP_SYSTYPEMASK)
            {
                case GTYP_WDRAGGING:
                   break;

                default:
                if ((gadget->Flags & GFLG_EXTENDED) != 0)
                {
                    if ((((struct ExtGadget *) gadget)->MoreFlags & GMORE_BOOPSIGADGET) != 0)
                    {
                        IPTR rtsm = 0;
                        GetAttr(GA_RightBorder, (Object *) gadget, &rtsm);
                        if (rtsm)
                        {
                            if (GetAttr(PGA_Top, (Object *) gadget, &rtsm))
                            {
                                SetAttrs((Object *) gadget, GA_RelRight, - data->dc->RightBorderGadgets + ((data->dc->RightBorderGadgets - (wd->img_verticalcontainer->w >> 1) + 1) >> 1) + 1, GA_Width, wd->img_verticalcontainer->w >> 1, TAG_DONE);
                            }
                            else
                            {
                                GetAttr(GA_Width, (Object *) gadget, &rtsm);
                                SetAttrs((Object *) gadget, GA_RelRight, - data->dc->RightBorderGadgets + ((data->dc->RightBorderGadgets - rtsm + 1) >> 1) + 1, TAG_DONE);
                            }
                        }
                        else
                        {
                            GetAttr(GA_BottomBorder, (Object *) gadget, &rtsm);
                            if (rtsm)
                            {
                                if (GetAttr(PGA_Top, (Object *) gadget, &rtsm))
                                {
                                    SetAttrs((Object *) gadget, GA_RelBottom, - data->dc->BottomBorderGadgets + ((data->dc->BottomBorderGadgets - (wd->img_horizontalcontainer->h >> 1) + 1)  >> 1) +1, GA_Height, (wd->img_horizontalcontainer->h >> 1), TAG_DONE);
                                }
                                else
                                {
                                    GetAttr(GA_Height, (Object *) gadget, &rtsm);
                                    SetAttrs((Object *) gadget, GA_RelBottom, - data->dc->BottomBorderGadgets + ((data->dc->BottomBorderGadgets - rtsm + 1) >> 1) + 1, TAG_DONE);
                                }
                            }
                        }
                    }
                }
                break;
            }
        }
        if (msg->wdp_Flags & WDF_LBG_MULTIPLE)
        {
            gadget = gadget->NextGadget;
        }
        else
        {
            gadget = NULL;
        }
    }
    gadget = msg->wdp_Gadgets;

    while(gadget)
    {
        if ((gadget->GadgetType & GTYP_SYSTYPEMASK) == 0)
        {
            if ((gadget->Activation & GACT_TOPBORDER) != 0)
            {
                if ((gadget->Flags & GFLG_RELRIGHT) != 0)
                {
                    gadget->TopEdge = (refheight - gadget->Height) / 2;
                    sysrgad -= gadget->Width;
                    gadget->LeftEdge = sysrgad;
                }
            }
        }
        gadget = gadget->NextGadget;
    }

    gadget = msg->wdp_Gadgets;

    if ((msg->wdp_Flags & WDF_LBG_SYSTEMGADGET) != 0) while(gadget)
    {
        switch(gadget->GadgetType & GTYP_SYSTYPEMASK)
        {
            case GTYP_WDRAGGING:
                gadget->Width = sysrgad;
                if (hasclose && !data->dc->CloseGadgetOnRight)
                {
                    gadget->Width -= data->dc->BarPreGadget_s;
                    if (data->dc->GadgetsThreeState) gadget->Width -= (wd->img_close->w / 3); else gadget->Width -= (wd->img_close->w >> 2);
                }
                break;
        }
        gadget = gadget->NextGadget;
    }

    if (draggadget)
    {
    }

    return TRUE;
}

/**************************************************************************************************/
static IPTR windecor_draw_borderpropback(Class *cl, Object *obj, struct wdpDrawBorderPropBack *msg)
{
    /* Simply return, we need to render the back in the knob method    */
    /* because we want to use irregular (alpha images) for the sliders */
    return TRUE;
}

/**************************************************************************************************/
static IPTR windecor_draw_borderpropknob(Class *cl, Object *obj, struct wdpDrawBorderPropKnob *msg)
{
    struct windecor_data   *data = INST_DATA(cl, obj);
    struct Window          *window = msg->wdp_Window;
    struct RastPort        *winrp = msg->wdp_RPort;
    struct WindowData      *wd = (struct WindowData *) msg->wdp_UserBuffer;

    struct RastPort        *rp;
    struct Gadget          *gadget = msg->wdp_Gadget;
    struct Rectangle       *r;
    struct PropInfo        *pi = ((struct PropInfo *)gadget->SpecialInfo);
    struct DecorImage        *di = NULL;
    BOOL                    hit = (msg->wdp_Flags & WDF_DBPK_HIT) ? TRUE : FALSE;
    ULONG                   y, x, bx0, bx1, by0, by1;
    LONG                    size;
    ULONG                   s_col, e_col, arc;
    LONG                    pen = -1;
    struct BitMap          *cachedgadgetbitmap = NULL;
    ULONG                   changetype = CHANGE_NO_CHANGE;
    ULONG                   subimage = 0;

    if (wd->scaleData) data = &wd->scaleData->renderer;

    if (!(pi->Flags & PROPNEWLOOK) || (gadget->Activation && (GACT_RIGHTBORDER | GACT_BOTTOMBORDER) == 0))
    {
        return DoSuperMethodA(cl, obj, (Msg)msg);
    }

    /* Detect change in gadget dimensions (which needs to trigger redraw) */
    if ((pi->Flags & FREEVERT) != 0)
    {
        changetype = HasPropGadgetChanged(&wd->vert, msg);
        
        if (changetype == CHANGE_SIZE_CHANGE)
        {
            /* Free is there was size change */
            if (wd->vert.bm != NULL)
                FreeBitMap(wd->vert.bm);
            wd->vert.bm = NULL;
        }
        cachedgadgetbitmap = wd->vert.bm;
    }
    else if ((pi->Flags & FREEHORIZ) != 0)
    {
        changetype = HasPropGadgetChanged(&wd->horiz, msg);
        
        if (changetype == CHANGE_SIZE_CHANGE)
        {
            /* Free is there was size change */
            if (wd->horiz.bm != NULL)
                FreeBitMap(wd->horiz.bm);
            wd->horiz.bm = NULL;
        }
        cachedgadgetbitmap = wd->horiz.bm;

    }
    else
        return TRUE; /* Return TRUE - after all this is not an error */
    
    /* Reuse the bitmap if that is possible */
    if (changetype == CHANGE_NO_CHANGE)
    {
        /* Final blitting of gadget bitmap to window rast port */
        BltBitMapRastPort(cachedgadgetbitmap, 0, 0, winrp, msg->wdp_PropRect->MinX,
            msg->wdp_PropRect->MinY,
            msg->wdp_PropRect->MaxX - msg->wdp_PropRect->MinX + 1,
            msg->wdp_PropRect->MaxY - msg->wdp_PropRect->MinY + 1, 0xc0);
        return TRUE;
    }
    
    /* Regenerate the bitmap */
    r = msg->wdp_PropRect;

    bx0 = r->MinX;
    by0 = r->MinY;
    bx1 = r->MaxX;
    by1 = r->MaxY;

    rp = CreateRastPort();
    if (rp)
    {
        /* Reuse the bitmap if there was no size change (ie. only move of knob) */
        if (changetype == CHANGE_NO_SIZE_CHANGE)
            rp->BitMap = cachedgadgetbitmap;
        else
            rp->BitMap = AllocBitMap(bx1 - bx0 + 1, by1 - by0 + 1, 1, 0, window->WScreen->RastPort.BitMap);

        if (rp->BitMap == NULL)
        {
            FreeRastPort(rp);
            return FALSE;
        }
    }
    else
        return FALSE;

    if (wd->img_border_normal->ok) di = wd->img_border_normal;

    if (di == NULL) data->dc->UseGradients = TRUE;

    if (window->Flags & (WFLG_WINDOWACTIVE | WFLG_TOOLBOX))
    {
        s_col = data->dc->ActivatedGradientColor_s;
        e_col = data->dc->ActivatedGradientColor_e;
        arc = data->dc->ActivatedGradientColor_a;
        pen = wd->ActivePen;
    }
    else
    {
        s_col = data->dc->DeactivatedGradientColor_s;
        e_col = data->dc->DeactivatedGradientColor_e;
        arc = data->dc->DeactivatedGradientColor_a;
        if (!data->dc->UseGradients)
        {
            if (wd->img_border_deactivated->ok) di = wd->img_border_deactivated;
        }
        pen = wd->DeactivePen;
    }

    /* Drawing background - this solves WDM_DRAW_BORDERPROPBACK without need of
       reading from window when drawing container and knob */
    if (data->dc->UseGradients)
    {
        FillPixelArrayGradient(pen, wd->truecolor, rp, 0, 0, window->Width-1, window->Height-1,  0, 0, bx1 - bx0 + 1, by1 - by0 + 1, s_col, e_col, arc, bx0, by0);
    }
    else
    {
        if (di->ok)
        {
            ULONG   color = 0x00cccccc;
            DHorizVertRepeatDecorImage(di, color, bx0, by0, rp, 0, 0, bx1 - bx0 + 1, by1 - by0 + 1);
        }
    }

    /* Drawing knob container */
    r = msg->wdp_PropRect;

    bx0 = 0;
    by0 = 0;
    bx1 = r->MaxX - r->MinX;
    by1 = r->MaxY - r->MinY;

    if ((pi->Flags & FREEVERT) != 0)
    {
        struct DecoratorElement *welems = wd->dts->dts_Elements;

        if (window->Flags & (WFLG_WINDOWACTIVE | WFLG_TOOLBOX)) subimage = 0; else subimage = 1;
        y = by0;
        size = by1 - by0 - data->dc->ContainerTop_s - data->dc->ContainerBottom_s + 1;
        y = DRenderElement(&welems[DECOR_ELEM_VContainerTop], rp, subimage, bx0, y, 0, data->dc->ContainerTop_s, 0);
        if (size > 0) y = DRenderElement(&welems[DECOR_ELEM_VContainerTile], rp, subimage, bx0, y, 0, size, 0);

        y = DRenderElement(&welems[DECOR_ELEM_VContainerBottom], rp, subimage, bx0, y, 0, data->dc->ContainerBottom_s, 0);

    }
    else if ((pi->Flags & FREEHORIZ) != 0)
    {
        struct DecoratorElement *welems = wd->dts->dts_Elements;

        if (window->Flags & (WFLG_WINDOWACTIVE | WFLG_TOOLBOX)) subimage = 0; else subimage = 1;
        x = bx0;
        size = bx1 - bx0 - data->dc->ContainerLeft_s - data->dc->ContainerRight_s + 1;
        x = DRenderElement(&welems[DECOR_ELEM_HContainerLeft], rp, subimage, x, by0, data->dc->ContainerLeft_s, 0, 0);
        if (size > 0) x = DRenderElement(&welems[DECOR_ELEM_HContainerTile], rp, subimage, x, by0, size, 0, 0);
        x = DRenderElement(&welems[DECOR_ELEM_HContainerRight], rp, subimage, x, by0, data->dc->ContainerRight_s, 0, 0);
    }


    /* Drawing knob */
    bx0 = msg->wdp_PropRect->MinX;
    by0 = msg->wdp_PropRect->MinY;
    bx1 = msg->wdp_PropRect->MaxX;
    by1 = msg->wdp_PropRect->MaxY;

    r = msg->wdp_RenderRect;
    if ((pi->Flags & FREEVERT) != 0)
    {
        struct DecoratorElement *welems = wd->dts->dts_Elements;

        if (hit) subimage = 1; else if (window->Flags & (WFLG_WINDOWACTIVE | WFLG_TOOLBOX)) subimage = 0; else subimage = 2;
        y = r->MinY - by0;
        size = r->MaxY - r->MinY - data->dc->KnobTop_s - data->dc->KnobBottom_s + 1;

        y = DRenderElement(&welems[DECOR_ELEM_VKnobTop], rp, subimage, r->MinX - bx0, y, 0, data->dc->KnobTop_s, 0);
        if (size > 0)
        {
            if (size > data->dc->KnobVertGripper_s)
            {
                size = size - data->dc->KnobVertGripper_s;
                int size_bak = size;
                size = size / 2;
                if (size > 0) y = DRenderElement(&welems[DECOR_ELEM_VKnobTileTop], rp, subimage, r->MinX - bx0, y, 0, size, 0);
                y = DRenderElement(&welems[DECOR_ELEM_VKnobGripper], rp, subimage, r->MinX - bx0, y, 0, data->dc->KnobVertGripper_s, 0);
                size = size_bak - size;
                if (size > 0) y = DRenderElement(&welems[DECOR_ELEM_VKnobTileBottom], rp, subimage, r->MinX - bx0, y, 0, size, 0);
            }
            else
            {
                y = DRenderElement(&welems[DECOR_ELEM_VKnobTileTop], rp, subimage, r->MinX - bx0, y, 0, size, 0);
            }
        }
        y = DRenderElement(&welems[DECOR_ELEM_VKnobBottom], rp, subimage, r->MinX - bx0, y, 0, data->dc->KnobBottom_s, 0);
    }
    else if ((pi->Flags & FREEHORIZ) != 0)
    {
        struct DecoratorElement *welems = wd->dts->dts_Elements;

        if (hit) subimage = 1; else if (window->Flags & (WFLG_WINDOWACTIVE | WFLG_TOOLBOX)) subimage = 0; else subimage = 2;
        x = r->MinX - bx0;
        size = r->MaxX - r->MinX - data->dc->KnobLeft_s - data->dc->KnobRight_s + 1;
        x = DRenderElement(&welems[DECOR_ELEM_HKnobLeft], rp, subimage, x, r->MinY - by0, data->dc->KnobLeft_s, 0, 0);

        if (size > 0)
        {
            if (size > data->dc->KnobHorGripper_s)
            {
                size = size - data->dc->KnobHorGripper_s;
                int size_bak = size;
                size = size / 2;
                if (size > 0) x = DRenderElement(&welems[DECOR_ELEM_HKnobTileLeft], rp, subimage, x, r->MinY - by0, size, 0, 0);
                x = DRenderElement(&welems[DECOR_ELEM_HKnobGripper], rp, subimage, x, r->MinY - by0, data->dc->KnobHorGripper_s, 0, 0);
                size = size_bak - size;
                if (size > 0) x = DRenderElement(&welems[DECOR_ELEM_HKnobTileRight], rp, subimage, x, r->MinY - by0, size, 0, 0);
            }
            else
            {
                x = DRenderElement(&welems[DECOR_ELEM_HKnobTileRight], rp, subimage, x, r->MinY - by0, size, 0, 0);
            }
        }
        x = DRenderElement(&welems[DECOR_ELEM_HKnobRight], rp, subimage, x, r->MinY - by0, data->dc->KnobRight_s, 0, 0);
    }

    /* Final blitting of gadget bitmap to window rast port */
    BltBitMapRastPort(rp->BitMap, 0, 0, winrp, msg->wdp_PropRect->MinX,
        msg->wdp_PropRect->MinY,
        msg->wdp_PropRect->MaxX - msg->wdp_PropRect->MinX + 1,
        msg->wdp_PropRect->MaxY - msg->wdp_PropRect->MinY + 1, 0xc0);

    /* Cache the actual bitmap */
    if ((pi->Flags & FREEVERT) != 0)
        CachePropGadget(&wd->vert, msg, rp->BitMap);
    else if ((pi->Flags & FREEHORIZ) != 0)
        CachePropGadget(&wd->horiz, msg, rp->BitMap);

    FreeRastPort(rp);

    return TRUE;
}

/**************************************************************************************************/
static IPTR windecor_getdefsizes(Class *cl, Object *obj, struct wdpGetDefSizeSysImage *msg)
{
    struct windecor_data   *data = INST_DATA(cl, obj);
    struct DecorImage        *n = NULL;
    WORD                    w = 0, h = 0;
    BOOL                    isset = FALSE;
    BOOL                    titlegadget = FALSE;

    switch(msg->wdp_Which)
    {
        case SIZEIMAGE:
            n = NULL;
            w = data->dc->RightBorderGadgets;
            h = data->dc->BottomBorderGadgets;
            isset = TRUE;
            break;

        case CLOSEIMAGE:
            n = data->img_close;
            if(n) isset = TRUE;
            titlegadget = TRUE;
            break;

        case MUIIMAGE:
            n = data->img_mui;
            if(n) isset = TRUE;
            titlegadget = TRUE;
            break;

        case POPUPIMAGE:
            n = data->img_popup;
            if(n) isset = TRUE;
            titlegadget = TRUE;
            break;

        case SNAPSHOTIMAGE:
            n = data->img_snapshot;
            if(n) isset = TRUE;
            titlegadget = TRUE;
            break;

        case ICONIFYIMAGE:
            n = data->img_iconify;
            if(n) isset = TRUE;
            titlegadget = TRUE;
            break;

        case LOCKIMAGE:
            n = data->img_lock;
            if(n) isset = TRUE;
            titlegadget = TRUE;
            break;

        case UPIMAGE:
            n = NULL;
            w = data->dc->RightBorderGadgets;
            h = data->img_up->h + data->dc->UpDownAddY;
            isset = TRUE;
            break;

        case DOWNIMAGE:
            n = NULL;
            w = data->dc->RightBorderGadgets;
            h = data->img_down->h + data->dc->UpDownAddY;
            isset = TRUE;
            break;

        case LEFTIMAGE:
            n = NULL;
            if (data->dc->GadgetsThreeState) w = (data->img_left->w / 3); else w = (data->img_left->w >> 2);
            w += data->dc->LeftRightAddX;
            h = data->dc->BottomBorderGadgets;
            isset = TRUE;
            break;

        case RIGHTIMAGE:
            n = NULL;
            if (data->dc->GadgetsThreeState) w = (data->img_right->w / 3); else w = (data->img_right->w >> 2);
            w += data->dc->LeftRightAddX;
            h = data->dc->BottomBorderGadgets;
            isset = TRUE;
            break;

        case DEPTHIMAGE:
            n = data->img_depth;
            if(n) isset = TRUE;
            titlegadget = TRUE;
            break;

        case ZOOMIMAGE:
            n = data->img_zoom;
            if(n) isset = TRUE;
            titlegadget = TRUE;
            break;

        default:
            return FALSE;
    }

    if (!isset) return DoSuperMethodA(cl, obj, (Msg) msg);

    if (n == NULL)
    {
        *msg->wdp_Width = w;
        *msg->wdp_Height = h;
    }
    else
    {
        if (n->ok)
        {
            if (data->dc->GadgetsThreeState)
            {
                *msg->wdp_Width = (n->w / 3);
                *msg->wdp_Height = n->h;
            }
            else
            {
                *msg->wdp_Width = (n->w >> 2);
                *msg->wdp_Height = n->h;
            }

            if(titlegadget && (msg->wdp_ReferenceFont->tf_YSize + 2 > data->dc->BarHeight))
            {
                /* Scale height so that the gadget is not proportionally resized (so that width does not change) */
                *msg->wdp_Height *= msg->wdp_ReferenceFont->tf_YSize + 2;
                *msg->wdp_Height /= data->dc->BarHeight;
            }
        } else return DoSuperMethodA(cl, obj, (Msg) msg);
    }

    return TRUE;
}

/**************************************************************************************************/
/* Detached shape preparation. Ordinary WDM_WINDOWSHAPE keeps a title cache
 * and has ambiguous NULL failure semantics; never call it from a transaction. */
static BOOL prepare_shape_parts(const struct DecorConfig *dc, LONG width)
{
#define CHECK_SHAPE_PART(part) \
    if ((LONG)dc->part##_o < 0 || (LONG)dc->part##_s < 0 || \
        (LONG)dc->part##_o > width - (LONG)dc->part##_s) return FALSE
    CHECK_SHAPE_PART(BarPreGadget);
    CHECK_SHAPE_PART(BarLGadgetFill);
    CHECK_SHAPE_PART(BarPre);
    CHECK_SHAPE_PART(BarJoinGB);
    CHECK_SHAPE_PART(BarLFill);
    CHECK_SHAPE_PART(BarJoinBT);
    CHECK_SHAPE_PART(BarTitleFill);
    CHECK_SHAPE_PART(BarJoinTB);
    CHECK_SHAPE_PART(BarRFill);
    CHECK_SHAPE_PART(BarJoinBG);
    CHECK_SHAPE_PART(BarRGadgetFill);
    CHECK_SHAPE_PART(BarPostGadget);
    CHECK_SHAPE_PART(BarPost);
#undef CHECK_SHAPE_PART
    return TRUE;
}

/* Same vertical sample phase as decorator.library ScaleBuffer, with checked
 * allocation and no unnecessary horizontal line-pattern allocation. Width is
 * unchanged. The original ScaleBuffer does not check both of its allocations. */
static ULONG *prepare_shape_pixels(const struct DecorImage *image, LONG height)
{
    ULONG *pixels;
    LONG y, source_y = 0, accumulated = -(image->h >> 1), boundary = height;
    IPTR rowbytes = (IPTR)image->w * sizeof(ULONG);
    if (height <= 0 || (IPTR)height > (IPTR)-1 / rowbytes) return NULL;
    pixels = AllocVec(rowbytes * height, MEMF_ANY);
    if (!pixels) return NULL;
    for (y = 0; y < height; ++y)
    {
        accumulated += image->h;
        while (accumulated > boundary)
        {
            ++source_y;
            boundary += height;
        }
        if (source_y >= image->h) { FreeVec(pixels); return NULL; }
        CopyMem(image->data + (IPTR)source_y * image->w,
            pixels + (IPTR)y * image->w, rowbytes);
    }
    return pixels;
}

static struct Region *prepare_shape_region(const struct DecorImageLUT8 *mask,
    LONG width, LONG height)
{
    struct Region *region = NewRegion();
    LONG y;
    struct Rectangle rect = {0, mask->h, width - 1, height - 1};
    if (!region) return NULL;
    if (!OrRectRegion(region, &rect)) goto fail;
    for (y = 0; y < mask->h; ++y)
    {
        LONG x = 0;
        while (x < width)
        {
            LONG first;
            while (x < width && !mask->data[(IPTR)y * width + x]) ++x;
            first = x;
            while (x < width && mask->data[(IPTR)y * width + x]) ++x;
            if (first == x) continue;
            rect.MinX = first; rect.MaxX = x - 1;
            rect.MinY = rect.MaxY = y;
            if (!OrRectRegion(region, &rect)) goto fail;
        }
    }
    return region;
fail:
    DisposeRegion(region);
    return NULL;
}

static IPTR windecor_preparescaleshape(Class *cl, Object *obj,
    struct wdpPrepareScaleShape *msg)
{
    struct windecor_data *data = INST_DATA(cl, obj);
    struct Region *region;
    if (!msg->wdp_Result) return FALSE;
    *msg->wdp_Result = NULL;
    if (OCLASS(obj) != cl || !msg->wdp_Window || !msg->wdp_Font ||
        msg->wdp_Width <= 0 || msg->wdp_Width > 32767 ||
        msg->wdp_Height <= 0 || msg->wdp_Height > 32767 ||
        msg->wdp_BorderTop < 0 || msg->wdp_BorderTop > 127 ||
        msg->wdp_BorderTop > msg->wdp_Height) return FALSE;
    const struct WindowData *preparedWindow = (const struct WindowData *)msg->wdp_UserBuffer;
    if (preparedWindow && preparedWindow->scaleData)
        data = &preparedWindow->scaleData->renderer;
    if (data->dc->BarMasking)
    {
        const struct WindowData *live = (const struct WindowData *)msg->wdp_UserBuffer;
        struct Window window = *msg->wdp_Window;
        struct Screen screen;
        struct WindowData wd;
        struct windecor_data localdata = *data;
        struct DecorConfig config = *data->dc;
        struct DecorImage image;
        struct DecorImageLUT8 mask;
        if (!msg->wdp_BorderTop) return TRUE; /* full rectangular body */
        if (!live || !window.WScreen || !live->img_winbar_normal ||
            !live->img_winbar_normal->ok || !live->img_winbar_normal->data ||
            !live->img_winbar_normal->w || !live->img_winbar_normal->h ||
            !prepare_shape_parts(data->dc, live->img_winbar_normal->w))
            return FALSE;
        screen = *window.WScreen;
        wd = *live;
        image = *live->img_winbar_normal;
        image.data = prepare_shape_pixels(live->img_winbar_normal, msg->wdp_BorderTop * 2);
        if (!image.data) return FALSE;
        image.h = msg->wdp_BorderTop * 2;
        mask.w = msg->wdp_Width; mask.h = msg->wdp_BorderTop;
        mask.data = AllocVec((IPTR)mask.w * mask.h, MEMF_ANY | MEMF_CLEAR);
        if (!mask.data) { FreeVec(image.data); return FALSE; }
        /* Only snapshots are passed to the ordinary pixel-only title layout.
         * In particular it sees proposed Width and proposed selected font.
         * Matching BarHeight prevents its unchecked lazy DScaleDecorImage. */
        window.Width = msg->wdp_Width; window.Height = msg->wdp_Height;
        window.BorderTop = msg->wdp_BorderTop; window.WScreen = &screen;
        SetFont(&screen.RastPort, msg->wdp_Font);
        wd.img_winbar_normal = &image;
        config.BarHeight = msg->wdp_BorderTop; localdata.dc = &config;
        DrawShapePartialTitleBar(&wd, &mask, &localdata, &window);
        region = prepare_shape_region(&mask, msg->wdp_Width, msg->wdp_Height);
        FreeVec(mask.data); FreeVec(image.data);
        if (!region) return FALSE;
        *msg->wdp_Result = region;
        return TRUE;
    }
    if (!data->dc->BarRounded) return TRUE;
    region = NewRegion();
    if (!region) return FALSE;
    {
        static const WORD inset[] = {9, 6, 4, 3, 2, 1, 0};
        static const WORD row[] = {0, 1, 2, 3, 4, 6, 9};
        ULONG i;
        for (i = 0; i < sizeof(inset) / sizeof(inset[0]); ++i)
        {
            ULONG scale = preparedWindow && preparedWindow->scaleData ?
                preparedWindow->scaleData->scale : 100;
            LONG x = ((LONG)inset[i] * scale + 50) / 100;
            LONG y = ((LONG)row[i] * scale + 50) / 100;
            struct Rectangle rect = {x, y,
                msg->wdp_Width - 1 - x, msg->wdp_Height - 1};
            if (!OrRectRegion(region, &rect))
            {
                DisposeRegion(region);
                return FALSE;
            }
        }
    }
    *msg->wdp_Result = region;
    return TRUE;
}

static IPTR windecor_windowshape(Class *cl, Object *obj, struct wdpWindowShape *msg)
{
    struct windecor_data   *data = INST_DATA(cl, obj);
    struct WindowData      *wd = (struct WindowData *) msg->wdp_UserBuffer;
    struct Window          *window = msg->wdp_Window;

    if (wd->scaleData)
    {
        struct Region *preparedShape = NULL;
        struct wdpPrepareScaleShape prepared = {0};
        prepared.MethodID = WDM_PREPARESCALESHAPE;
        prepared.wdp_Window = window;
        prepared.wdp_Width = msg->wdp_Width;
        prepared.wdp_Height = msg->wdp_Height;
        prepared.wdp_BorderTop = window->BorderTop;
        prepared.wdp_Font = wd->scaleData->context->src_PhysicalDrawInfo->dri_Font;
        prepared.wdp_UserBuffer = msg->wdp_UserBuffer;
        prepared.wdp_Result = &preparedShape;
        if (!windecor_preparescaleshape(cl, obj, &prepared)) return 0;
        return (IPTR)preparedShape;
    }
    if (data->dc->BarMasking)
    {
        if (HasTitleBarShapeChanged(&wd->tbarshape, window) != CHANGE_NO_CHANGE)
        {
            struct  DNewLUT8ImageContainer *shape;
            struct Region * newreg = NULL;

            if (wd->tbarshape.shape != NULL)
            {
                DisposeRegion(wd->tbarshape.shape);
                wd->tbarshape.shape = NULL;
            }

            shape = (struct  DNewLUT8ImageContainer *)DNewLUT8ImageContainer(window->Width, window->BorderTop);
            if (shape)
            {
                if (window->BorderTop > 0) DrawShapePartialTitleBar(wd, (struct DecorImageLUT8 *)shape, data, window);

                newreg = DRegionFromLUT8Image(msg->wdp_Width, msg->wdp_Height, (struct DecorImageLUT8 *)shape);
                DDisposeLUT8ImageContainer((struct DecorImageLUT8 *)shape);

                CacheTitleBarShape(&wd->tbarshape, window, newreg);
            }
        }

        /* Make a copy of region and return it */
        return (IPTR)CopyRegion(wd->tbarshape.shape);

    }

    if (!data->dc->BarRounded) return (IPTR) NULL;

    struct  Region *newshape;

    int x2 = msg->wdp_Width-1;
    int y2 = msg->wdp_Height-1;
    
    if ((newshape = NewRegion()))
    {
        struct Rectangle rect;
        BOOL success = TRUE;

        rect.MinX = 9;
        rect.MinY = 0;
        rect.MaxX = x2 - 9;
        rect.MaxY = y2;
        success &= OrRectRegion(newshape, &rect);

        rect.MinX = 6;
        rect.MinY = 1;
        rect.MaxX = x2 - 6;
        rect.MaxY = y2;
        success &= OrRectRegion(newshape, &rect);

        rect.MinX = 4;
        rect.MinY = 2;
        rect.MaxX = x2 - 4;
        rect.MaxY = y2;
        success &= OrRectRegion(newshape, &rect);

        rect.MinX = 3;
        rect.MinY = 3;
        rect.MaxX = x2 - 3;
        rect.MaxY = y2;
        success &= OrRectRegion(newshape, &rect);

        rect.MinX = 2;
        rect.MinY = 4;
        rect.MaxX = x2 - 2;
        rect.MaxY = y2;
        success &= OrRectRegion(newshape, &rect);

        rect.MinX = 1;
        rect.MinY = 6;
        rect.MaxX = x2 - 1;
        rect.MaxY = y2;
        success &= OrRectRegion(newshape, &rect);

        rect.MinX = 0;
        rect.MinY = 9;
        rect.MaxX = x2;
        rect.MaxY = y2;
        success &= OrRectRegion(newshape, &rect);
    }
    return (IPTR) newshape;
}

/**************************************************************************************************/

struct WindowScaleSysImage
{
    struct DecoratorElement element;
    const struct ScreenRenderContext *context;
    BOOL logical;
    ULONG width, height;
};

static IPTR windecor_getscaledefsizes(Class *cl, Object *obj,
    struct wdpGetScaleDefSizeSysImage *msg)
{
    struct windecor_data *data = INST_DATA(cl, obj);
    const struct DecorImage *source = NULL;
    struct TextFont *font;
    struct WindowScaleSysImage *prepared;
    struct wdpScaleRefusal *refusal = msg->refusal;
    const char *id = NULL;
    ULONG width, height, scale;
    BOOL title = FALSE;
    if (msg->prepared) *msg->prepared = NULL;
    if (msg->base.wdp_Width && msg->base.wdp_Height)
        *msg->base.wdp_Width = *msg->base.wdp_Height = 0;
    if (OCLASS(obj) != cl)
    {
        window_scale_refuse(refusal, WDP_REFUSE_UNSUPPORTED, 0, 0, 0, NULL);
        return WDP_SCALE_FAILED;
    }
    if (!msg->base.wdp_Width || !msg->base.wdp_Height || !msg->context ||
        !msg->context->src_LogicalDrawInfo || !msg->context->src_PhysicalDrawInfo ||
        msg->context->src_Scale < 100 || msg->context->src_Scale > 400 || msg->context->src_Scale % 25)
    {
        window_scale_refuse(refusal, WDP_REFUSE_ARGUMENTS,
            msg->context ? msg->context->src_Scale : 0, 0, 0, NULL);
        return WDP_SCALE_FAILED;
    }
    switch (msg->base.wdp_Which)
    {
        case SIZEIMAGE: source = data->img_size; id = "size"; break;
        case CLOSEIMAGE: source = data->img_close; id = "close"; title = TRUE; break;
        case DEPTHIMAGE: source = data->img_depth; id = "depth"; title = TRUE; break;
        case ZOOMIMAGE: source = data->img_zoom; id = "zoom"; title = TRUE; break;
        case MUIIMAGE: source = data->img_mui; id = "mui"; title = TRUE; break;
        case POPUPIMAGE: source = data->img_popup; id = "popup"; title = TRUE; break;
        case SNAPSHOTIMAGE: source = data->img_snapshot; id = "snapshot"; title = TRUE; break;
        case ICONIFYIMAGE: source = data->img_iconify; id = "iconify"; title = TRUE; break;
        case LOCKIMAGE: source = data->img_lock; id = "lock"; title = TRUE; break;
        case UPIMAGE: source = data->img_up; id = "up"; break;
        case DOWNIMAGE: source = data->img_down; id = "down"; break;
        case LEFTIMAGE: source = data->img_left; id = "left"; break;
        case RIGHTIMAGE: source = data->img_right; id = "right"; break;
        case JUMPIMAGE: return WDP_SCALE_UNAVAILABLE; /* no shipped asset */
        default:
            window_scale_refuse(refusal, WDP_REFUSE_ARGUMENTS, msg->context->src_Scale, 0, 0, NULL);
            return WDP_SCALE_FAILED;
    }
    if (!source || !source->ok) return WDP_SCALE_UNAVAILABLE;
    if (!source->data || !source->w || !source->h || !source->subimagescols ||
        !source->subimagesrows || source->w % source->subimagescols ||
        source->h % source->subimagesrows)
    {
        window_scale_refuse(refusal, WDP_REFUSE_IMAGE_GRID, source->w, source->h,
            source->subimagescols * 100 + source->subimagesrows % 100, window_scale_image_name(id));
        return WDP_SCALE_FAILED;
    }
    font = msg->logical ? msg->base.wdp_ReferenceFont :
        msg->context->src_LogicalDrawInfo->dri_Font;
    if (!font)
    {
        window_scale_refuse(refusal, WDP_REFUSE_FONT, 0, data->dc->BarHeight, 0, NULL);
        return WDP_SCALE_FAILED;
    }
    width = source->w / source->subimagescols;
    height = source->h / source->subimagesrows;
    if (title && font->tf_YSize + 2 > data->dc->BarHeight)
    {
        if (data->dc->BarHeight <= 0)
        {
            window_scale_refuse(refusal, WDP_REFUSE_FONT, font->tf_YSize, data->dc->BarHeight, 0, NULL);
            return WDP_SCALE_FAILED;
        }
        height = (UQUAD)height * (font->tf_YSize + 2) / data->dc->BarHeight;
    }
    scale = msg->logical ? 100 : msg->context->src_Scale;
    switch (msg->base.wdp_Which)
    {
        case SIZEIMAGE:
            if (data->dc->RightBorderGadgets <= 0 || data->dc->BottomBorderGadgets <= 0)
            {
                BOOL right = data->dc->RightBorderGadgets <= 0;
                window_scale_refuse(refusal, WDP_REFUSE_CONFIG,
                    right ? data->dc->RightBorderGadgets : data->dc->BottomBorderGadgets, scale, 0,
                    right ? "RightBorderGadgets" : "BottomBorderGadgets");
                return WDP_SCALE_FAILED;
            }
            width = data->dc->RightBorderGadgets;
            height = data->dc->BottomBorderGadgets;
            break;
        case UPIMAGE: case DOWNIMAGE:
            if (data->dc->RightBorderGadgets <= 0 || data->dc->UpDownAddY < 0)
            {
                BOOL right = data->dc->RightBorderGadgets <= 0;
                window_scale_refuse(refusal, WDP_REFUSE_CONFIG,
                    right ? data->dc->RightBorderGadgets : data->dc->UpDownAddY, scale, 0,
                    right ? "RightBorderGadgets" : "UpDownAddY");
                return WDP_SCALE_FAILED;
            }
            width = data->dc->RightBorderGadgets;
            height += data->dc->UpDownAddY;
            break;
        case LEFTIMAGE: case RIGHTIMAGE:
            if (data->dc->BottomBorderGadgets <= 0 || data->dc->LeftRightAddX < 0)
            {
                BOOL bottom = data->dc->BottomBorderGadgets <= 0;
                window_scale_refuse(refusal, WDP_REFUSE_CONFIG,
                    bottom ? data->dc->BottomBorderGadgets : data->dc->LeftRightAddX, scale, 0,
                    bottom ? "BottomBorderGadgets" : "LeftRightAddX");
                return WDP_SCALE_FAILED;
            }
            width += data->dc->LeftRightAddX;
            height = data->dc->BottomBorderGadgets;
            break;
    }
    width = ((UQUAD)width * scale + 50) / 100;
    height = ((UQUAD)height * scale + 50) / 100;
    if (!width || !height || width > 32767 || height > 32767 ||
        msg->requestedWidth > 32767 || msg->requestedHeight > 32767)
    {
        window_scale_refuse(refusal, WDP_REFUSE_IMAGE_EXTENT,
            msg->requestedWidth > 32767 ? (LONG)msg->requestedWidth : (LONG)width,
            msg->requestedHeight > 32767 ? (LONG)msg->requestedHeight : (LONG)height, 0,
            window_scale_image_name(id));
        return WDP_SCALE_FAILED;
    }
    if (msg->requestedHeight && !msg->requestedWidth)
    {
        UQUAD aspect = ((UQUAD)width * msg->requestedHeight + height / 2) / height;
        if (!aspect || aspect > 32767)
        {
            window_scale_refuse(refusal, WDP_REFUSE_IMAGE_EXTENT, aspect > 32767 ? 32768 : (LONG)aspect,
                msg->requestedHeight, 0, window_scale_image_name(id));
            return WDP_SCALE_FAILED;
        }
        width = aspect;
    }
    if (msg->requestedWidth) width = msg->requestedWidth;
    if (msg->requestedHeight) height = msg->requestedHeight;
    if (!msg->prepared)
    {
        *msg->base.wdp_Width = width; *msg->base.wdp_Height = height;
        return WDP_SCALE_READY;
    }
    prepared = AllocVec(sizeof(*prepared), MEMF_ANY | MEMF_CLEAR);
    if (!prepared)
    {
        window_scale_refuse(refusal, WDP_REFUSE_NO_MEMORY, sizeof(*prepared), 0, 0, NULL);
        return WDP_SCALE_FAILED;
    }
    prepared->element.de_Image = checked_scale_image_cells(source, width, height);
    if (!prepared->element.de_Image)
    {
        FreeVec(prepared);
        window_scale_refuse(refusal, WDP_REFUSE_IMAGE_EXTENT, width, height, 0,
            window_scale_image_name(id));
        return WDP_SCALE_FAILED;
    }
    prepared->element.de_Type = DE_TYPE_STATEFUL_GADGET;
    prepared->element.de_SubImageCols = source->subimagescols;
    prepared->element.de_SubImageRows = source->subimagesrows;
    prepared->element.de_Flags = source->subimagescols == 3 ? DEF_THREE_STATE : 0;
    prepared->context = msg->context; prepared->logical = msg->logical;
    prepared->width = width; prepared->height = height;
    *msg->base.wdp_Width = width; *msg->base.wdp_Height = height;
    *msg->prepared = prepared;
    return WDP_SCALE_READY;
}

static IPTR windecor_drawscalesysimage(Class *cl, Object *obj,
    struct wdpDrawScaleSysImage *msg)
{
    struct WindowScaleSysImage *prepared = msg->prepared;
    struct wdpDrawSysImage *base = &msg->base;
    struct WindowData *wd = (struct WindowData *)base->wdp_UserBuffer;
    if (OCLASS(obj) != cl || !prepared || prepared->context != msg->context ||
        prepared->logical != msg->logical || !base->wdp_RPort ||
        base->wdp_Width != (LONG)prepared->width || base->wdp_Height != (LONG)prepared->height)
        return FALSE;
    if (titlebar_image(base->wdp_Which))
        reblit_titlebar(wd, base->wdp_RPort, base->wdp_X, base->wdp_Y,
            base->wdp_Width, base->wdp_Height);
    /* The owned sheet already has exactly the selected destination extent.
     * Natural-size rendering never enters unchecked lazy ScaleBuffer. */
    DRenderElement(&prepared->element, base->wdp_RPort, base->wdp_State,
        base->wdp_X, base->wdp_Y, -1, -1, 0);
    return TRUE;
}

static IPTR windecor_releasescalesysimage(Class *cl, Object *obj,
    struct wdpReleaseScaleSysImage *msg)
{
    struct WindowScaleSysImage *prepared = msg->prepared;
    if (OCLASS(obj) != cl) return FALSE;
    if (prepared)
    {
        DDisposeImageContainer(prepared->element.de_Image);
        FreeVec(prepared);
    }
    return TRUE;
}

/* An offset/size theme pair keeps its scaled end point; a refusal names it. */
static BOOL scale_config_pair(LONG *size, LONG sourceOffset, LONG sourceSize,
    LONG scaledOffset, ULONG scale, const char *name, struct wdpScaleRefusal *refusal)
{
    QUAD end = (QUAD)sourceOffset + sourceSize;
    if (sourceOffset < 0 || sourceSize < 0 || end > 65535)
    {
        window_scale_refuse(refusal, WDP_REFUSE_CONFIG, sourceOffset < 0 ? sourceOffset :
            sourceSize < 0 ? sourceSize : (LONG)end, scale, 0, name);
        return FALSE;
    }
    *size = (end * scale + 50) / 100 - scaledOffset;
    if (sourceSize && *size <= 0)
    {
        window_scale_refuse(refusal, WDP_REFUSE_CONFIG, sourceSize, scale, 0, name);
        return FALSE;
    }
    return TRUE;
}
static BOOL scale_window_config(struct DecorConfig *out,
    const struct DecorConfig *source, ULONG scale, struct wdpScaleRefusal *refusal)
{
    static const struct { ULONG offset; const char *name; } offsets[] = {
        {offsetof(struct DecorConfig, BarHeight), "BarHeight"},
        {offsetof(struct DecorConfig, BarFrame), "BarFrame"},
        {offsetof(struct DecorConfig, BarJoinTB_o), "BarJoinTB_o"},
        {offsetof(struct DecorConfig, BarJoinTB_s), "BarJoinTB_s"},
        {offsetof(struct DecorConfig, BarPreGadget_o), "BarPreGadget_o"},
        {offsetof(struct DecorConfig, BarPreGadget_s), "BarPreGadget_s"},
        {offsetof(struct DecorConfig, BarPre_o), "BarPre_o"},
        {offsetof(struct DecorConfig, BarPre_s), "BarPre_s"},
        {offsetof(struct DecorConfig, BarLGadgetFill_o), "BarLGadgetFill_o"},
        {offsetof(struct DecorConfig, BarLGadgetFill_s), "BarLGadgetFill_s"},
        {offsetof(struct DecorConfig, BarJoinGB_o), "BarJoinGB_o"},
        {offsetof(struct DecorConfig, BarJoinGB_s), "BarJoinGB_s"},
        {offsetof(struct DecorConfig, BarLFill_o), "BarLFill_o"},
        {offsetof(struct DecorConfig, BarLFill_s), "BarLFill_s"},
        {offsetof(struct DecorConfig, BarJoinBT_o), "BarJoinBT_o"},
        {offsetof(struct DecorConfig, BarJoinBT_s), "BarJoinBT_s"},
        {offsetof(struct DecorConfig, BarTitleFill_o), "BarTitleFill_o"},
        {offsetof(struct DecorConfig, BarTitleFill_s), "BarTitleFill_s"},
        {offsetof(struct DecorConfig, BarRFill_o), "BarRFill_o"},
        {offsetof(struct DecorConfig, BarRFill_s), "BarRFill_s"},
        {offsetof(struct DecorConfig, BarJoinBG_o), "BarJoinBG_o"},
        {offsetof(struct DecorConfig, BarJoinBG_s), "BarJoinBG_s"},
        {offsetof(struct DecorConfig, BarRGadgetFill_o), "BarRGadgetFill_o"},
        {offsetof(struct DecorConfig, BarRGadgetFill_s), "BarRGadgetFill_s"},
        {offsetof(struct DecorConfig, BarPostGadget_o), "BarPostGadget_o"},
        {offsetof(struct DecorConfig, BarPostGadget_s), "BarPostGadget_s"},
        {offsetof(struct DecorConfig, BarPost_o), "BarPost_o"},
        {offsetof(struct DecorConfig, BarPost_s), "BarPost_s"},
        {offsetof(struct DecorConfig, ContainerTop_o), "ContainerTop_o"},
        {offsetof(struct DecorConfig, ContainerTop_s), "ContainerTop_s"},
        {offsetof(struct DecorConfig, ContainerVertTile_o), "ContainerVertTile_o"},
        {offsetof(struct DecorConfig, ContainerVertTile_s), "ContainerVertTile_s"},
        {offsetof(struct DecorConfig, ContainerBottom_o), "ContainerBottom_o"},
        {offsetof(struct DecorConfig, ContainerBottom_s), "ContainerBottom_s"},
        {offsetof(struct DecorConfig, KnobTop_o), "KnobTop_o"},
        {offsetof(struct DecorConfig, KnobTop_s), "KnobTop_s"},
        {offsetof(struct DecorConfig, KnobTileTop_o), "KnobTileTop_o"},
        {offsetof(struct DecorConfig, KnobTileTop_s), "KnobTileTop_s"},
        {offsetof(struct DecorConfig, KnobVertGripper_o), "KnobVertGripper_o"},
        {offsetof(struct DecorConfig, KnobVertGripper_s), "KnobVertGripper_s"},
        {offsetof(struct DecorConfig, KnobTileBottom_o), "KnobTileBottom_o"},
        {offsetof(struct DecorConfig, KnobTileBottom_s), "KnobTileBottom_s"},
        {offsetof(struct DecorConfig, KnobBottom_o), "KnobBottom_o"},
        {offsetof(struct DecorConfig, KnobBottom_s), "KnobBottom_s"},
        {offsetof(struct DecorConfig, ContainerLeft_o), "ContainerLeft_o"},
        {offsetof(struct DecorConfig, ContainerLeft_s), "ContainerLeft_s"},
        {offsetof(struct DecorConfig, ContainerHorTile_o), "ContainerHorTile_o"},
        {offsetof(struct DecorConfig, ContainerHorTile_s), "ContainerHorTile_s"},
        {offsetof(struct DecorConfig, ContainerRight_o), "ContainerRight_o"},
        {offsetof(struct DecorConfig, ContainerRight_s), "ContainerRight_s"},
        {offsetof(struct DecorConfig, KnobLeft_o), "KnobLeft_o"},
        {offsetof(struct DecorConfig, KnobLeft_s), "KnobLeft_s"},
        {offsetof(struct DecorConfig, KnobTileLeft_o), "KnobTileLeft_o"},
        {offsetof(struct DecorConfig, KnobTileLeft_s), "KnobTileLeft_s"},
        {offsetof(struct DecorConfig, KnobHorGripper_o), "KnobHorGripper_o"},
        {offsetof(struct DecorConfig, KnobHorGripper_s), "KnobHorGripper_s"},
        {offsetof(struct DecorConfig, KnobTileRight_o), "KnobTileRight_o"},
        {offsetof(struct DecorConfig, KnobTileRight_s), "KnobTileRight_s"},
        {offsetof(struct DecorConfig, KnobRight_o), "KnobRight_o"},
        {offsetof(struct DecorConfig, KnobRight_s), "KnobRight_s"},
        {offsetof(struct DecorConfig, BottomBorderGadgets), "BottomBorderGadgets"},
        {offsetof(struct DecorConfig, RightBorderGadgets), "RightBorderGadgets"},
        {offsetof(struct DecorConfig, UpDownAddY), "UpDownAddY"},
        {offsetof(struct DecorConfig, LeftRightAddX), "LeftRightAddX"}};
    ULONG i;
    *out = *source;
    for (i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i)
    {
        LONG old = *(const LONG *)((const UBYTE *)source + offsets[i].offset);
        QUAD value = old < 0 ? -((-(QUAD)old * scale + 50) / 100) :
            ((QUAD)old * scale + 50) / 100;
        if (value < -32768 || value > 32767)
        {
            window_scale_refuse(refusal, WDP_REFUSE_CONFIG, old, scale, 0, offsets[i].name);
            return FALSE;
        }
        *(LONG *)((UBYTE *)out + offsets[i].offset) = value;
    }
#define SCALE_CONFIG_PAIR(part) \
    if (!scale_config_pair(&out->part##_s, source->part##_o, source->part##_s, \
        out->part##_o, scale, #part, refusal)) return FALSE;
    SCALE_CONFIG_PAIR(BarJoinTB)
    SCALE_CONFIG_PAIR(BarPreGadget)
    SCALE_CONFIG_PAIR(BarPre)
    SCALE_CONFIG_PAIR(BarLGadgetFill)
    SCALE_CONFIG_PAIR(BarJoinGB)
    SCALE_CONFIG_PAIR(BarLFill)
    SCALE_CONFIG_PAIR(BarJoinBT)
    SCALE_CONFIG_PAIR(BarTitleFill)
    SCALE_CONFIG_PAIR(BarRFill)
    SCALE_CONFIG_PAIR(BarJoinBG)
    SCALE_CONFIG_PAIR(BarRGadgetFill)
    SCALE_CONFIG_PAIR(BarPostGadget)
    SCALE_CONFIG_PAIR(BarPost)
    SCALE_CONFIG_PAIR(ContainerTop)
    SCALE_CONFIG_PAIR(ContainerVertTile)
    SCALE_CONFIG_PAIR(ContainerBottom)
    SCALE_CONFIG_PAIR(KnobTop)
    SCALE_CONFIG_PAIR(KnobTileTop)
    SCALE_CONFIG_PAIR(KnobVertGripper)
    SCALE_CONFIG_PAIR(KnobTileBottom)
    SCALE_CONFIG_PAIR(KnobBottom)
    SCALE_CONFIG_PAIR(ContainerLeft)
    SCALE_CONFIG_PAIR(ContainerHorTile)
    SCALE_CONFIG_PAIR(ContainerRight)
    SCALE_CONFIG_PAIR(KnobLeft)
    SCALE_CONFIG_PAIR(KnobTileLeft)
    SCALE_CONFIG_PAIR(KnobHorGripper)
    SCALE_CONFIG_PAIR(KnobTileRight)
    SCALE_CONFIG_PAIR(KnobRight)
#undef SCALE_CONFIG_PAIR
    return TRUE;
}

static void release_window_scale_data(struct WindowScaleData *scaled)
{
    if (!scaled) return;
#define RELEASE_SCALE_IMAGE(id) \
    if (scaled->images.img_##id && scaled->images.img_##id != &scaled->empty) \
        DDisposeImageContainer(scaled->images.img_##id);
    WINDOW_SCALE_IMAGE_LIST(RELEASE_SCALE_IMAGE)
#undef RELEASE_SCALE_IMAGE
    FreeVec(scaled);
}

static IPTR windecor_preparescalewindow(Class *cl, Object *obj,
    struct wdpPrepareScaleWindow *msg)
{
    struct windecor_data *data = INST_DATA(cl, obj);
    struct WindowData *wd = (struct WindowData *)msg->base.wdp_UserBuffer;
    const struct ScreenData *sd = (const struct ScreenData *)msg->base.wdp_ScreenUserBuffer;
    struct wdpScaleRefusal *refusal = msg->refusal;
    struct WindowScaleData *scaled;
    ULONG scale, i;
    if (OCLASS(obj) != cl)
    {
        window_scale_refuse(refusal, WDP_REFUSE_UNSUPPORTED, 0, 0, 0, NULL);
        return FALSE;
    }
    if (!wd || !sd || !sd->dts || !sd->di || wd->dts || wd->scaleData)
    {
        window_scale_refuse(refusal, WDP_REFUSE_STATE, 0, 0, 0, NULL);
        return FALSE;
    }
    if (!msg->context || !msg->context->src_LogicalDrawInfo ||
        msg->width < 1 || msg->width > 32767 || msg->height < 1 || msg->height > 32767 ||
        msg->borderTop < 0 || msg->borderTop > 127 ||
        msg->context->src_Scale < 100 || msg->context->src_Scale > 400 ||
        msg->context->src_Scale % 25)
    {
        window_scale_refuse(refusal, WDP_REFUSE_ARGUMENTS,
            msg->context ? msg->context->src_Scale : 0, msg->width, msg->height, NULL);
        return FALSE;
    }
    if (!msg->context->src_LogicalDrawInfo->dri_Font || !msg->font)
    {
        window_scale_refuse(refusal, WDP_REFUSE_FONT, 0, data->dc->BarHeight, 0, NULL);
        return FALSE;
    }
    scale = msg->logical ? 100 : msg->context->src_Scale;
    scaled = AllocVec(sizeof(*scaled), MEMF_ANY | MEMF_CLEAR);
    if (!scaled)
    {
        window_scale_refuse(refusal, WDP_REFUSE_NO_MEMORY, sizeof(*scaled), 0, 0, NULL);
        return FALSE;
    }
    scaled->context = msg->context; scaled->scale = scale;
    scaled->renderer = *data;
    if (!scale_window_config(&scaled->config, data->dc, scale, refusal)) goto fail;
    scaled->config.BarHeight = msg->borderTop;
    scaled->renderer.dc = &scaled->config;
    scaled->theme = *sd->dts;
    scaled->theme.dts_Images = &scaled->images;
#define PREPARE_SCALE_IMAGE(id) \
    do { \
        const struct DecorImage *source = sd->di->img_##id; \
        if (!source || !source->ok) scaled->images.img_##id = &scaled->empty; \
        else { \
            ULONG cols = source->subimagescols, rows = source->subimagesrows; \
            ULONG width, height; \
            if (!cols || !rows || !source->data || source->w % cols || source->h % rows) { \
                window_scale_refuse(refusal, WDP_REFUSE_IMAGE_GRID, source->w, source->h, \
                    cols * 100 + rows % 100, window_scale_image_name(#id)); \
                goto fail; \
            } \
            width = ((ULONG)(source->w / cols) * scale + 50) / 100; \
            height = ((ULONG)(source->h / rows) * scale + 50) / 100; \
            if (msg->borderTop && (source == sd->di->img_close || source == sd->di->img_depth || \
                source == sd->di->img_zoom || source == sd->di->img_mui || source == sd->di->img_popup || \
                source == sd->di->img_snapshot || source == sd->di->img_iconify || source == sd->di->img_lock)) { \
                ULONG baselineHeight = source->h / rows; \
                ULONG canonicalHeight = ((ULONG)msg->borderTop * 100 + scale / 2) / scale; \
                struct TextFont *basefont = msg->context->src_LogicalDrawInfo->dri_Font; \
                if (!basefont || data->dc->BarHeight <= 0) { \
                    window_scale_refuse(refusal, WDP_REFUSE_FONT, basefont ? basefont->tf_YSize : 0, \
                        data->dc->BarHeight, 0, NULL); \
                    goto fail; \
                } \
                if (basefont->tf_YSize + 2 > data->dc->BarHeight) \
                    baselineHeight = (UQUAD)baselineHeight * (basefont->tf_YSize + 2) / data->dc->BarHeight; \
                if (!baselineHeight) { \
                    window_scale_refuse(refusal, WDP_REFUSE_IMAGE_EXTENT, source->w / cols, 0, 0, \
                        window_scale_image_name(#id)); \
                    goto fail; \
                } \
                if (!canonicalHeight) canonicalHeight = 1; \
                ULONG canonicalWidth = ((UQUAD)(source->w / cols) * canonicalHeight + baselineHeight / 2) / baselineHeight; \
                width = ((UQUAD)canonicalWidth * scale + 50) / 100; \
                height = msg->borderTop; \
            } \
            if (source == sd->di->img_winbar_normal) { \
                if (rows != 2) { \
                    window_scale_refuse(refusal, WDP_REFUSE_IMAGE_GRID, source->w, source->h, \
                        cols * 100 + rows % 100, window_scale_image_name(#id)); \
                    goto fail; \
                } \
                height = msg->borderTop ? msg->borderTop : 1; \
            } \
            scaled->images.img_##id = checked_scale_image_cells(source, width, height); \
            if (!scaled->images.img_##id) { \
                window_scale_refuse(refusal, WDP_REFUSE_IMAGE_EXTENT, width, height, 0, \
                    window_scale_image_name(#id)); \
                goto fail; \
            } \
        } \
    } while (0);
    WINDOW_SCALE_IMAGE_LIST(PREPARE_SCALE_IMAGE)
#undef PREPARE_SCALE_IMAGE
#define BIND_SCALE_RENDER_IMAGE(id) scaled->renderer.img_##id = scaled->images.img_##id;
    BIND_SCALE_RENDER_IMAGE(size)
    BIND_SCALE_RENDER_IMAGE(close)
    BIND_SCALE_RENDER_IMAGE(depth)
    BIND_SCALE_RENDER_IMAGE(zoom)
    BIND_SCALE_RENDER_IMAGE(up)
    BIND_SCALE_RENDER_IMAGE(down)
    BIND_SCALE_RENDER_IMAGE(left)
    BIND_SCALE_RENDER_IMAGE(right)
    BIND_SCALE_RENDER_IMAGE(mui)
    BIND_SCALE_RENDER_IMAGE(popup)
    BIND_SCALE_RENDER_IMAGE(snapshot)
    BIND_SCALE_RENDER_IMAGE(iconify)
    BIND_SCALE_RENDER_IMAGE(lock)
#undef BIND_SCALE_RENDER_IMAGE
    for (i = 0; i < DECOR_NUM_ELEMENTS; ++i)
    {
        struct DecoratorElement *element = &scaled->theme.dts_Elements[i];
        const struct DecorImage *original = element->de_Image;
        const char *imageName = NULL;
        element->de_Image = NULL;
#define REMAP_SCALE_ELEMENT(id) \
        if (original && original == sd->di->img_##id) \
        { element->de_Image = scaled->images.img_##id; imageName = window_scale_image_name(#id); }
        WINDOW_SCALE_IMAGE_LIST(REMAP_SCALE_ELEMENT)
#undef REMAP_SCALE_ELEMENT
        if (element->de_Image && element->de_Image->ok)
        {
            QUAD end = (QUAD)element->de_SrcOffset + element->de_SrcSize;
            LONG offset, size;
            if (element->de_SrcOffset < 0 || element->de_SrcSize < 0 || end > 65535)
            {
                window_scale_refuse(refusal, WDP_REFUSE_ELEMENT, element->de_SrcOffset,
                    element->de_SrcSize, 65535, imageName);
                goto fail;
            }
            offset = ((QUAD)element->de_SrcOffset * scale + 50) / 100;
            size = (end * scale + 50) / 100 - offset;
            element->de_SrcOffset = offset; element->de_SrcSize = size;
            element->de_SrcHeight = ((QUAD)element->de_SrcHeight * scale + 50) / 100;
            QUAD padX = element->de_PadX, padY = element->de_PadY;
            padX = padX < 0 ? -((-padX * scale + 50) / 100) : (padX * scale + 50) / 100;
            padY = padY < 0 ? -((-padY * scale + 50) / 100) : (padY * scale + 50) / 100;
            if (padX < -32768 || padX > 32767 || padY < -32768 || padY > 32767)
            {
                window_scale_refuse(refusal, WDP_REFUSE_ELEMENT, padX < -32768 || padX > 32767 ?
                    (LONG)padX : (LONG)padY, 0, 32767, imageName);
                goto fail;
            }
            element->de_PadX = padX; element->de_PadY = padY;
            if (size > 0)
            {
                ULONG extent = element->de_Type == DE_TYPE_TILED_VERTICAL ?
                    element->de_Image->h / element->de_Image->subimagesrows :
                    element->de_Image->w / element->de_Image->subimagescols;
                if (offset < 0 || (ULONG)offset > extent || (ULONG)size > extent - offset)
                {
                    window_scale_refuse(refusal, WDP_REFUSE_ELEMENT, offset, size, extent, imageName);
                    goto fail;
                }
            }
        }
    }
    wd->scaleData = scaled; wd->dts = &scaled->theme;
    wd->truecolor = msg->base.wdp_TrueColor;
    wd->ActivePen = sd->ActivePen; wd->DeactivePen = sd->DeactivePen;
#define BIND_SCALE_WINDOW_IMAGE(id) wd->img_##id = scaled->images.img_##id;
    WINDOW_SCALE_IMAGE_LIST(BIND_SCALE_WINDOW_IMAGE)
#undef BIND_SCALE_WINDOW_IMAGE
    return TRUE;
fail:
    release_window_scale_data(scaled);
    return FALSE;
}

static IPTR windecor_layoutscalebordergadgets(Class *cl, Object *obj,
    struct wdpLayoutScaleBorderGadgets *msg)
{
    struct WindowData *wd = (struct WindowData *)msg->base.wdp_UserBuffer;
    if (OCLASS(obj) != cl)
    {
        window_scale_refuse(msg->refusal, WDP_REFUSE_UNSUPPORTED, 0, 0, 0, NULL);
        return FALSE;
    }
    if (!msg->context || !wd || !wd->scaleData || wd->scaleData->context != msg->context)
    {
        window_scale_refuse(msg->refusal, WDP_REFUSE_STATE, 0, 0, 0, NULL);
        return FALSE;
    }
    if (!msg->base.wdp_Window || !msg->base.wdp_Gadgets)
    {
        window_scale_refuse(msg->refusal, WDP_REFUSE_ARGUMENTS, wd->scaleData->scale, 0, 0, NULL);
        return FALSE;
    }
    return windecor_layout_bordergadgets(cl, obj, &msg->base, msg->refusal);
}

static IPTR windecor_initwindow(Class *cl, Object *obj, struct wdpInitWindow *msg)
{
    struct WindowData *wd = (struct WindowData *)msg->wdp_UserBuffer;
    struct ScreenData *sd = (struct ScreenData *)msg->wdp_ScreenUserBuffer;

    wd->truecolor = msg->wdp_TrueColor;

    wd->dts = sd->dts;

    wd->ActivePen = sd->ActivePen;
    wd->DeactivePen = sd->DeactivePen;

    wd->vert.bm = NULL;
    wd->horiz.bm = NULL;

    SETIMAGE_WIN(size);
    SETIMAGE_WIN(close);
    SETIMAGE_WIN(depth);
    SETIMAGE_WIN(zoom);
    SETIMAGE_WIN(up);
    SETIMAGE_WIN(down);
    SETIMAGE_WIN(left);
    SETIMAGE_WIN(right);
    SETIMAGE_WIN(mui);
    SETIMAGE_WIN(popup);
    SETIMAGE_WIN(snapshot);
    SETIMAGE_WIN(iconify);
    SETIMAGE_WIN(lock);
    SETIMAGE_WIN(winbar_normal);
    SETIMAGE_WIN(border_normal);
    SETIMAGE_WIN(border_deactivated);
    SETIMAGE_WIN(verticalcontainer);
    SETIMAGE_WIN(verticalknob);
    SETIMAGE_WIN(horizontalcontainer);
    SETIMAGE_WIN(horizontalknob);

    return TRUE;
}

/**************************************************************************************************/
static IPTR windecor_exitwindow(Class *cl, Object *obj, struct wdpExitWindow *msg)
{
    struct WindowData *wd = (struct WindowData *) msg->wdp_UserBuffer;

    if (wd->vert.bm) FreeBitMap(wd->vert.bm);
    if (wd->horiz.bm) FreeBitMap(wd->horiz.bm);
    if (wd->tbar.bm) FreeBitMap(wd->tbar.bm);
    if (wd->tbarshape.shape) DisposeRegion(wd->tbarshape.shape);
    if (wd->tbar.title) FreeVec(wd->tbar.title);
    release_window_scale_data(wd->scaleData);
    wd->scaleData = NULL;

    return TRUE;
}

/**************************************************************************************************/
static IPTR windecor_dispatcher(struct IClass *cl, Object *obj, Msg msg)
{
    IPTR retval;

    switch(msg->MethodID)
    {
        case OM_NEW:
            retval = windecor_new(cl, obj, (struct opSet *)msg);
            break;

        case OM_DISPOSE:
            retval = windecor_dispose(cl, obj, (struct opSet *)msg);
            break;

        case OM_GET:
            retval = windecor_get(cl, obj, (struct opGet *)msg);
            break;

        case WDM_DRAW_SYSIMAGE:
            retval = windecor_draw_sysimage(cl, obj, (struct wdpDrawSysImage *)msg);
            break;

        case WDM_DRAW_WINBORDER:
            retval = windecor_draw_winborder(cl, obj, (struct wdpDrawWinBorder *)msg);
            break;

        case WDM_LAYOUT_BORDERGADGETS:
            retval = windecor_layout_bordergadgets(cl, obj, (struct wdpLayoutBorderGadgets *)msg, NULL);
            break;

        case WDM_DRAW_BORDERPROPBACK:
            retval = windecor_draw_borderpropback(cl, obj, (struct wdpDrawBorderPropBack *)msg);
            break;

        case WDM_DRAW_BORDERPROPKNOB:
            retval = windecor_draw_borderpropknob(cl, obj, (struct wdpDrawBorderPropKnob *)msg);
            break;

        case WDM_GETDEFSIZE_SYSIMAGE:
            retval = windecor_getdefsizes(cl, obj, (struct wdpGetDefSizeSysImage *) msg);
            break;

        case WDM_GETSCALEDEFSIZE_SYSIMAGE:
            retval = windecor_getscaledefsizes(cl, obj, (struct wdpGetScaleDefSizeSysImage *)msg);
            break;
        case WDM_DRAWSCALE_SYSIMAGE:
            retval = windecor_drawscalesysimage(cl, obj, (struct wdpDrawScaleSysImage *)msg);
            break;
        case WDM_RELEASESCALE_SYSIMAGE:
            retval = windecor_releasescalesysimage(cl, obj, (struct wdpReleaseScaleSysImage *)msg);
            break;

        case WDM_LAYOUTSCALEBORDERGADGETS:
            retval = windecor_layoutscalebordergadgets(cl, obj, (struct wdpLayoutScaleBorderGadgets *)msg);
            break;

        case WDM_PREPARESCALEWINDOW:
            retval = windecor_preparescalewindow(cl, obj, (struct wdpPrepareScaleWindow *)msg);
            break;

        case WDM_PREPARESCALESHAPE:
            retval = windecor_preparescaleshape(cl, obj, (struct wdpPrepareScaleShape *)msg);
            break;

        case WDM_WINDOWSHAPE:
            retval = windecor_windowshape(cl, obj, (struct wdpWindowShape *) msg);
            break;

        case WDM_INITWINDOW:
            retval = windecor_initwindow(cl, obj, (struct wdpInitWindow *) msg);
            break;

        case WDM_EXITWINDOW:
            retval = windecor_exitwindow(cl, obj, (struct wdpExitWindow *) msg);
            break;

        default:
            retval = DoSuperMethodA(cl, obj, msg);
            break;
    }

    return retval;
}

struct IClass * MakeWindowDecorClass()
{
    struct IClass * cl = MakeClass(NULL, WINDECORCLASS, NULL, sizeof(struct windecor_data), 0);
    if (cl)
    {
        cl->cl_Dispatcher.h_Entry    = HookEntry;
        cl->cl_Dispatcher.h_SubEntry = (HOOKFUNC)windecor_dispatcher;
    }
    
    return cl;
}
