/*
    Copyright (C) 1995-2020, The AROS Development Team. All rights reserved.

    Copyright (C) 2026, John KNIPPER <code@jkn.me>.

    Decoration support code
*/

#include <intuition/screens.h>
#include <intuition/intuition.h>
#include <intuition/windecorclass.h>
#include <intuition/scrdecorclass.h>
#include <intuition/gadgetclass.h>
#include <intuition/extensions.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>

#include "intuition_intern.h"
#include "intuition_customize.h"
#include "inputhandler_actions.h"
#include "prepareddecor.h"
#include "screenrendercontext.h"
#include <intuition/imageclass.h>
#include <exec/memory.h>

BOOL int_LoadDecorator(const char *name, struct IntScreen *screen, struct IntuitionBase *IntuitionBase)
{
    IPTR userbuffersize;
    struct NewDecorator *nd = NULL;

    ObtainSemaphore(&((struct IntIntuitionBase *)(IntuitionBase))->ScrDecorSem);

    if (name)
    {
        nd = (struct NewDecorator *)FindName(&GetPrivIBase(IntuitionBase)->Decorations, name);
    }
    else
    {
        struct DosLibrary *DOSBase = GetPrivIBase(IntuitionBase)->DOSBase;

        /* Open dos.library only once, when first needed */
        if (!DOSBase)
            GetPrivIBase(IntuitionBase)->DOSBase = DOSBase = (struct DosLibrary *)TaggedOpenLibrary(TAGGEDOPEN_DOS);

        if (DOSBase)
        {
            struct Node *node;

            if (!IsListEmpty(&GetPrivIBase(IntuitionBase)->Decorations))
            {
                node = GetPrivIBase(IntuitionBase)->Decorations.lh_Head;
                for (; node->ln_Succ; node = node->ln_Succ)
                {
                    struct NewDecorator *d = (struct NewDecorator *) node;

                    if ((d->nd_IntPattern != NULL) && (screen->Screen.Title != NULL))
                    {
                        if (MatchPattern(d->nd_IntPattern, screen->Screen.Title))
                            nd = d;
                    }
                }
            }
        }
    }

    if (!nd)
        nd = ((struct IntIntuitionBase *)(IntuitionBase))->Decorator;

    if (nd)
        nd->nd_cnt++;

    ReleaseSemaphore(&((struct IntIntuitionBase *)(IntuitionBase))->ScrDecorSem);

    screen->Decorator = nd;
    if (nd != NULL)
    {
        screen->ScrDecorObj  = NewObjectA(nd->nd_ScreenClass, NULL, nd->nd_ScreenTags);
        screen->MenuDecorObj = NewObjectA(nd->nd_MenuClass, NULL, nd->nd_MenuTags);
        screen->WinDecorObj  = NewObjectA(nd->nd_WindowClass, NULL, nd->nd_WindowTags);
    }
    else
    {
        screen->ScrDecorObj  = NewObjectA(GetPrivIBase(IntuitionBase)->ScrDecorClass, NULL, GetPrivIBase(IntuitionBase)->ScrDecorTags);
        screen->MenuDecorObj = NewObjectA(GetPrivIBase(IntuitionBase)->MenuDecorClass, NULL, GetPrivIBase(IntuitionBase)->MenuDecorTags);
        screen->WinDecorObj  = NewObjectA(GetPrivIBase(IntuitionBase)->WinDecorClass, NULL, GetPrivIBase(IntuitionBase)->WinDecorTags);
    }

    GetAttr(SDA_UserBuffer, screen->ScrDecorObj, &userbuffersize);
    screen->DecorUserBufferSize = userbuffersize;
    if (userbuffersize)
    {
        screen->DecorUserBuffer = (IPTR)AllocMem(userbuffersize, MEMF_ANY | MEMF_CLEAR);
        if (!screen->DecorUserBuffer)
            return FALSE;
    }

    return TRUE;
}

struct RemoveDecoratorMsg
{
    struct IntuiActionMsg  msg;
    struct NewDecorator   *nd;
};

/* This is called on the input.device's context */

static VOID int_removedecorator(struct RemoveDecoratorMsg *m,
                               struct IntuitionBase *IntuitionBase)
{
    struct DecoratorMessage msg;
    struct MsgPort *port = CreateMsgPort();

