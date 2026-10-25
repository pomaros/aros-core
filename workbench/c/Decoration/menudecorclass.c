/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Copyright (C)  2011-2026, The AROS Development Team.
*/

#define DEBUG 0
#include <aros/debug.h>

#include <clib/alib_protos.h>

#include <intuition/intuition.h>
#include <intuition/extensions.h>
#include <intuition/imageclass.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/exec.h>

#include <libraries/decorator.h>
#include <proto/decorator.h>

#include "menudecorclass.h"
#include "screendecorclass.h"

#define SETIMAGE_MEN(id) md->img_##id=sd->di->img_##id

struct menudecor_data
{
    struct DecorConfig * dc;

    /* Pointers to images used for sys images */
    struct DecorImage *img_amigakey;
    struct DecorImage *img_menucheck;
    struct DecorImage *img_submenu;
};

static IPTR menudecor_getmenuspaces(Class *, Object *, struct mdpGetMenuSpaces *);
/* All allocations happen while the image/popup is still detached. The
 * nearest-sample bitmap fallback is exact at integer scales; no filtered or
 * vector fidelity is claimed for theme bitmap assets at fractional scales. */
static struct DecorImage *menu_checked_image(const struct DecorImage *source,
    ULONG width, ULONG height)
{
    struct DecorImage *image;
    ULONG x, y;
    if (!width || !height || width > 32767 || height > 32767 ||
        (UQUAD)width * height > 0x7fffffffUL / sizeof(ULONG) ||
        (source && (!source->ok || !source->data || !source->w || !source->h)))
        return NULL;
    image = DNewDecorImageContainer(width, height);
    if (!image) return NULL;
    if (source)
        for (y = 0; y < height; y++)
            for (x = 0; x < width; x++)
                image->data[y * width + x] = source->data[
                    ((UQUAD)y * source->h / height) * source->w +
                    (UQUAD)x * source->w / width];
    image->ok = TRUE;
    return image;
}
static BOOL menu_typed_context(Class *cl, Object *obj,
    const struct ScreenRenderContext *context)
{
    return OCLASS(obj) == cl && context && context->src_Scale >= 100 &&
        context->src_Scale <= 400 && !(context->src_Scale % 25);
}
static ULONG menu_typed_metric(ULONG point, ULONG scale)
{
    return ((UQUAD)point * scale + 50) / 100;
}
static IPTR menudecor_scale_spaces(Class *cl, Object *obj,
    struct mdpGetScaleSpaces *msg)
{
    LONG *fields[] = { &msg->base.mdp_InnerLeft, &msg->base.mdp_InnerTop,
        &msg->base.mdp_InnerRight, &msg->base.mdp_InnerBottom,
        &msg->base.mdp_ItemInnerLeft, &msg->base.mdp_ItemInnerTop,
        &msg->base.mdp_ItemInnerRight, &msg->base.mdp_ItemInnerBottom,
        &msg->base.mdp_MinWidth, &msg->base.mdp_MinHeight };
    ULONG i, scale;
    if (!menu_typed_context(cl, obj, msg->context)) return FALSE;
    menudecor_getmenuspaces(cl, obj, &msg->base);
    scale = msg->logical ? 100 : msg->context->src_Scale;
    for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++)
    {
        if (*fields[i] < 0 || *fields[i] > 32767 / 4) return FALSE;
        *fields[i] = menu_typed_metric(*fields[i], scale);
    }
    return TRUE;
}
static IPTR menudecor_prepare_image(Class *cl, Object *obj,
    struct mdpGetScaleDefSizeSysImage *msg)
{
    struct menudecor_data *data = INST_DATA(cl, obj);
    struct DecorImage *source, *image;
    ULONG width, height, scale;
    if (!msg->prepared || !msg->base.mdp_Width || !msg->base.mdp_Height ||
        !menu_typed_context(cl, obj, msg->context)) return FALSE;
    *msg->prepared = NULL;
    *msg->base.mdp_Width = *msg->base.mdp_Height = 0;
    switch (msg->base.mdp_Which)
    {
        case AMIGAKEY: source = data->img_amigakey; break;
        case MENUCHECK: source = data->img_menucheck; break;
        case SUBMENUIMAGE: source = data->img_submenu; break;
        default: return FALSE;
    }
    if (!source) return MDP_SCALE_UNAVAILABLE;
    if (!source->w || !source->h || !source->ok || !source->data ||
        source->subimagescols != 1 || source->subimagesrows != 1) return FALSE;
    scale = msg->logical ? 100 : msg->context->src_Scale;
    width = menu_typed_metric(source->w, scale);
    height = menu_typed_metric(source->h, scale);
    if (!width || !height) return FALSE;
    if (msg->requestedHeight > 32767 || msg->requestedWidth > 32767) return FALSE;
    if (msg->requestedHeight && !msg->requestedWidth)
    {
        UQUAD aspect = ((UQUAD)width * msg->requestedHeight + height / 2) / height;
        if (!aspect || aspect > 32767) return FALSE;
        width = aspect;
    }
    if (msg->requestedWidth) width = msg->requestedWidth;
    if (msg->requestedHeight) height = msg->requestedHeight;
    image = menu_checked_image(source, width, height);
    if (!image) return FALSE;
    *msg->base.mdp_Width = width;
    *msg->base.mdp_Height = height;
    *msg->prepared = image;
    return TRUE;
}
static IPTR menudecor_draw_prepared(Class *cl, Object *obj,
    struct mdpDrawScaleSysImage *msg)
{
    struct DecorImage *image = msg->prepared;
    if (!menu_typed_context(cl, obj, msg->context) || !msg->base.mdp_RPort || !image || !image->ok ||
        image->w != msg->base.mdp_Width || image->h != msg->base.mdp_Height)
        return FALSE;
    /* Unlike DPutImageToRP, this helper does not consume the retained image. */
    DDrawPartImageToRP(msg->base.mdp_RPort, image, msg->base.mdp_X,
        msg->base.mdp_Y, 0, 0, image->w, image->h);
    return TRUE;
}
static IPTR menudecor_release_prepared(Class *cl, Object *obj,
    struct mdpReleaseScaleSysImage *msg)
{
    if (OCLASS(obj) != cl) return FALSE;
    if (msg->prepared) DDisposeImageContainer(msg->prepared);
    return TRUE;
}

