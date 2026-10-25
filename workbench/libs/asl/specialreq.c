/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Copyright (C) 1995-2001, The AROS Development Team. All rights reserved.
    This file is part of AROS and is distributed under the AROS Public License 1.1.
*/
#include <proto/alib.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/screens.h>
#include <intuition/gadgetclass.h>
#include <string.h>
#include "asl_intern.h"
#include "layout.h"
#include "specialreq.h"
#if USE_SHARED_COOLIMAGES
#include <libraries/coolimages.h>
#include <proto/coolimages.h>
#else
#include "coolimages.h"
#endif

#define OK_ID 1
#define CANCEL_ID 2
#define STRING_ID 3

struct StringModal
{
    struct LayoutData layout;
    struct LayoutData *parent;
    Object *ok, *cancel, *string;
    struct Hook edit;
    STRPTR oktext, canceltext;
    const struct ScreenRenderContext *initialContext;
#if SPECIALREQ_COOL_BUTTONS
    const struct CoolImage *useimage, *cancelimage;
#endif
};

/* All three objects belong to this dialog. Candidate strings contain no live
 * edit snapshot: InputTask may continue editing the original StringInfo while
 * these fonts, frames and images are prepared by the requester Process. */
static BOOL BuildStringResources(struct StringModal *modal, struct LayoutData *ld,
    Object **ok, Object **cancel, Object **string, BOOL initial, STRPTR text,
    struct AslBase_intern *AslBase)
{
    LONG width, other, height, left, top;
    struct TagItem buttons[] = {
        {GA_Text, (IPTR)modal->oktext}, {GA_ID,OK_ID}, {GA_UserData,0},
        {GA_Left,0}, {GA_Top,0}, {GA_Width,0}, {GA_Height,0},
        {GA_Image,0}, {GA_RelVerify,TRUE}, {TAG_IGNORE,0}, {TAG_IGNORE,0}, {TAG_DONE,0}};
    struct TagItem strings[] = {
        {GA_Previous,0}, {GA_ID,STRING_ID}, {GA_Left,0}, {GA_Top,0},
        {GA_Width,0}, {GA_Height,0}, {GA_RelVerify,TRUE},
        {STRINGA_MaxChars,256}, {STRINGA_TextVal,(IPTR)(initial ? text : (STRPTR)"")},
        {STRINGA_BufferPos,initial ? strlen(text) : 0}, {STRINGA_DispPos,0},
        {STRINGA_Font,(IPTR)ld->ld_Font}, {STRINGA_EditHook,(IPTR)&modal->edit}, {TAG_DONE,0}};
    width = TextLength(&ld->ld_DummyRP, modal->oktext, strlen(modal->oktext));
    other = TextLength(&ld->ld_DummyRP, modal->canceltext, strlen(modal->canceltext));
    height = ld->ld_Font->tf_YSize + AslScaleValue(ld, BUTTONEXTRAHEIGHT);
#if SPECIALREQ_COOL_BUTTONS
    if (ld->ld_TrueColor)
    {
        LONG imageheight = 0;
        if (modal->useimage)
        {
            width += AslScaleValue(ld, IMAGEBUTTONEXTRAWIDTH) + AslScaleValue(ld, modal->useimage->width);
            imageheight = AslScaleValue(ld, modal->useimage->height);
        }
        if (modal->cancelimage)
        {
            LONG h = AslScaleValue(ld, modal->cancelimage->height);
            other += AslScaleValue(ld, IMAGEBUTTONEXTRAWIDTH) + AslScaleValue(ld, modal->cancelimage->width);
            if (h > imageheight) imageheight = h;
        }
        if (imageheight)
        {
            imageheight += AslScaleValue(ld, IMAGEBUTTONEXTRAHEIGHT);
            if (height < imageheight) height = imageheight;
        }
    }
    buttons[9] = (struct TagItem){ASLBT_CoolImage,(IPTR)modal->useimage};
#endif
    if (other > width) width = other;
    width += AslScaleValue(ld, BUTTONEXTRAWIDTH);
    left = ld->ld_WBorLeft + AslScaleValue(ld, OUTERSPACINGX);
    top = ld->ld_WBorTop + AslScaleValue(ld, OUTERSPACINGY);
    ld->ld_MinWidth = ld->ld_WBorLeft + ld->ld_WBorRight +
        2 * AslScaleValue(ld, OUTERSPACINGX) + 3 * width;
    ld->ld_MinHeight = ld->ld_WBorTop + ld->ld_WBorBottom +
        2 * AslScaleValue(ld, OUTERSPACINGY) + 2 * height + AslScaleValue(ld,GADGETSPACINGY);
    if (width <= 0 || height <= 0 || ld->ld_MinWidth > 32767 || ld->ld_MinHeight > 32767)
        return FALSE;
    buttons[2].ti_Data = (IPTR)ld;
    buttons[3].ti_Data = left;
    buttons[4].ti_Data = top + height + AslScaleValue(ld,GADGETSPACINGY);
    buttons[5].ti_Data = width; buttons[6].ti_Data = height;
    *ok = AslLayoutObject(ld, AslBase->aslbuttonclass, NULL, buttons, modal->ok, AslBase);
    if (!*ok) return FALSE;
    buttons[0].ti_Data = (IPTR)modal->canceltext; buttons[1].ti_Data = CANCEL_ID;
    buttons[3].ti_Data += 2 * width;
    buttons[10] = (struct TagItem){GA_Previous,(IPTR)*ok};
#if SPECIALREQ_COOL_BUTTONS
    buttons[9].ti_Data = (IPTR)modal->cancelimage;
#endif
    *cancel = AslLayoutObject(ld, AslBase->aslbuttonclass, NULL, buttons, modal->cancel, AslBase);
    if (!*cancel) return FALSE;
    strings[0].ti_Data = (IPTR)*cancel;
    strings[2].ti_Data = left; strings[3].ti_Data = top;
    strings[4].ti_Data = 3 * width; strings[5].ti_Data = height;
    *string = AslLayoutObject(ld, AslBase->aslstringclass, NULL, strings, modal->string, AslBase);
    if (!*string) return FALSE;
    ld->ld_GList = (struct Gadget *)*ok;
    return !ld->ld_GenerationFailed;
}