    if (port)
    {
        Remove((struct Node *)m->nd);
        FreeVec(m->nd->nd_IntPattern);

        msg.dm_Message.mn_ReplyPort = port;
        msg.dm_Message.mn_Magic     = MAGIC_DECORATOR;
        msg.dm_Message.mn_Version   = DECORATOR_VERSION;
        msg.dm_Class                = DM_CLASS_DESTROYDECORATOR;
        msg.dm_Code                 = 0;
        msg.dm_Flags                = 0;
        msg.dm_Object               = (IPTR)m->nd;

        PutMsg(m->nd->nd_Port, (struct Message *)&msg);
        WaitPort(port);
        GetMsg(port);
        DeleteMsgPort(port);
    }
}

/* !!! ScrDecorSem must be obtained and released by the caller !!! */
void int_UnloadDecorator(struct NewDecorator *tnd, struct IntuitionBase *IntuitionBase)
{
    if ((tnd->nd_cnt == 0) && (tnd->nd_Port != NULL))
    {
        struct RemoveDecoratorMsg msg;

        msg.nd = tnd;
        DoASyncAction((APTR)int_removedecorator, &msg.msg, sizeof(msg), IntuitionBase);
    }
}

/* The public border fields are logical. BarHeight is the physical raster
 * extent minus one; scale only its fixed padding, not the raster font twice. */
static LONG scale_bar_height(ULONG fontheight, LONG logicaltop,
    LONG logicalvborder, ULONG scale)
{
    LONG padding = (logicaltop - 1 + logicalvborder * 2) * (LONG)scale;
    padding = (padding + (padding < 0 ? -50 : 50)) / 100;
    return (LONG)fontheight + padding - 1;
}

BOOL int_InitDecorator(struct Screen *screen, struct TextFont *basefont, ULONG scale)
{
    BOOL ok;
    struct sdpInitScreen msg;

    if (!basefont || scale < 100 || scale > 400 || scale % 25) return FALSE;
    msg.MethodID        = SDM_INITSCREEN;
    msg.sdp_Screen      = screen;
    msg.sdp_TrueColor   = (GetPrivScreen(screen)->DInfo.dri_Flags & DRIF_DIRECTCOLOR) ? TRUE : FALSE;
    msg.sdp_FontHeight  = GetPrivScreen(screen)->DInfo.dri_Font->tf_YSize;
    msg.sdp_BaseFontHeight = basefont->tf_YSize;
    msg.sdp_Scale = scale;
    msg.sdp_BarVBorder  = screen->BarVBorder;
    msg.sdp_BarHBorder  = screen->BarHBorder;
    msg.sdp_MenuVBorder = screen->MenuVBorder;
    msg.spd_MenuHBorder = screen->MenuHBorder;
    msg.sdp_WBorTop     = screen->WBorTop;
    msg.sdp_WBorLeft    = screen->WBorLeft;
    msg.sdp_WBorRight   = screen->WBorRight;
    msg.sdp_WBorBottom  = screen->WBorBottom;
    msg.sdp_TitleHack   = 0;
    msg.sdp_BarHeight = scale_bar_height(msg.sdp_FontHeight,
        screen->WBorTop, screen->BarVBorder, scale);
    /* The defaults are logical on entry; publish physical bar/menu padding
     * exactly once. Theme init may replace these proposed values. */
    msg.sdp_BarVBorder = (msg.sdp_BarVBorder * scale + 50) / 100;
    msg.sdp_BarHBorder = (msg.sdp_BarHBorder * scale + 50) / 100;
    msg.sdp_MenuVBorder = (msg.sdp_MenuVBorder * scale + 50) / 100;
    msg.spd_MenuHBorder = (msg.spd_MenuHBorder * scale + 50) / 100;
    msg.sdp_UserBuffer  = ((struct IntScreen *)screen)->DecorUserBuffer;

    ok = DoMethodA(GetPrivScreen(screen)->ScrDecorObj, &msg.MethodID);
    if (ok && (msg.sdp_BarHeight > 127 || msg.sdp_BarVBorder > 127 ||
        msg.sdp_BarHBorder > 127 || msg.sdp_MenuVBorder > 127 ||
        msg.spd_MenuHBorder > 127 || msg.sdp_WBorTop < 0 ||
        msg.sdp_WBorLeft < 0 || msg.sdp_WBorRight < 0 || msg.sdp_WBorBottom < 0))
        ok = FALSE;
    if (ok)
    {
        screen->BarHeight     = msg.sdp_BarHeight;
        screen->BarVBorder    = msg.sdp_BarVBorder;
        screen->BarHBorder    = msg.sdp_BarHBorder;
        screen->MenuVBorder   = msg.sdp_MenuVBorder;
        screen->MenuHBorder   = msg.spd_MenuHBorder;
        screen->WBorTop       = msg.sdp_WBorTop;
        screen->WBorLeft      = msg.sdp_WBorLeft;
        screen->WBorRight     = msg.sdp_WBorRight;
        screen->WBorBottom    = msg.sdp_WBorBottom;
    }

    return ok;
}