static IPTR menudecor_getdefsizes(Class *cl, Object *obj, struct mdpGetDefSizeSysImage *msg)
{
    struct menudecor_data *data = INST_DATA(cl, obj);

    struct DecorImage *n = NULL;
    BOOL  isset = FALSE;

    switch(msg->mdp_Which)
    {
        case AMIGAKEY:
            n = data->img_amigakey;
            if(n) isset = TRUE;
            break;

        case MENUCHECK:
            n = data->img_menucheck;
            if(n) isset = TRUE;
            break;

        case SUBMENUIMAGE:
            n = data->img_submenu;
            if(n) isset = TRUE;
            break;

        default:
            return FALSE;
    }

    if (!isset) return DoSuperMethodA(cl, obj, (Msg) msg);

    *msg->mdp_Width = n->w;
    *msg->mdp_Height = n->h;
    return TRUE;
}

static IPTR menudecor_getmenuspaces(Class *cl, Object *obj, struct mdpGetMenuSpaces *msg)
{
    struct menudecor_data *data = INST_DATA(cl, obj);

    msg->mdp_InnerLeft =  data->dc->MenuInnerLeft;
    msg->mdp_InnerTop =  data->dc->MenuInnerTop;
    msg->mdp_InnerRight =  data->dc->MenuInnerRight;
    msg->mdp_InnerBottom =  data->dc->MenuInnerBottom;
    msg->mdp_ItemInnerLeft = 1;
    msg->mdp_ItemInnerTop = 2;
    msg->mdp_ItemInnerRight = 2;
    msg->mdp_ItemInnerBottom = 1;
    if ((data->dc->MenuTileLeft + data->dc->MenuTileRight) > (data->dc->MenuInnerLeft + data->dc->MenuInnerRight))
        msg->mdp_MinWidth = data->dc->MenuTileLeft + data->dc->MenuTileRight;
    else
        msg->mdp_MinWidth = data->dc->MenuInnerLeft + data->dc->MenuInnerRight;

    if ((data->dc->MenuTileTop + data->dc->MenuTileBottom) > (data->dc->MenuInnerTop + data->dc->MenuInnerBottom))
        msg->mdp_MinHeight = data->dc->MenuTileTop + data->dc->MenuTileBottom;
    else
        msg->mdp_MinHeight = data->dc->MenuInnerTop + data->dc->MenuInnerBottom;

    return TRUE;
}