static VOID FreeStringResources(struct LayoutData *ld, struct AslBase_intern *AslBase)
{
    AslDisposeObjects(ld, AslBase);
    if (ld->ld_VisualInfo) FreeVisualInfo(ld->ld_VisualInfo);
    ld->ld_VisualInfo = NULL;
}

/* Process publication, separate from the OS atomic frame transaction. Every
 * resource is prepared before Fit or the private GadgetLock method; native
 * identity and input subscription never change. A newer context is retried. */
static BOOL RelayoutStringModal(struct StringModal *modal, struct AslBase_intern *AslBase)
{
    struct LayoutData *live = &modal->layout, candidate, retired;
    const struct ScreenRenderContext *ctx = NULL;
    Object *ok = NULL, *cancel = NULL, *string = NULL;
    struct AslInstallScale install;
    struct AslObjectRecord *record;
    if (!live->ld_ScaleAware) return TRUE;
    if (!GetAttr(SA_RenderContext,(Object *)live->ld_Screen,(IPTR *)&ctx) || !ctx) return FALSE;
    if (ctx == live->ld_RenderContext && live->ld_WBorLeft == live->ld_Window->BorderLeft &&
        live->ld_WBorTop == live->ld_Window->BorderTop && live->ld_WBorRight == live->ld_Window->BorderRight &&
        live->ld_WBorBottom == live->ld_Window->BorderBottom) return TRUE;
    candidate = *live;
    candidate.ld_Objects = NULL; candidate.ld_GadgetContext = NULL; candidate.ld_VisualInfo = NULL;
    candidate.ld_GenerationFailed = FALSE; candidate.ld_PrepareOnly = TRUE; candidate.ld_Rebuilding = TRUE;
    if (!AslPrepareContext(&candidate,AslBase)) goto failed;
    if (!BuildStringResources(modal,&candidate,&ok,&cancel,&string,FALSE,(STRPTR)"",AslBase)) goto failed;
    if (!FitWindowLayout(live->ld_Window,candidate.ld_RenderContext,candidate.ld_MinWidth,candidate.ld_MinHeight))
        goto failed;
    for (record = candidate.ld_Objects; record; record = record->next)
        if (record->kind == ASLOBJ_GADGET)
        {
            struct gpLayout layout = {GM_LAYOUT,NULL,TRUE};
            DoGadgetMethodA((struct Gadget *)record->object,live->ld_Window,NULL,(Msg)&layout);
        }
    if (!GetAttr(SA_RenderContext,(Object *)live->ld_Screen,(IPTR *)&ctx) || ctx != candidate.ld_RenderContext)
        goto failed;
    install = (struct AslInstallScale){ASLGM_INSTALLSCALE,NULL,live,&candidate};
    if (!DoGadgetMethodA((struct Gadget *)modal->ok,live->ld_Window,NULL,(Msg)&install)) goto failed;
    retired = candidate;
    retired.ld_GadgetContext = live->ld_GadgetContext; retired.ld_VisualInfo = live->ld_VisualInfo;
    /* Records/objects stay attached. Their resource pointers now refer to the
     * candidate context; detached candidate records own the losing resources. */
    live->ld_GadgetContext = candidate.ld_GadgetContext; live->ld_VisualInfo = candidate.ld_VisualInfo;
    live->ld_RenderContext = candidate.ld_RenderContext; live->ld_RenderScale = candidate.ld_RenderScale;
    live->ld_Font = candidate.ld_Font; live->ld_Dri = candidate.ld_Dri;
    live->ld_TextAttr = candidate.ld_TextAttr; live->ld_DummyRP = candidate.ld_DummyRP;
    live->ld_WBorLeft = candidate.ld_WBorLeft; live->ld_WBorRight = candidate.ld_WBorRight;
    live->ld_WBorTop = candidate.ld_WBorTop; live->ld_WBorBottom = candidate.ld_WBorBottom;
    live->ld_MinWidth = candidate.ld_MinWidth; live->ld_MinHeight = candidate.ld_MinHeight;
    SetFont(live->ld_Window->RPort,live->ld_Font);
    RefreshGList(live->ld_GList,live->ld_Window,NULL,-1);
    FreeStringResources(&retired,AslBase);
    if (!GetAttr(SA_RenderContext,(Object *)live->ld_Screen,(IPTR *)&ctx) || ctx != live->ld_RenderContext)
        return FALSE;
    return TRUE;
failed:
    FreeStringResources(&candidate,AslBase);
    return FALSE;
}

