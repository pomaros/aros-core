/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    This file is part of AROS and is distributed under the AROS Public License 1.1.
*/
#include <proto/alib.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/diskfont.h>
#include <proto/gadtools.h>
#include <proto/utility.h>
#include <proto/workbench.h>
#include <dos/dosextens.h>
#include <exec/memory.h>
#include <intuition/screens.h>
#include <intuition/gadgetclass.h>
#include <intuition/imageclass.h>
#include <string.h>
#include "asl_intern.h"
#include "layout.h"
#include "modereqhooks.h"
#include "modereqsupport.h"
#include "fontreqhooks.h"
#include "fontreqsupport.h"

LONG AslUnscaleValue(const struct LayoutData *ld, LONG pixels)
{
    QUAD scale = ld->ld_RenderScale ? ld->ld_RenderScale : 100;
    QUAD value = pixels;
    return value < 0 ? -((-value * 100 + scale / 2) / scale) : (value * 100 + scale / 2) / scale;
}
VOID AslPrepareWindowGeometry(const struct LayoutData *ld, const struct NewWindow *nw,
    struct ScaleWindowGeometry *geometry)
{
    *geometry = (struct ScaleWindowGeometry){
        AslUnscaleValue(ld, nw->LeftEdge), AslUnscaleValue(ld, nw->TopEdge),
        AslUnscaleValue(ld, nw->Width), AslUnscaleValue(ld, nw->Height),
        AslUnscaleValue(ld, ld->ld_MinWidth), AslUnscaleValue(ld, ld->ld_MinHeight), -1, -1};
}
BOOL AslScaleEligible(const struct IntReq *ir)
{
    /* Unknown callbacks may retain/draw native objects or install pixel resources. */
    if (ir->ir_TextAttr || ir->ir_IntuiMsgFunc) return FALSE;
    switch (ir->ir_ReqType)
    {
        case ASL_FileRequest:
            return !((const struct IntFileReq *)ir)->ifr_FilterFunc && !((const struct IntFileReq *)ir)->ifr_HookFunc;
        case ASL_FontRequest:
            return !((const struct IntFontReq *)ir)->ifo_FilterFunc && !((const struct IntFontReq *)ir)->ifo_HookFunc;
        case ASL_ScreenModeRequest:
            return !((const struct IntSMReq *)ir)->ism_FilterFunc && !((const struct IntSMReq *)ir)->ism_CustomSMList;
    }
    return FALSE;
}
const struct ScreenRenderContext *AslPopupContext(const struct LayoutData *ld)
{
    const struct ScreenRenderContext *ctx = NULL;
    if (!GetAttr(SA_RenderContext, (Object *)ld->ld_Screen, (IPTR *)&ctx) || !ctx ||
        !ctx->src_LogicalDrawInfo || ctx->src_LogicalDrawInfo->dri_Screen != ld->ld_Screen ||
        !ctx->src_LegacyScaleFactor) return NULL;
    /* An unknown client's retained logical resources do not follow font/scale
     * generations. Binding still protects its current integer raster domain. */
    if (ld->ld_ScaleAware && ctx != ld->ld_RenderContext) return NULL;
    return ctx;
}
BOOL AslPrepareContext(struct LayoutData *ld, struct AslBase_intern *AslBase)
{
    const struct ScreenRenderContext *ctx = NULL;
    struct TagItem vi[] = {{GTVI_RenderContext, 0}, {GTVI_Logical, !ld->ld_ScaleAware}, {TAG_DONE, 0}};
    APTR visual;
    if (!GetAttr(SA_RenderContext, (Object *)ld->ld_Screen, (IPTR *)&ctx) || !ctx ||
        !ctx->src_LogicalDrawInfo || !ctx->src_PhysicalDrawInfo ||
        ctx->src_LogicalDrawInfo->dri_Screen != ld->ld_Screen ||
        ctx->src_PhysicalDrawInfo->dri_Screen != ld->ld_Screen ||
        ctx->src_Scale < 100 || ctx->src_Scale > 400) return FALSE;
    /* Exact provider capability is captured in the same immutable context.
     * Unknown decorators keep the complete requester in the logical domain. */
    if (ld->ld_ScaleAware && !ctx->src_PreparedScaleWindow)
    {
        if (ld->ld_GadgetsAttached) return FALSE;
        ld->ld_ScaleAware = FALSE;
    }
    vi[0].ti_Data = (IPTR)ctx;
    vi[1].ti_Data = !ld->ld_ScaleAware;
    visual = GetVisualInfoA(ld->ld_Screen, vi);
    if (!visual) return FALSE;
    ld->ld_RenderContext = ctx;
    ld->ld_RenderScale = ld->ld_ScaleAware ? ctx->src_Scale : 100;
    ld->ld_Dri = ld->ld_ScaleAware ? ctx->src_PhysicalDrawInfo : ctx->src_LogicalDrawInfo;
    ld->ld_VisualInfo = visual;
    ld->ld_Font = ld->ld_Dri->dri_Font;
    ld->ld_TextAttr = ctx->src_BaseFont;
    ld->ld_TextAttr.tta_YSize = ld->ld_Font->tf_YSize;
    /* The captured font name and tags remain pinned until ScreenClose. */
    ld->ld_WBorLeft = AslScaleValue(ld, ld->ld_Screen->WBorLeft);
    ld->ld_WBorRight = AslScaleValue(ld, ld->ld_Screen->WBorRight);
    ld->ld_WBorTop = AslScaleValue(ld, ld->ld_Screen->WBorTop + 1) + ld->ld_Font->tf_YSize;
    ld->ld_WBorBottom = AslScaleValue(ld, 16);
    if (ld->ld_Window)
    {
        ULONG lock = LockIBase(0);
        LockLayerInfo(&ld->ld_Screen->LayerInfo);
        ld->ld_WBorLeft = ld->ld_Window->BorderLeft;
        ld->ld_WBorTop = ld->ld_Window->BorderTop;
        ld->ld_WBorRight = ld->ld_Window->BorderRight;
        ld->ld_WBorBottom = ld->ld_Window->BorderBottom;
        UnlockLayerInfo(&ld->ld_Screen->LayerInfo);
        UnlockIBase(lock);
    }
    else
    {
        struct TagItem tags[] = {{SYSIA_Which, SIZEIMAGE}, {SYSIA_DrawInfo, (IPTR)ld->ld_Dri},
            {SYSIA_RenderContext, (IPTR)ctx}, {SYSIA_Logical, !ld->ld_ScaleAware}, {TAG_DONE, 0}};
        Object *image = NewObjectA(NULL, SYSICLASS, tags);
        if (!image) { FreeVisualInfo(visual); ld->ld_VisualInfo = NULL; return FALSE; }
        ld->ld_WBorBottom = ((struct Image *)image)->Height;
        DisposeObject(image);
    }
    SetFont(&ld->ld_DummyRP, ld->ld_Font);
    return TRUE;
}