void int_CalcSkinInfo(struct Screen *screen, struct IntuitionBase *IntuitionBase)
{
    if (screen->FirstGadget)
    {
        struct sdpLayoutScreenGadgets lmsg;

        lmsg.MethodID       = SDM_LAYOUT_SCREENGADGETS;
        lmsg.sdp_TrueColor  = (GetPrivScreen(screen)->DInfo.dri_Flags & DRIF_DIRECTCOLOR) ? TRUE : FALSE;
        lmsg.sdp_Layer      = screen->BarLayer;
        lmsg.sdp_Gadgets    = screen->FirstGadget;
        lmsg.sdp_Flags      = SDF_LSG_INITIAL | SDF_LSG_MULTIPLE;
        lmsg.sdp_UserBuffer = ((struct IntScreen *)screen)->DecorUserBuffer;

        DoMethodA(((struct IntScreen *)(screen))->ScrDecorObj, &lmsg.MethodID);
    }
}

/* A detached image generation and scalar screen/gadget snapshot. Never copy
 * Screen/Gadget wholesale: their lists, ports and private state remain live. */
struct PreparedDepthGadget
{
    WORD left, top, width, height;
    UWORD flags;
    APTR render;
};
struct PreparedScreenScaleDecoration
{
    struct Screen *screen;
    struct Gadget *gadget;
    struct ScreenScaleMetrics old, next;
    struct PreparedDepthGadget oldgadget, nextgadget;
    BOOL installed;
};

static void capture_scale_metrics(struct Screen *screen, struct ScreenScaleMetrics *m)
{
    m->BarHeight = screen->BarHeight;
    m->BarVBorder = screen->BarVBorder;
    m->BarHBorder = screen->BarHBorder;
    m->MenuVBorder = screen->MenuVBorder;
    m->MenuHBorder = screen->MenuHBorder;
    m->WBorTop = screen->WBorTop;
    m->WBorLeft = screen->WBorLeft;
    m->WBorRight = screen->WBorRight;
    m->WBorBottom = screen->WBorBottom;
    m->DepthRightInset = -1;
    m->DepthTop = 0;
}

static BOOL valid_scale_metrics(const struct ScreenScaleMetrics *m)
{
    /* Public Screen stores these as signed bytes. Reject truncation before
     * any live state change, rather than silently creating a negative bar. */
    return m->BarHeight >= 0 && m->BarHeight <= 127 &&
        m->BarVBorder >= 0 && m->BarVBorder <= 127 &&
        m->BarHBorder >= 0 && m->BarHBorder <= 127 &&
        m->MenuVBorder >= 0 && m->MenuVBorder <= 127 &&
        m->MenuHBorder >= 0 && m->MenuHBorder <= 127 &&
        m->WBorTop >= 0 && m->WBorTop <= 127 &&
        m->WBorLeft >= 0 && m->WBorLeft <= 127 &&
        m->WBorRight >= 0 && m->WBorRight <= 127 &&
        m->WBorBottom >= 0 && m->WBorBottom <= 127 &&
        m->DepthTop >= -32768 && m->DepthTop <= 32767 &&
        m->DepthRightInset >= -32768 && m->DepthRightInset <= 32767;
}

static void capture_depth_gadget(struct Gadget *g, struct PreparedDepthGadget *f)
{
    f->left = g->LeftEdge; f->top = g->TopEdge;
    f->width = g->Width; f->height = g->Height;
    f->flags = g->Flags; f->render = g->GadgetRender;
}