static IPTR menudecor_draw_sysimage(Class *cl, Object *obj, struct mdpDrawSysImage *msg)
{
    struct ScreenData      *md = (struct ScreenData *) msg->mdp_UserBuffer;
    struct RastPort        *rp = msg->mdp_RPort;
    struct DecoratorElement *elem = NULL;
    LONG                    left = msg->mdp_X;
    LONG                    top = msg->mdp_Y;
    WORD                    addx = 0;
    WORD                    addy = 0;
    ULONG                   elemid;

    switch(msg->mdp_Which)
    {
        case AMIGAKEY:
            elemid = DECOR_ELEM_MenuAmigaKey;
            break;

        case MENUCHECK:
            elemid = DECOR_ELEM_MenuCheck;
            break;

        case SUBMENUIMAGE:
            elemid = DECOR_ELEM_MenuSubMenu;
            break;

        default:
            return DoSuperMethodA(cl, obj, (Msg)msg);
    }

    if (md && md->dts)
        elem = &md->dts->dts_Elements[elemid];

    if (!elem || !elem->de_Image || !elem->de_Image->ok)
        return DoSuperMethodA(cl, obj, (Msg)msg);

    DRenderElement(elem, rp, IDS_NORMAL, left + addx, top + addy, -1, -1, 0);

    return TRUE;
}

/**************************************************************************************************/

/* The popup keeps both normal and highlighted pixels. No image allocation or
 * image scaling is needed during visible item highlight/unhighlight. */
static ULONG menu_sample_axis(ULONG at, ULONG extent, ULONG source,
    ULONG leading, ULONG trailing)
{
    if (at < leading) return at;
    if (at >= extent - trailing) return source - (extent - at);
    return leading + (at - leading) % (source - leading - trailing);
}
static ULONG menu_blur_channel(const struct DecorImage *source, LONG x, LONG y,
    ULONG channel, LONG radius)
{
    static const signed char taps[][3] = {
        {0,0,1},{0,-2,1},{-1,-1,1},{0,-1,2},{1,-1,1},
        {-2,0,1},{-1,0,1},{1,0,1},{2,0,1},
        {-1,1,1},{0,1,1},{1,1,1},{0,2,1}};
    ULONG i, sum = 0;
    for (i = 0; i < sizeof(taps) / sizeof(taps[0]); i++)
    {
        LONG sx = x + taps[i][0] * radius, sy = y + taps[i][1] * radius;
        if (sx < 0) sx = 0;
        if (sy < 0) sy = 0;
        if (sx >= source->w) sx = source->w - 1;
        if (sy >= source->h) sy = source->h - 1;
        sum += ((source->data[sy * source->w + sx] >> channel) & 255) * taps[i][2];
    }
    return sum / 14;
}
static IPTR menudecor_init_scale(Class *cl, Object *obj, struct mdpInitScaleMenu *msg)
{
    struct menudecor_data *data = INST_DATA(cl, obj);
    struct mdpInitMenu *m = &msg->base;
    struct MenuData *md = (struct MenuData *)m->mdp_UserBuffer;
    struct ScreenData *sd = (struct ScreenData *)m->mdp_ScreenUserBuffer;
    struct DecorImage *captured;
    ULONG scale, factor, tw, th, left = 0, right = 0, top = 0, bottom = 0;
    ULONG x, y, c, radius, tilew, tileh;
    if (!menu_typed_context(cl, obj, msg->context) || !md || !sd || !sd->di ||
        !m->mdp_RPort || !m->mdp_Screen || m->mdp_Screen->Width <= 0 || m->mdp_Screen->Height <= 0 ||
        m->mdp_Width == 0 || m->mdp_Height <= 0 ||
        m->mdp_Width > 32767 || m->mdp_Height > 32767) return FALSE;
    md->context = msg->context;
    md->logical = msg->logical;
    scale = msg->logical ? 100 : msg->context->src_Scale;
    factor = msg->logical ? msg->context->src_LegacyScaleFactor : 1;
    if (factor < 1 || factor > 4 ||
        ((UQUAD)m->mdp_Left + m->mdp_Width) * factor > (ULONG)m->mdp_Screen->Width ||
        ((UQUAD)m->mdp_Top + m->mdp_Height) * factor > (ULONG)m->mdp_Screen->Height)
        return FALSE;
    captured = DGetImageFromRP(m->mdp_RPort, m->mdp_Left * factor,
        m->mdp_Top * factor, m->mdp_Width * factor, m->mdp_Height * factor);
    if (!captured) return FALSE;
    captured->ok = TRUE;
    md->di = menu_checked_image(captured, m->mdp_Width, m->mdp_Height);
    DDisposeImageContainer(captured);
    if (!md->di) return FALSE;
    md->highlight = menu_checked_image(md->di, md->di->w, md->di->h);
    if (!md->highlight) return FALSE;
    if (sd->di->img_menu)
    {
        tw = menu_typed_metric(sd->di->img_menu->w, scale);
        th = menu_typed_metric(sd->di->img_menu->h, scale);
        md->scaled_menu = menu_checked_image(sd->di->img_menu, tw, th);
        if (!md->scaled_menu) return FALSE;
        if (data->dc->MenuIsTiled)
        {
            left = menu_typed_metric(data->dc->MenuTileLeft, scale);
            right = menu_typed_metric(data->dc->MenuTileRight, scale);
            top = menu_typed_metric(data->dc->MenuTileTop, scale);
            bottom = menu_typed_metric(data->dc->MenuTileBottom, scale);
            if (left >= tw || right >= tw || top >= th || bottom >= th ||
                left + right >= tw || top + bottom >= th) return FALSE;
        }
    }
    /* A themed tile frame can exceed the popup: Ice's 15+15 rows exceed the
     * 18-row menu bar at 100 %. Like MDM_INITMENU, compose the frame at its
     * minimum extent and sample it down to the popup, instead of refusing. */
    tilew = left + right > md->di->w ? left + right : md->di->w;
    tileh = top + bottom > md->di->h ? top + bottom : md->di->h;
    radius = menu_typed_metric(1, scale);
    for (y = 0; y < md->di->h; y++)
        for (x = 0; x < md->di->w; x++)
        {
            ULONG pixel = 0xff000000UL, texture = 0;
            if (md->scaled_menu)
            {
                ULONG sx = data->dc->MenuIsTiled ? menu_sample_axis(
                    (UQUAD)x * tilew / md->di->w, tilew,
                    md->scaled_menu->w, left, right) : x % md->scaled_menu->w;
                ULONG sy = data->dc->MenuIsTiled ? menu_sample_axis(
                    (UQUAD)y * tileh / md->di->h, tileh,
                    md->scaled_menu->h, top, bottom) : y % md->scaled_menu->h;
                texture = md->scaled_menu->data[sy * md->scaled_menu->w + sx];
            }
            for (c = 0; c <= 16; c += 8)
            {
                ULONG v = menu_blur_channel(md->highlight, x, y, c, radius);
                if (md->scaled_menu)
                {
                    ULONG alpha = texture >> 24;
                    ULONG mix = ((((texture >> c) & 255) * 204) >> 8) + ((v * 51) >> 8);
                    v = data->dc->MenuIsTiled ?
                        ((mix * alpha) >> 8) + ((v * (255 - alpha)) >> 8) : mix;
                }
                pixel |= v << c;
            }
            md->di->data[y * md->di->w + x] = pixel;
        }
    CopyMem(md->di->data, md->highlight->data, (ULONG)md->di->w * md->di->h * sizeof(ULONG));
    DSetImageTint(md->highlight, 255 - (data->dc->MenuHighlightTint >> 24),
        data->dc->MenuHighlightTint & 0xffffff);
    return TRUE;
}