/* The parent Process is blocked inside this helper. Update its resources as
 * well as the modal's, while preserving its queued semantic events for the
 * outer handler. Both publications keep their original Window identities. */
static BOOL RefreshStringContexts(struct StringModal *modal, struct AslBase_intern *AslBase)
{
    struct LayoutData *parent = modal->parent;
    struct AslReqInfo *info = &AslBase->ReqInfo[parent->ld_IntReq->ir_ReqType];
    struct LayoutData *previous = parent->ld_GadgetContext;
    BOOL parentReady = AslRelayout(parent,info,AslBase);
    BOOL modalReady = RelayoutStringModal(modal,AslBase);
    /* Keep the ordinary parent retry alive if the modal is accepted before
     * its failed preparation can be retried inside this nested loop. */
    parent->ld_RetryScale = !parentReady && !parent->ld_PendingAbort;
    if (parentReady && parent->ld_ScaleAware && previous != parent->ld_GadgetContext)
    {
        ULONG command = parent->ld_Command;
        parent->ld_Command = LDCMD_LAYOUT;
        CallHookPkt(&info->GadgetryHook,parent,AslBase);
        parent->ld_Command = command;
    }
    return parentReady && modalReady;
}

/* Wait on both ports. Abort remains queued in the parent's owned pending list
 * for its ordinary handler; a scale never retires either port or input pool. */
static BOOL WaitStringEvents(struct StringModal *modal, BOOL retry, struct AslBase_intern *AslBase)
{
    struct LayoutData *parent = modal->parent;
    ULONG mask = (1UL << modal->layout.ld_Window->UserPort->mp_SigBit) |
        (1UL << parent->ld_Window->UserPort->mp_SigBit);
    if (AslAbortPending(parent,AslBase)) return FALSE;
    if (retry) { Delay(1); SetSignal(0,mask); }
    else Wait(mask);
    return !AslAbortPending(parent,AslBase);
}

static WORD ModalCoordinate(LONG value)
{
    return value < -32768 ? -32768 : value > 32767 ? 32767 : value;
}