struct PreparedScreenScaleDecoration *PrepareScreenScaleDecoration(
    struct Screen *screen, struct TextFont *basefont, struct TextFont *font,
    ULONG scale, struct ScreenScaleResult *result, struct IntuitionBase *IntuitionBase)
{
    struct PreparedScreenScaleDecoration *p;
    struct IntScreen *is;
    struct sdpPrepareScale message;
    if (!screen || !font || !font->tf_YSize || !basefont ||
        !basefont->tf_YSize || scale < 100 || scale > 400 || scale % 25)
        return NULL;
    is = GetPrivScreen(screen);
    {
        ULONG capability = SDM_QUERYCAPABILITIES;
        if (!(DoMethodA(is->ScrDecorObj, (Msg)&capability) & SDM_CAP_PREPARESCALE))
        {
            if (result) result->ssr_Reason = SSR_REASON_UNSUPPORTED;
            return NULL;
        }
    }
    p = AllocVec(sizeof(*p), MEMF_PUBLIC | MEMF_CLEAR);
    if (!p)
    {
        if (result) result->ssr_Reason = SSR_REASON_NO_MEMORY;
        return NULL;
    }
    p->screen = screen;
    p->gadget = (struct Gadget *)is->depthgadget;
    capture_scale_metrics(screen, &p->old);
    {
        const struct ScreenRenderContext *context = CurrentScreenRenderContext(screen);
        const struct ScreenScaleMetrics *logical = ScreenRenderContextLogicalMetrics(context);
        if (!logical) goto fail;
        p->next = *logical;
        p->next.BarHeight = scale_bar_height(font->tf_YSize,
            logical->WBorTop, logical->BarVBorder, scale);
        p->next.BarVBorder = (logical->BarVBorder * scale + 50) / 100;
        p->next.BarHBorder = (logical->BarHBorder * scale + 50) / 100;
        p->next.MenuVBorder = (logical->MenuVBorder * scale + 50) / 100;
        p->next.MenuHBorder = (logical->MenuHBorder * scale + 50) / 100;
    }
    message.MethodID = SDM_PREPARESCALE;
    message.sdp_Screen = screen;
    message.sdp_Font = font;
    message.sdp_BaseFont = basefont;
    message.sdp_Scale = scale;
    message.sdp_UserBuffer = is->DecorUserBuffer;
    message.sdp_Metrics = &p->next;
    if (!DoMethodA(is->ScrDecorObj, (Msg)&message) || !valid_scale_metrics(&p->next))
        goto fail;
    if (p->gadget)
    {
        capture_depth_gadget(p->gadget, &p->oldgadget);
        p->nextgadget = p->oldgadget;
        p->nextgadget.height = p->next.BarHeight + 1;
    }
    return p;
fail:
    ReleaseScreenScaleDecoration(p, IntuitionBase);
    return NULL;
}

BOOL BindScreenScaleDecoration(struct PreparedScreenScaleDecoration *p,
    const struct ScreenRenderContext *context)
{
    Object *image;
    LONG left;
    if (!p || !context || context->src_PhysicalDrawInfo->dri_Screen != p->screen ||
        context->src_PhysicalBarHeight != (ULONG)p->next.BarHeight + 1) return FALSE;
    if (!p->gadget || (p->screen->Flags & SCREENQUIET)) return TRUE;
    image = ScreenRenderContextDepthImage(context);
    if (!image || ((struct Image *)image)->Width <= 0) return FALSE;
    p->nextgadget.width = ((struct Image *)image)->Width;
    left = -(LONG)p->nextgadget.width - p->next.DepthRightInset;
    if (left < -32768 || left > 32767) return FALSE;
    p->nextgadget.left = left;
    p->nextgadget.top = p->next.DepthTop;
    p->nextgadget.flags = (p->nextgadget.flags & ~GFLG_RELWIDTH) | GFLG_RELRIGHT;
    p->nextgadget.render = image;
    return TRUE;
}

const struct ScreenScaleMetrics *PreparedScreenScaleDecorationMetrics(
    const struct PreparedScreenScaleDecoration *p)
{
    return p ? &p->next : NULL;
}

void InstallScreenScaleDecoration(struct PreparedScreenScaleDecoration *p, BOOL rollback)
{
    struct Screen *screen;
    const struct ScreenScaleMetrics *m;
    if (!p) return;
    screen = p->screen;
    m = rollback ? &p->old : &p->next;
    screen->BarHeight = m->BarHeight;
    screen->BarVBorder = m->BarVBorder;
    screen->BarHBorder = m->BarHBorder;
    screen->MenuVBorder = m->MenuVBorder;
    screen->MenuHBorder = m->MenuHBorder;
    screen->WBorTop = m->WBorTop;
    screen->WBorLeft = m->WBorLeft;
    screen->WBorRight = m->WBorRight;
    screen->WBorBottom = m->WBorBottom;
    if (p->gadget)
    {
        const struct PreparedDepthGadget *f = rollback ? &p->oldgadget : &p->nextgadget;
        p->gadget->LeftEdge = f->left; p->gadget->TopEdge = f->top;
        p->gadget->Width = f->width; p->gadget->Height = f->height;
        p->gadget->Flags = f->flags; p->gadget->GadgetRender = f->render;
    }
    p->installed = !rollback;
}

void ReleaseScreenScaleDecoration(struct PreparedScreenScaleDecoration *p,
    struct IntuitionBase *IntuitionBase)
{
    if (!p) return;
    /* Depth images belong to retained immutable screen generations. */
    FreeVec(p);
}