static IPTR menudecor_renderbackground(Class *cl, Object *obj, struct mdpDrawBackground *msg)
{
    struct menudecor_data *data = INST_DATA(cl, obj);
    struct RastPort    *rp = msg->mdp_RPort;
    struct DecorImage    *di;
    struct MenuData    *md = (struct MenuData *) msg->mdp_UserBuffer;
    UWORD               flags = msg->mdp_Flags;

    if (md->context)
    {
        struct DecorImage *image = (flags & HIGHITEM) ? md->highlight : md->di;
        if (!image || msg->mdp_ItemLeft < 0 || msg->mdp_ItemTop < 0 ||
            msg->mdp_ItemWidth <= 0 || msg->mdp_ItemHeight <= 0 ||
            (QUAD)msg->mdp_ItemLeft + msg->mdp_ItemWidth > image->w ||
            (QUAD)msg->mdp_ItemTop + msg->mdp_ItemHeight > image->h) return FALSE;
        DDrawPartImageToRP(rp, image, msg->mdp_ItemLeft, msg->mdp_ItemTop,
            msg->mdp_ItemLeft, msg->mdp_ItemTop, msg->mdp_ItemWidth, msg->mdp_ItemHeight);
        return TRUE;
    }
    if ((flags & HIGHITEM) && md->di)
    {
        di = DNewDecorImageContainer(msg->mdp_ItemWidth, msg->mdp_ItemHeight);
        if (di)
        {
            DDrawPartToImage(md->di, di, msg->mdp_ItemLeft, msg->mdp_ItemTop, msg->mdp_ItemWidth, msg->mdp_ItemHeight, 0, 0);
            DSetImageTint(di, 255 - (data->dc->MenuHighlightTint >> 24), data->dc->MenuHighlightTint & 0xffffff);
            DPutImageToRP(rp, di, msg->mdp_ItemLeft, msg->mdp_ItemTop);
        }
    }
    else
    {
        if (md->di) DDrawPartImageToRP(rp, md->di, msg->mdp_ItemLeft, msg->mdp_ItemTop, msg->mdp_ItemLeft, msg->mdp_ItemTop, msg->mdp_ItemWidth, msg->mdp_ItemHeight);
    }

    return TRUE;
}