static VOID PutTag(struct TagItem *, ULONG *, Tag, IPTR);
Object *AslLayoutObject(struct LayoutData *ld, Class *cl, CONST_STRPTR name,
    struct TagItem *tags, Object *previous, struct AslBase_intern *AslBase)
{
    struct AslObjectRecord *r = AllocVec(sizeof(*r), MEMF_ANY);
    struct TagItem *flat = NULL, *scan = tags, *ti;
    Object *object = NULL;
    ULONG count = 0, n = 0;
    if (!r) goto failed;
    while (NextTagItem(&scan)) ++count;
    flat = AllocVec((count + 16) * sizeof(*flat), MEMF_ANY | MEMF_CLEAR);
    if (!flat) goto failed;
    scan = tags;
    while ((ti = NextTagItem(&scan))) flat[n++] = *ti;
    if (!ld->ld_GadgetContext)
    {
        ld->ld_GadgetContext = AllocVec(sizeof(*ld), MEMF_ANY);
        if (!ld->ld_GadgetContext) goto failed;
        *ld->ld_GadgetContext = *ld;
    }
    PutTag(flat, &n, GA_UserData, (IPTR)ld->ld_GadgetContext);
    /* Labels change only in this requester Process. Keep their original
     * stable storage for candidate resource measurement; do not copy any live
     * edit/selection/input state while InputTask is still using the objects. */
    if (ld->ld_PrepareOnly && previous && cl == AslBase->aslcycleclass)
        PutTag(flat, &n, ASLCY_Labels,
            (IPTR)((struct AslCycleData *)INST_DATA(cl, previous))->labels);
    if (ld->ld_PrepareOnly && previous && cl == AslBase->asllistviewclass)
        PutTag(flat, &n, ASLLV_Labels,
            (IPTR)((struct AslListViewData *)INST_DATA(cl, previous))->labels);
    if (ld->ld_Rebuilding && previous && !ld->ld_PrepareOnly)
    {
        static const Tag states[] = {STRINGA_TextVal, STRINGA_BufferPos, STRINGA_DispPos,
            ASLLV_Labels, ASLLV_Top, ASLLV_Active, ASLCY_Active, ASLCP_Color, ASLFS_Style};
        ULONG i;
        IPTR value;
        for (i = 0; i < sizeof(states) / sizeof(states[0]); ++i)
            if (GetAttr(states[i], previous, &value))
            {
                if (states[i] == ASLLV_Labels && cl == AslBase->asllistviewclass &&
                    value == (IPTR)&((struct AslListViewData *)INST_DATA(cl, previous))->emptylist) value = 0;
                PutTag(flat, &n, states[i], value);
            }
        if (cl == AslBase->aslfontpreviewclass)
        {
            struct AslFontPreviewData *d = INST_DATA(cl, previous);
            PutTag(flat, &n, ASLFP_Font, (IPTR)d->font);
            PutTag(flat, &n, ASLFP_Style, d->drawstyle);
            PutTag(flat, &n, ASLFP_APen, d->apen);
            PutTag(flat, &n, ASLFP_BPen, d->bpen);
        }
    }
    object = NewObjectA(cl, (STRPTR)name, flat);
    if (!object) goto failed;
    FreeVec(flat);
    r->objectClass = cl;
    r->kind = cl ? ASLOBJ_GADGET : !strcmp((CONST_STRPTR)name, SYSICLASS) ? ASLOBJ_IMAGE : ASLOBJ_MODEL;
    r->object = object; r->next = ld->ld_Objects; ld->ld_Objects = r;
    return object;
failed:
    if (flat) FreeVec(flat);
    if (r) FreeVec(r);
    ld->ld_GenerationFailed = TRUE;
    return NULL;
}
VOID AslForgetObject(struct LayoutData *ld, Object *object, struct AslBase_intern *AslBase)
{
    struct AslObjectRecord **at = &ld->ld_Objects;
    while (*at)
    {
        struct AslObjectRecord *r = *at;
        if (r->object == object) { *at = r->next; FreeVec(r); return; }
        at = &r->next;
    }
}
VOID AslDisposeObjects(struct LayoutData *ld, struct AslBase_intern *AslBase)
{
    while (ld->ld_Objects)
    {
        struct AslObjectRecord *r = ld->ld_Objects;
        ld->ld_Objects = r->next;
        DisposeObject(r->object); FreeVec(r);
    }
    if (ld->ld_GadgetContext) FreeVec(ld->ld_GadgetContext);
    ld->ld_GadgetContext = NULL;
}