static BOOL OpenStringModal(struct StringModal *modal, STRPTR title, STRPTR text,
    struct AslBase_intern *AslBase)
{
    struct LayoutData *ld = &modal->layout;
    struct Window *parent = modal->parent->ld_Window;
    struct NewWindow outer = {0};
    struct ScaleWindowGeometry geometry;
    const struct ScreenRenderContext *ctx;
    LONG centerX, centerY;
    if (!AslPrepareContext(ld,AslBase)) return FALSE;
    modal->initialContext = ld->ld_RenderContext;
    if (modal->parent->ld_FontOwned)
    {
        ld->ld_Font = modal->parent->ld_Font; ld->ld_TextAttr = modal->parent->ld_TextAttr;
        SetFont(&ld->ld_DummyRP,ld->ld_Font);
    }
    if (!BuildStringResources(modal,ld,&modal->ok,&modal->cancel,&modal->string,TRUE,text,AslBase))
        return FALSE;
    if (ld->ld_MinWidth > ld->ld_Screen->Width || ld->ld_MinHeight > ld->ld_Screen->Height)
        return FALSE;
    ctx = ld->ld_RenderContext;
    centerX = parent->LeftEdge + parent->Width / 2;
    centerY = parent->TopEdge + parent->Height / 2;
    if (modal->parent->ld_ScaleAware && !ld->ld_ScaleAware)
    { centerX /= ctx->src_LegacyScaleFactor; centerY /= ctx->src_LegacyScaleFactor; }
    outer.Width = ld->ld_MinWidth; outer.Height = ld->ld_MinHeight;
    outer.LeftEdge = ModalCoordinate(centerX - outer.Width / 2);
    outer.TopEdge = ModalCoordinate(centerY - outer.Height / 2);
    AslPrepareWindowGeometry(ld,&outer,&geometry);
    {
        struct TagItem tags[] = {
            {WA_CustomScreen,(IPTR)ld->ld_Screen}, {WA_Left,outer.LeftEdge}, {WA_Top,outer.TopEdge},
            {WA_Width,outer.Width}, {WA_Height,outer.Height}, {WA_AutoAdjust,TRUE}, {WA_Title,(IPTR)title},
            {WA_CloseGadget,TRUE}, {WA_DragBar,TRUE}, {WA_DepthGadget,TRUE}, {WA_Activate,FALSE},
            {WA_SimpleRefresh,TRUE}, {WA_NoCareRefresh,TRUE}, {WA_RMBTrap,TRUE},
            {WA_IDCMP,IDCMP_CLOSEWINDOW|IDCMP_GADGETUP|IDCMP_VANILLAKEY|IDCMP_RAWKEY|
                (ld->ld_ScaleAware ? IDCMP_CHANGESCALE|IDCMP_CHANGEWINDOW|IDCMP_NEWSIZE : 0)},
            {WA_Gadgets,(IPTR)modal->ok}, {WA_ScaleAware,ld->ld_ScaleAware},
            {WA_RenderContext,(IPTR)ctx}, {WA_Hidden,TRUE}, {WA_ScaleGeometry,(IPTR)&geometry}, {TAG_DONE,0}};
        if (!ld->ld_ScaleAware) FindTagItem(WA_ScaleGeometry,tags)->ti_Tag = TAG_IGNORE;
        ld->ld_Window = OpenWindowTagList(NULL,tags);
    }
    if (!ld->ld_Window) return FALSE;
    ld->ld_GadgetsAttached = TRUE;
    for (;;)
    {
        if (!GetAttr(SA_RenderContext,(Object *)ld->ld_Screen,(IPTR *)&ctx) ||
            ctx != modal->initialContext) return FALSE;
        if (RelayoutStringModal(modal,AslBase)) break;
        if (AslAbortPending(modal->parent,AslBase)) return FALSE;
        Delay(1);
    }
    ld->ld_GadgetContext->ld_Window = ld->ld_Window;
    SetFont(ld->ld_Window->RPort,ld->ld_Font);
    RefreshGList(ld->ld_GList,ld->ld_Window,NULL,-1);
    if (AslAbortPending(modal->parent,AslBase) || !ShowWindow(ld->ld_Window,NULL)) return FALSE;
    ActivateWindow(ld->ld_Window);
    ActivateGadget((struct Gadget *)modal->string,ld->ld_Window,NULL);
    return TRUE;
}

static VOID ResetStringLayout(struct StringModal *modal)
{
    modal->layout = *modal->parent;
    modal->layout.ld_Objects = NULL; modal->layout.ld_GadgetContext = NULL;
    modal->layout.ld_VisualInfo = NULL; modal->layout.ld_Window = NULL; modal->layout.ld_Window2 = NULL;
    modal->layout.ld_GadgetsAttached = FALSE; modal->layout.ld_GenerationFailed = FALSE;
    modal->layout.ld_PrepareOnly = FALSE; modal->layout.ld_Rebuilding = FALSE;
    modal->ok = modal->cancel = modal->string = NULL; modal->initialContext = NULL;
}