static IPTR menudecor_initmenu(Class *cl, Object *obj, struct mdpInitMenu *msg)
{
    struct menudecor_data  *data = INST_DATA(cl, obj);
    struct RastPort        *rp = msg->mdp_RPort;
    struct MenuData        *md = (struct MenuData *) msg->mdp_UserBuffer;
    struct ScreenData      *sd = (struct ScreenData *) msg->mdp_ScreenUserBuffer;

    SETIMAGE_MEN(menu);
    SETIMAGE_MEN(amigakey);
    SETIMAGE_MEN(menucheck);
    SETIMAGE_MEN(submenu);

    if (data->dc->MenuIsTiled)
    {
        md->img_menu_ti = AllocVec(sizeof(struct TileInfo), MEMF_ANY | MEMF_CLEAR);
        md->img_menu_ti->TileLeft   = data->dc->MenuTileLeft;
        md->img_menu_ti->TileRight  = data->dc->MenuTileRight;
        md->img_menu_ti->TileBottom = data->dc->MenuTileBottom;
        md->img_menu_ti->TileTop    = data->dc->MenuTileTop;
    }

    if ((msg->mdp_MenuDecorFlags & MDP_MDF_MENU) && !(msg->mdp_MenuDecorFlags & MDP_MDF_MENUS_UNDERMOUSE))
    {
        /* Special handling for pulled down menu bar */
        LONG height = msg->mdp_Height;

        /* Increase height for rendering if needed */
        if ((data->dc->MenuIsTiled) && (height < (md->img_menu_ti->TileBottom + md->img_menu_ti->TileTop)))
            height = (md->img_menu_ti->TileBottom + md->img_menu_ti->TileTop);

        md->di = DNewDecorImageContainer(msg->mdp_Width, height);
        if (md->di)
        {
            md->di->ok = TRUE;
            DRenderMenuBarBackground(md->di, md->img_menu, md->img_menu_ti, 20);

            /* Scale down if needed */
            if (height > msg->mdp_Height)
            {
                struct DecorImage * sni = DScaleDecorImage(md->di, msg->mdp_Width, msg->mdp_Height);
                if (sni)
                {
                    DDisposeImageContainer(md->di);
                    md->di = sni;
                    md->di->ok = TRUE;
                }
            }
        }
    }
    else
    {
        md->di = DGetImageFromRP(rp, msg->mdp_Left, msg->mdp_Top, msg->mdp_Width, msg->mdp_Height);
        if (md->di)
        {
            md->di->ok = TRUE;
            DRenderMenuBackground(md->di, md->img_menu, md->img_menu_ti, 20);
        }
    }


    return TRUE;
}

static IPTR menudecor_exitmenu(Class *cl, Object *obj, struct mdpExitMenu *msg)
{
    struct MenuData     *md = (struct MenuData *) msg->mdp_UserBuffer;

    if (md->highlight) DDisposeImageContainer(md->highlight);
    if (md->scaled_menu) DDisposeImageContainer(md->scaled_menu);
    if (md->di) DDisposeImageContainer(md->di);
    if (md->map) FreeBitMap(md->map);
    if (md->img_menu_ti) FreeVec(md->img_menu_ti);

    return TRUE;
}

static void DisposeMenuSkinning(struct menudecor_data *data)
{
}

static BOOL InitMenuSkinning(struct menudecor_data *data, struct DecorImages * di, struct DecorConfig * dc)
{
    if ((!dc) || (!di))
        return FALSE;
        
    data->dc = dc;

    /* Set pointers to gadget images, used only to get gadget sizes as they
       are requested prior to creation of menu object */
    data->img_amigakey  = di->img_amigakey;
    data->img_menucheck = di->img_menucheck;
    data->img_submenu   = di->img_submenu;

    return TRUE;
}