static VOID PutTag(struct TagItem *tags, ULONG *count, Tag tag, IPTR value)
{
    ULONG i;
    for (i = 0; i < *count; ++i)
        if (tags[i].ti_Tag == tag)
        {
            ULONG j;
            for (j = i; j + 1 < *count; ++j) tags[j] = tags[j + 1];
            --*count; break;
        }
    tags[*count].ti_Tag = tag; tags[*count].ti_Data = value; ++*count;
    tags[*count].ti_Tag = TAG_DONE;
}

/* Only this requester Process touches this list. Public ports are consumed
 * through GetMsg, which also supplies the required SMP port locking. */
struct IntuiMessage *AslNextEvent(struct LayoutData *ld, struct MsgPort *port,
    struct AslBase_intern *AslBase)
{
    struct Message *message = ld->ld_Events ? (struct Message *)RemHead(ld->ld_Events) : NULL;
    return (struct IntuiMessage *)(message ? message : GetMsg(port));
}
static VOID CollectEvents(struct LayoutData *ld, struct MsgPort *port,
    struct AslBase_intern *AslBase)
{
    struct Message *message;
    if (ld->ld_Events && port)
        while ((message = GetMsg(port))) AddTail(ld->ld_Events, (struct Node *)message);
}
static BOOL CancelEvent(const struct IntuiMessage *message)
{
    return message->Class == IDCMP_CLOSEWINDOW ||
        (message->Class == IDCMP_VANILLAKEY && message->Code == 27) ||
        (message->Class == IDCMP_GADGETUP && message->IAddress &&
         ((const struct Gadget *)message->IAddress)->GadgetID == ID_MAINBUTTON_CANCEL);
}
BOOL AslAbortPending(struct LayoutData *ld, struct AslBase_intern *AslBase)
{
    struct IntuiMessage *message;
    if (!ld->ld_Events || !ld->ld_Window) return FALSE;
    CollectEvents(ld, ld->ld_Window->UserPort, AslBase);
    ForeachNode(ld->ld_Events, message)
        if (message->IDCMPWindow == ld->ld_Window && CancelEvent(message))
            return TRUE;
    return FALSE;
}
VOID AslDiscardEvents(struct LayoutData *ld, struct Window *window,
    struct AslBase_intern *AslBase)
{
    struct IntuiMessage *message, *next;
    if (!ld->ld_Events) return;
    ForeachNodeSafe(ld->ld_Events, message, next)
        if (!window || message->IDCMPWindow == window)
        {
            Remove((struct Node *)message);
            ReplyMsg((struct Message *)message);
        }
}
/* A preview uses its last Process-accepted font, never a live StringInfo
 * buffer which InputTask may currently be editing. Font names/tags stay valid
 * while the old owning font reference remains open through publication. */