static VOID DisposeStringModal(struct StringModal *modal, struct AslBase_intern *AslBase)
{
    if (modal->layout.ld_Window)
    {
        /* This window owns the port created by OpenWindow (no WA_UserPort).
         * Native CloseWindow synchronously detaches InputTask first, then
         * drains/replies pending messages and deletes its saved owned port.
         * ModifyIDCMP(0) would delete that port before this protocol. */
        CloseWindow(modal->layout.ld_Window);
        modal->layout.ld_Window = NULL;
    }
    FreeStringResources(&modal->layout,AslBase);
}

STRPTR REQ_String(STRPTR title, STRPTR text, STRPTR ok, STRPTR cancel,
    struct LayoutData *parent, struct AslBase_intern *AslBase)
{
    struct StringModal *modal;
    STRPTR result = NULL;
    BOOL retry = FALSE, accept = FALSE, cancelled = FALSE;
    if (!(modal = AllocVec(sizeof(*modal),MEMF_ANY|MEMF_CLEAR))) return NULL;
    modal->parent = parent;
    modal->oktext = ok; modal->canceltext = cancel;
    modal->edit.h_Entry = (APTR)AROS_ASMSYMNAME(StringEditFunc);
    modal->edit.h_Data = AslBase;
#if SPECIALREQ_COOL_BUTTONS
#if USE_SHARED_COOLIMAGES
    if (CoolImagesBase)
    { modal->useimage = COOL_ObtainImageA(COOL_USEIMAGE_ID,NULL); modal->cancelimage = COOL_ObtainImageA(COOL_CANCELIMAGE_ID,NULL); }
#else
    modal->useimage = &cool_useimage; modal->cancelimage = &cool_cancelimage;
#endif
#endif
    for (;;)
    {
        const struct ScreenRenderContext *current = NULL;
        if (AslAbortPending(parent,AslBase)) goto finished;
        ResetStringLayout(modal);
        if (OpenStringModal(modal,title,text ? text : (STRPTR)"",AslBase)) break;
        GetAttr(SA_RenderContext,(Object *)parent->ld_Screen,(IPTR *)&current);
        /* A hidden, nonactive construction can be discarded on a context
         * race. No user input has been exposed; after reveal this Window is
         * never replaced. Ordinary initial allocation failure returns NULL. */
        if (!modal->initialContext || current == modal->initialContext ||
            AslAbortPending(parent,AslBase)) goto finished;
        DisposeStringModal(modal,AslBase);
        Delay(1);
    }
    while (!accept && !cancelled)
    {
        struct IntuiMessage *message;
        if (!WaitStringEvents(modal,retry,AslBase)) { cancelled = TRUE; break; }
        while ((message = (struct IntuiMessage *)GetMsg(modal->layout.ld_Window->UserPort)))
        {
            switch (message->Class)
            {
                case IDCMP_CLOSEWINDOW: cancelled = TRUE; break;
                case IDCMP_VANILLAKEY:
                    if (message->Code == 27) cancelled = TRUE;
                    else if (message->Code == 9)
                        ActivateGadget((struct Gadget *)modal->string,modal->layout.ld_Window,NULL);
                    break;
                case IDCMP_GADGETUP:
                    if (message->IAddress == modal->cancel) cancelled = TRUE;
                    else if (message->IAddress == modal->ok) accept = TRUE;
                    else if (message->IAddress == modal->string)
                    { if (message->Code == 0) accept = TRUE; else if (message->Code == 27) cancelled = TRUE; }
                    break;
                case IDCMP_CHANGESCALE:
                case IDCMP_CHANGEWINDOW:
                case IDCMP_NEWSIZE: retry = TRUE; break;
            }
            ReplyMsg((struct Message *)message);
        }
        if (AslAbortPending(parent,AslBase)) cancelled = TRUE;
        if (!accept && !cancelled) retry = !RefreshStringContexts(modal,AslBase);
    }
    if (accept && !cancelled)
    {
        STRPTR value;
        GetAttr(STRINGA_TextVal,modal->string,(IPTR *)&value);
        if (value && *value) result = VecPooledCloneString(value,NULL,parent->ld_IntReq->ir_MemPool,AslBase);
    }
finished:
    DisposeStringModal(modal,AslBase);
    FreeVec(modal);
    return result;
}