static IPTR menudecor__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    struct menudecor_data *data;

    D(bug("menudecor__OM_NEW(tags @ 0x%p)\n", msg->ops_AttrList));

    obj = (Object *)DoSuperMethodA(cl, obj, (Msg)msg);
    if (obj)
    {
        data = INST_DATA(cl, obj);

        struct DecorImages * di = (struct DecorImages *) GetTagData(MDA_DecorImages, (IPTR) NULL, msg->ops_AttrList);
        struct DecorConfig * dc = (struct DecorConfig *) GetTagData(MDA_DecorConfig, (IPTR) NULL, msg->ops_AttrList);

        D(bug("menudecor__OM_NEW: DecorImages @ 0x%p\n", di));
        D(bug("menudecor__OM_NEW: DecorConfig @ 0x%p\n", dc));

        if (!InitMenuSkinning(data, di, dc))
        {
            CoerceMethod(cl, obj ,OM_DISPOSE);
            obj = NULL;
        }
    }
    return (IPTR)obj;
}


static IPTR menudecor__OM_DISPOSE(Class *cl, Object *obj, struct opSet *msg)
{
    struct menudecor_data *data = INST_DATA(cl, obj);

    DisposeMenuSkinning(data);

    return DoSuperMethodA(cl, obj, (Msg)msg);
}

static IPTR menudecor_dispatcher(struct IClass *cl, Object *obj, Msg msg)
{
    IPTR retval;

    switch(msg->MethodID)
    {
        case OM_NEW:
            retval = menudecor__OM_NEW(cl, obj, (struct opSet *) msg);
            break;

        case OM_DISPOSE:
            retval = menudecor__OM_DISPOSE(cl, obj, (struct opSet *) msg);
            break;

        case MDM_QUERYCAPABILITIES:
            retval = OCLASS(obj) == cl ? MDM_CAP_SCALE : 0;
            break;
        case MDM_INITSCALEMENU:
            retval = menudecor_init_scale(cl, obj, (struct mdpInitScaleMenu *)msg);
            break;
        case MDM_GETSCALESPACES:
            retval = menudecor_scale_spaces(cl, obj, (struct mdpGetScaleSpaces *)msg);
            break;
        case MDM_GETSCALEDEFSIZE_SYSIMAGE:
            retval = menudecor_prepare_image(cl, obj, (struct mdpGetScaleDefSizeSysImage *)msg);
            break;
        case MDM_DRAWSCALE_SYSIMAGE:
            retval = menudecor_draw_prepared(cl, obj, (struct mdpDrawScaleSysImage *)msg);
            break;
        case MDM_RELEASESCALE_SYSIMAGE:
            retval = menudecor_release_prepared(cl, obj, (struct mdpReleaseScaleSysImage *)msg);
            break;
        case MDM_DRAW_SYSIMAGE:
            retval = menudecor_draw_sysimage(cl, obj, (struct mdpDrawSysImage *)msg);
            break;

        case MDM_GETDEFSIZE_SYSIMAGE:
            retval = menudecor_getdefsizes(cl, obj, (struct mdpGetDefSizeSysImage *) msg);
            break;

        case MDM_DRAWBACKGROUND:
            retval = menudecor_renderbackground(cl, obj, (struct mdpDrawBackground *)msg);
            break;

        case MDM_INITMENU:
            retval = menudecor_initmenu(cl, obj, (struct mdpInitMenu *)msg);
            break;

        case MDM_EXITMENU:
            retval = menudecor_exitmenu(cl, obj, (struct mdpExitMenu *)msg);
            break;

        case MDM_GETMENUSPACES:
            retval = menudecor_getmenuspaces(cl, obj, (struct mdpGetMenuSpaces *)msg);
            break;

        default:
            retval = DoSuperMethodA(cl, obj, msg);
            break;
    }

    return retval;
}

struct IClass * MakeMenuDecorClass()
{
    struct IClass * cl = MakeClass(NULL, MENUDECORCLASS, NULL, sizeof(struct menudecor_data), 0);
    if (cl)
    {
        cl->cl_Dispatcher.h_Entry    = HookEntry;
        cl->cl_Dispatcher.h_SubEntry = (HOOKFUNC)menudecor_dispatcher;
    }
    
    return cl;
}