static struct TextFont *PreparePreviewFont(const struct LayoutData *live,
    const struct LayoutData *candidate, struct AslBase_intern *AslBase)
{
    struct FOUserData *data = live->ld_UserData;
    struct TextFont *old = data->PreviewFont;
    struct TTextAttr attr = {0};
    LONG size;
    if (!old) return NULL;
    size = AslScaleValue(candidate, data->PreviewSize);
    attr.tta_Name = old->tf_Message.mn_Node.ln_Name;
    attr.tta_YSize = size < 1 ? 1 : size > 65535 ? 65535 : size;
    attr.tta_Style = old->tf_Style;
    attr.tta_Flags = old->tf_Flags;
    if ((attr.tta_Style & FSF_TAGGED) && old->tf_Extension)
        attr.tta_Tags = ((struct TextFontExtension *)old->tf_Extension)->tfe_Tags;
    else attr.tta_Style &= ~FSF_TAGGED;
    return OpenDiskFont((struct TextAttr *)&attr);
}

static BOOL FitPropertyWindow(struct LayoutData *live, struct LayoutData *candidate,
    struct AslBase_intern *AslBase)
{
    struct IntSMReq *request;
    struct Window *window = live->ld_Window2;
    LONG count, width, height;
    if (!window || live->ld_IntReq->ir_ReqType != ASL_ScreenModeRequest) return TRUE;
    request = (struct IntSMReq *)live->ld_IntReq;
    count = (&SREQ_LAST_PROPERTY_ITEM(request) - &SREQ_FIRST_PROPERTY_ITEM(request)) /
        sizeof(STRPTR) + 1;
    width = BiggestTextLength(&SREQ_FIRST_PROPERTY_ITEM(request), count,
        &candidate->ld_DummyRP, AslBase) + window->BorderLeft + window->BorderRight +
        2 * (AslScaleValue(candidate, OUTERSPACINGX) +
             AslScaleValue(candidate, BORDERLVSPACINGX) +
             AslScaleValue(candidate, BORDERLVITEMSPACINGX));
    height = SREQ_MAX_PROPERTIES * (candidate->ld_Font->tf_YSize +
        2 * AslScaleValue(candidate, BORDERLVITEMSPACINGY)) +
        window->BorderTop + window->BorderBottom +
        2 * (AslScaleValue(candidate, OUTERSPACINGY) + AslScaleValue(candidate, BORDERLVSPACINGY));
    return FitWindowLayout(window, candidate->ld_RenderContext, width, height);
}

BOOL AslRelayout(struct LayoutData *ld, struct AslReqInfo *info, struct AslBase_intern *AslBase)
{
    const struct ScreenRenderContext *ctx = NULL, *after = NULL;
    struct LayoutData candidate, retired;
    struct TextFont *newPreview = NULL, *oldPreview = NULL;
    struct AslObjectRecord *record;
    BOOL menuChanged = FALSE, published = FALSE;
    struct AslInstallScale install;
    if (!ld->ld_ScaleAware && ld->ld_GadgetsAttached) return TRUE;
    if (!GetAttr(SA_RenderContext, (Object *)ld->ld_Screen, (IPTR *)&ctx) || !ctx) return FALSE;
    if (ctx == ld->ld_RenderContext && ld->ld_WBorLeft == ld->ld_Window->BorderLeft &&
        ld->ld_WBorTop == ld->ld_Window->BorderTop && ld->ld_WBorRight == ld->ld_Window->BorderRight &&
        ld->ld_WBorBottom == ld->ld_Window->BorderBottom) return TRUE;

    candidate = *ld;
    candidate.ld_UserData = AllocVec(ld->ld_UserDataSize, MEMF_ANY);
    if (!candidate.ld_UserData) return FALSE;
    CopyMem(ld->ld_UserData, candidate.ld_UserData, ld->ld_UserDataSize);
    candidate.ld_Objects = NULL; candidate.ld_GadgetContext = NULL;
    candidate.ld_Rebuilding = TRUE; candidate.ld_PrepareOnly = TRUE;
    candidate.ld_GenerationFailed = FALSE; candidate.ld_VisualInfo = NULL; candidate.ld_Menu = NULL;
    if (!AslPrepareContext(&candidate, AslBase)) goto failed;
    if (ld->ld_FontOwned)
    {
        candidate.ld_Font = ld->ld_Font; candidate.ld_TextAttr = ld->ld_TextAttr;
        SetFont(&candidate.ld_DummyRP, candidate.ld_Font);
    }
    candidate.ld_Window = NULL;
    candidate.ld_Command = LDCMD_INIT;
    if (!CallHookPkt(&info->GadgetryHook, &candidate, AslBase) || candidate.ld_GenerationFailed ||
        (ld->ld_Menu && !candidate.ld_Menu)) goto failed;
    candidate.ld_Window = ld->ld_Window;
    if (ld->ld_IntReq->ir_ReqType == ASL_FontRequest)
    {
        oldPreview = ((struct FOUserData *)ld->ld_UserData)->PreviewFont;
        if (oldPreview)
        {
            struct AslFontPreviewData *preview;
            newPreview = PreparePreviewFont(ld, &candidate, AslBase);
            if (!newPreview) goto failed;
            preview = INST_DATA(AslBase->aslfontpreviewclass, ((struct FOUserData *)candidate.ld_UserData)->Preview);
            preview->font = newPreview;
        }
    }
    /* The measured minimum is generation-local. FitWindowLayout preserves
     * the canonical 106 intent, including the user's last deliberate resize.
     * Every candidate resource has already been allocated before this call. */
    if (!FitWindowLayout(ld->ld_Window, candidate.ld_RenderContext,
            candidate.ld_MinWidth, candidate.ld_MinHeight)) goto failed;
    if (!FitPropertyWindow(ld, &candidate, AslBase)) goto failed;
    /* Candidate layout can allocate/notify its detached internal models; it
     * runs before publication, while the live string remains fully active. */
    for (record = candidate.ld_Objects; record; record = record->next)
        if (record->kind == ASLOBJ_GADGET)
        {
            struct gpLayout layout = {GM_LAYOUT, NULL, TRUE};
            DoGadgetMethodA((struct Gadget *)record->object, ld->ld_Window, NULL, (Msg)&layout);
        }
    candidate.ld_GadgetContext->ld_UserData = ld->ld_UserData;
    if (!GetAttr(SA_RenderContext, (Object *)ld->ld_Screen, (IPTR *)&after) || after != candidate.ld_RenderContext)
        goto failed;
    if (candidate.ld_Menu)
    {
        if (!SetMenuStripContext(ld->ld_Window, candidate.ld_Menu, candidate.ld_RenderContext, !ld->ld_ScaleAware))
            goto failed;
        menuChanged = TRUE;
    }
    install = (struct AslInstallScale){ASLGM_INSTALLSCALE, NULL, ld, &candidate};
    published = DoGadgetMethodA(ld->ld_GList, ld->ld_Window, NULL, (Msg)&install);
    if (!published) goto failed;

    /* Object records remain attached to their stable native identities. Only
     * losing resources and detached candidates belong to the retired view. */
    retired = candidate;
    retired.ld_GadgetContext = ld->ld_GadgetContext;
    retired.ld_VisualInfo = ld->ld_VisualInfo; retired.ld_Menu = ld->ld_Menu;
    ld->ld_GadgetContext = candidate.ld_GadgetContext;
    ld->ld_RenderContext = candidate.ld_RenderContext; ld->ld_RenderScale = candidate.ld_RenderScale;
    ld->ld_Dri = candidate.ld_Dri; ld->ld_Font = candidate.ld_Font;
    ld->ld_TextAttr = candidate.ld_TextAttr; ld->ld_DummyRP = candidate.ld_DummyRP;
    ld->ld_VisualInfo = candidate.ld_VisualInfo; ld->ld_Menu = candidate.ld_Menu;
    ld->ld_WBorLeft = candidate.ld_WBorLeft; ld->ld_WBorTop = candidate.ld_WBorTop;
    ld->ld_WBorRight = candidate.ld_WBorRight; ld->ld_WBorBottom = candidate.ld_WBorBottom;
    ld->ld_MinWidth = candidate.ld_MinWidth; ld->ld_MinHeight = candidate.ld_MinHeight;
    CopyMem(candidate.ld_ColumnWidth, ld->ld_ColumnWidth, sizeof(ld->ld_ColumnWidth));
    CopyMem(candidate.ld_ColumnAlign, ld->ld_ColumnAlign, sizeof(ld->ld_ColumnAlign));
    if (ld->ld_IntReq->ir_ReqType == ASL_FontRequest)
        ((struct FOUserData *)ld->ld_UserData)->PreviewFont = newPreview;
    SetFont(ld->ld_Window->RPort, ld->ld_Font);
    if (ld->ld_GadgetsAttached) RefreshGList(ld->ld_GList, ld->ld_Window, NULL, -1);
    if (ld->ld_Window2 && ld->ld_IntReq->ir_ReqType == ASL_ScreenModeRequest)
    {
        SetFont(ld->ld_Window2->RPort, ld->ld_Font);
        RefreshGList((struct Gadget *)((struct SMUserData *)ld->ld_UserData)->PropertyGadget,
            ld->ld_Window2, NULL, -1);
    }
    AslDisposeObjects(&retired, AslBase);
    if (retired.ld_Menu) FreeMenus(retired.ld_Menu);
    if (retired.ld_VisualInfo) FreeVisualInfo(retired.ld_VisualInfo);
    if (oldPreview) CloseFont(oldPreview);
    FreeVec(candidate.ld_UserData);
    /* This is a Process publication, not the global native scene commit. A
     * newer generation is retried without invalidating any queued message. */
    if (!GetAttr(SA_RenderContext, (Object *)ld->ld_Screen, (IPTR *)&after) || after != ld->ld_RenderContext)
        return FALSE;
    return TRUE;
failed:
    if (menuChanged)
    {
        if (ld->ld_Menu) SetMenuStripContext(ld->ld_Window, ld->ld_Menu, ld->ld_RenderContext, !ld->ld_ScaleAware);
        else ClearMenuStrip(ld->ld_Window);
    }
    AslDisposeObjects(&candidate, AslBase);
    if (newPreview) CloseFont(newPreview);
    if (candidate.ld_Menu) FreeMenus(candidate.ld_Menu);
    if (candidate.ld_VisualInfo) FreeVisualInfo(candidate.ld_VisualInfo);
    FreeVec(candidate.ld_UserData);
    return FALSE;
}
