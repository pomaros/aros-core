/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include "intuition_intern.h"
#include "intuition_customize.h"
#include "screenrendercontext.h"
#include "windowcomposition.h"
#include <exec/memory.h>
#include <intuition/imageclass.h>
#include <intuition/windecorclass.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <string.h>

struct ScreenRenderGeneration
{
    struct ScreenRenderContext view; /* first: private getters recover owner */
    struct ScreenRenderGeneration *next;
    struct Screen *owner;
    struct IntDrawInfo logical, physical;
    struct IntuitionCustomize logicalcustom, physicalcustom;
    struct ScreenScaleMetrics metrics, logicalmetrics;
    struct TTextAttr physicalattr;
    UWORD pens[NUMDRIPENS];
    Object *screenprovider, *windowprovider, *menuprovider;
    APTR theme;
    ULONG imageflags;
    Object *depthimage;
};
struct PreparedScreenRenderContext
{
    struct IntScreen *screen;
    struct ScreenRenderGeneration *generation, *previous;
    BOOL created, linked, installed;
};

static LONG context_scaled(LONG value, ULONG scale)
{
    return (LONG)(((QUAD)value * scale + 50) / 100);
}
static BOOL context_fontattr(struct TextFont *font, struct TTextAttr *attr,
    struct IntuitionBase *IntuitionBase)
{
    struct Library *UtilityBase = GetPrivIBase(IntuitionBase)->UtilityBase;
    struct TTextAttr source;
    ULONG length;
    ScreenFontTextAttr(font, &source);
    *attr = source;
    attr->tta_Name = NULL;
    attr->tta_Tags = NULL;
    if (!source.tta_Name || strlen(source.tta_Name) > 65535) return FALSE;
    length = strlen(source.tta_Name) + 1;
    attr->tta_Name = AllocVec(length, MEMF_PUBLIC);
    if (!attr->tta_Name) return FALSE;
    memcpy(attr->tta_Name, source.tta_Name, length);
    if (source.tta_Tags)
    {
        attr->tta_Tags = CloneTagItems(source.tta_Tags);
        if (!attr->tta_Tags) return FALSE;
    }
    return TRUE;
}
static void context_freeattr(struct TTextAttr *attr, struct IntuitionBase *IntuitionBase)
{
    struct Library *UtilityBase = GetPrivIBase(IntuitionBase)->UtilityBase;
    FreeTagItems(attr->tta_Tags);
    FreeVec(attr->tta_Name);
}
static void context_dispose(struct ScreenRenderGeneration *g, struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    if (!g) return;
    DisposeObject(g->depthimage);
    DisposeObject((Object *)g->physical.dri_CheckMark);
    DisposeObject((Object *)g->physical.dri_AmigaKey);
    DisposeObject((Object *)g->physicalcustom.submenu);
    DisposeObject((Object *)g->logical.dri_CheckMark);
    DisposeObject((Object *)g->logical.dri_AmigaKey);
    DisposeObject((Object *)g->logicalcustom.submenu);
    context_freeattr(&g->view.src_BaseFont, IntuitionBase);
    context_freeattr(&g->physicalattr, IntuitionBase);
    if (g->physical.dri_Font) CloseFont(g->physical.dri_Font);
    if (g->logical.dri_Font) CloseFont(g->logical.dri_Font);
    FreeVec(g);
}
static BOOL context_logicalmetrics(struct IntScreen *screen, struct TextFont *base,
    ULONG scale, const struct ScreenScaleMetrics *metrics, struct ScreenScaleMetrics *logical,
    struct IntuitionBase *IntuitionBase)
{
    struct sdpPrepareScale message;
    ULONG capability = SDM_QUERYCAPABILITIES;
    if (!(DoMethodA(screen->ScrDecorObj, (Msg)&capability) & SDM_CAP_PREPARESCALE))
    {
        /* Existing third-party decorators can open an ordinary 100% screen.
         * Its completed InitScreen metrics are already in logical units.
         * A later scale request requires explicit preparation support. */
        if (scale != 100 || screen->RenderContext) return FALSE;
        *logical = *metrics;
    }
    else
    {
        *logical = *metrics;
        logical->BarVBorder = 1; logical->BarHBorder = 5;
        logical->MenuVBorder = 2; logical->MenuHBorder = 4;
        logical->BarHeight = base->tf_YSize + logical->WBorTop;
        message.MethodID = SDM_PREPARESCALE;
        message.sdp_Screen = &screen->Screen;
        message.sdp_Font = message.sdp_BaseFont = base;
        message.sdp_Scale = 100;
        message.sdp_UserBuffer = screen->DecorUserBuffer;
        message.sdp_Metrics = logical;
        if (!DoMethodA(screen->ScrDecorObj, (Msg)&message)) return FALSE;
    }
    return
        logical->WBorLeft >= 0 && logical->WBorLeft <= 127 &&
        logical->WBorTop >= 0 && logical->WBorTop <= 127 &&
        logical->WBorRight >= 0 && logical->WBorRight <= 127 &&
        logical->WBorBottom >= 0 && logical->WBorBottom <= 127 &&
        logical->BarHBorder >= 0 && logical->BarHBorder <= 127 &&
        logical->BarVBorder >= 0 && logical->BarVBorder <= 127 &&
        logical->MenuHBorder >= 0 && logical->MenuHBorder <= 127 &&
        logical->MenuVBorder >= 0 && logical->MenuVBorder <= 127;
}
static BOOL context_images(struct ScreenRenderGeneration *g, BOOL logical,
    struct IntuitionBase *IntuitionBase)
{
    struct IntDrawInfo *di = logical ? &g->logical : &g->physical;
    struct IntuitionCustomize *custom = logical ? &g->logicalcustom : &g->physicalcustom;
    struct TagItem tags[] = {
        {SYSIA_Which, MENUCHECK}, {SYSIA_DrawInfo, (IPTR)di},
        {SYSIA_ReferenceFont, (IPTR)di->dri_Font},
        {SYSIA_RenderContext, (IPTR)&g->view}, {SYSIA_Logical, logical}, {TAG_DONE, 0}};
    di->dri_CheckMark = (struct Image *)NewObjectA(NULL, SYSICLASS, tags);
    if (!di->dri_CheckMark) return FALSE;
    tags[0].ti_Data = AMIGAKEY;
    di->dri_AmigaKey = (struct Image *)NewObjectA(NULL, SYSICLASS, tags);
    if (!di->dri_AmigaKey) return FALSE;
    tags[0].ti_Data = SUBMENUIMAGE;
    custom->submenu = (struct Image *)NewObjectA(NULL, SYSICLASS, tags);
    return custom->submenu != NULL;
}
struct PreparedScreenRenderContext *PrepareScreenRenderContext(struct IntScreen *screen,
    struct TextFont *base, struct TextFont *physical, ULONG scale,
    const struct ScreenScaleMetrics *metrics, struct IntuitionBase *IntuitionBase)
{
    struct PreparedScreenRenderContext *p;
    struct ScreenRenderGeneration *g;
    struct ScreenScaleMetrics logical;
    UWORD legacyfactor;
    ULONG imageflags;
    if (!screen || !base || !physical || !metrics || !screen->ScrDecorObj ||
        !screen->WinDecorObj || !screen->MenuDecorObj || scale < 100 || scale > 400 || scale % 25 ||
        !screen->DInfo.dri_Pens || !screen->DInfo.dri_NumPens || screen->DInfo.dri_NumPens > NUMDRIPENS || metrics->BarHeight < 0 || metrics->BarHeight > 127)
        return NULL;
    if (!context_logicalmetrics(screen, base, scale, metrics, &logical, IntuitionBase)) return NULL;
    legacyfactor = ScreenLegacyScaleFactorForScale(&screen->Screen, scale);
    imageflags = screen->Screen.Flags & (SCREENQUIET | SCREENHIRES);
    p = AllocVec(sizeof(*p), MEMF_PUBLIC | MEMF_CLEAR);
    if (!p) return NULL;
    p->screen = screen;
    p->previous = screen->RenderContext;
    for (g = screen->RenderGenerations; g; g = g->next)
        if (g->logical.dri_Font == base && g->physical.dri_Font == physical &&
            g->view.src_Scale == scale && g->theme == screen->Decorator &&
            g->imageflags == imageflags && g->view.src_LegacyScaleFactor == legacyfactor &&
            g->logical.dri_Version == screen->DInfo.dri_Version &&
            g->logical.dri_NumPens == screen->DInfo.dri_NumPens &&
            g->logical.dri_Depth == screen->DInfo.dri_Depth &&
            g->logical.dri_Flags == screen->DInfo.dri_Flags &&
            g->logical.dri_Resolution.X == screen->DInfo.dri_Resolution.X &&
            g->logical.dri_Resolution.Y == screen->DInfo.dri_Resolution.Y &&
            g->screenprovider == screen->ScrDecorObj && g->windowprovider == screen->WinDecorObj &&
            g->menuprovider == screen->MenuDecorObj &&
            !memcmp(&g->metrics, metrics, sizeof(*metrics)) &&
            !memcmp(&g->logicalmetrics, &logical, sizeof(logical)) &&
            !memcmp(g->pens, screen->DInfo.dri_Pens, screen->DInfo.dri_NumPens * sizeof(UWORD)))
        { p->generation = g; return p; }
    g = AllocVec(sizeof(*g), MEMF_PUBLIC | MEMF_CLEAR);
    if (!g) { FreeVec(p); return NULL; }
    p->generation = g; p->created = TRUE;
    g->owner = &screen->Screen;
    g->theme = screen->Decorator;
    g->imageflags = imageflags;
    g->screenprovider = screen->ScrDecorObj;
    g->windowprovider = screen->WinDecorObj;
    {
        IPTR supported = 0;
        g->view.src_PreparedScaleWindow =
            GetAttr(WDA_PreparedScaleWindow, screen->WinDecorObj, &supported) && supported;
    }
    g->menuprovider = screen->MenuDecorObj;
    g->metrics = *metrics; g->logicalmetrics = logical;
    g->logical = g->physical = screen->DInfo;
    g->logical.dri_Font = g->physical.dri_Font = NULL;
    g->logical.dri_CheckMark = g->logical.dri_AmigaKey = NULL;
    g->physical.dri_CheckMark = g->physical.dri_AmigaKey = NULL;
    g->logical.dri_Customize = &g->logicalcustom;
    g->physical.dri_Customize = &g->physicalcustom;
    memcpy(g->pens, screen->DInfo.dri_Pens, screen->DInfo.dri_NumPens * sizeof(UWORD));
    g->logical.dri_Pens = g->physical.dri_Pens = g->pens;
    g->logical.dri_Colors = g->physical.dri_Colors = NULL;
    g->logical.dri_Font = SafeReopenFont(IntuitionBase, &base);
    g->physical.dri_Font = SafeReopenFont(IntuitionBase, &physical);
    /* Cache identities stay pinned; an approximate reopen is a refusal. */
    if (g->logical.dri_Font != base || g->physical.dri_Font != physical) goto fail;
    if (!context_fontattr(base, &g->view.src_BaseFont, IntuitionBase) ||
        !context_fontattr(physical, &g->physicalattr, IntuitionBase)) goto fail;
    g->view.src_Scale = scale;
    g->view.src_LegacyScaleFactor = legacyfactor;
    g->view.src_PhysicalBarHeight = metrics->BarHeight + 1;
    g->view.src_PhysicalBarHBorder = metrics->BarHBorder;
    g->view.src_PhysicalBarVBorder = metrics->BarVBorder;
    g->view.src_PhysicalMenuHBorder = metrics->MenuHBorder;
    g->view.src_PhysicalMenuVBorder = metrics->MenuVBorder;
    g->view.src_LogicalBarHBorder = logical.BarHBorder;
    g->view.src_LogicalBarVBorder = logical.BarVBorder;
    g->view.src_LogicalMenuHBorder = logical.MenuHBorder;
    g->view.src_LogicalMenuVBorder = logical.MenuVBorder;
    g->view.src_LogicalDrawInfo = (struct DrawInfo *)&g->logical;
    g->view.src_PhysicalDrawInfo = (struct DrawInfo *)&g->physical;
    if (!context_images(g, TRUE, IntuitionBase) || !context_images(g, FALSE, IntuitionBase)) goto fail;
    if (!(screen->Screen.Flags & SCREENQUIET))
    {
        struct TagItem tags[] = {{SYSIA_Which, SDEPTHIMAGE},
            {SYSIA_DrawInfo, (IPTR)&g->physical}, {SYSIA_ReferenceFont, (IPTR)physical},
            {SYSIA_RenderContext, (IPTR)&g->view}, {SYSIA_Logical, FALSE},
            {IA_Height, metrics->BarHeight + 1},
            {SYSIA_Size, screen->Screen.Flags & SCREENHIRES ? SYSISIZE_MEDRES : SYSISIZE_LOWRES},
            {TAG_DONE, 0}};
        g->depthimage = NewObjectA(NULL, SYSICLASS, tags);
        if (!g->depthimage) goto fail;
    }
    return p;
fail:
    context_dispose(g, IntuitionBase); FreeVec(p); return NULL;
}
const struct ScreenRenderContext *PreparedScreenRenderContextView(const struct PreparedScreenRenderContext *p)
{ return p ? &p->generation->view : NULL; }
const struct ScreenRenderContext *CurrentScreenRenderContext(struct Screen *screen)
{
    struct ScreenRenderGeneration *g = screen ?
        __atomic_load_n(&IS(screen)->RenderContext, __ATOMIC_ACQUIRE) : NULL;
    return g ? &g->view : NULL;
}
const struct ScreenScaleMetrics *ScreenRenderContextLogicalMetrics(const struct ScreenRenderContext *context)
{ return context ? &((const struct ScreenRenderGeneration *)context)->logicalmetrics : NULL; }
BOOL ScreenRenderContextBorders(const struct ScreenRenderContext *context, BOOL logical,
    LONG *left, LONG *top, LONG *right, LONG *bottom)
{
    const struct ScreenRenderGeneration *g = (const struct ScreenRenderGeneration *)context;
    if (!g || !left || !top || !right || !bottom) return FALSE;
    *left = g->logicalmetrics.WBorLeft; *top = g->logicalmetrics.WBorTop;
    *right = g->logicalmetrics.WBorRight; *bottom = g->logicalmetrics.WBorBottom;
    if (!logical)
    {
        *left = context_scaled(*left, g->view.src_Scale); *top = context_scaled(*top, g->view.src_Scale);
        *right = context_scaled(*right, g->view.src_Scale); *bottom = context_scaled(*bottom, g->view.src_Scale);
    }
    return TRUE;
}
Object *ScreenRenderContextDepthImage(const struct ScreenRenderContext *context)
{ return context ? ((const struct ScreenRenderGeneration *)context)->depthimage : NULL; }
void InstallScreenRenderContext(struct PreparedScreenRenderContext *p, BOOL rollback)
{
    if (!p) return;
    if (rollback)
    {
        __atomic_store_n(&p->screen->RenderContext, p->previous, __ATOMIC_RELEASE);
        if (p->created && p->linked)
        { p->screen->RenderGenerations = p->generation->next; p->linked = FALSE; }
    }
    else
    {
        if (p->created && !p->linked)
        { p->generation->next = p->screen->RenderGenerations; p->screen->RenderGenerations = p->generation; p->linked = TRUE; }
        __atomic_store_n(&p->screen->RenderContext, p->generation, __ATOMIC_RELEASE);
    }
    p->installed = !rollback;
}
void ReleaseScreenRenderContext(struct PreparedScreenRenderContext *p, BOOL committed,
    struct IntuitionBase *IntuitionBase)
{
    if (!p) return;
    if (!committed && p->created && !p->linked) context_dispose(p->generation, IntuitionBase);
    FreeVec(p);
}
BOOL InitializeScreenRenderContext(struct IntScreen *screen, struct IntuitionBase *IntuitionBase)
{
    struct Screen *s = &screen->Screen;
    struct ScreenScaleMetrics metrics = {s->BarHeight, s->BarVBorder, s->BarHBorder,
        s->MenuVBorder, s->MenuHBorder, s->WBorTop, s->WBorLeft, s->WBorRight, s->WBorBottom, -1, 0};
    struct PreparedScreenRenderContext *p = PrepareScreenRenderContext(screen,
        screen->ScaleBaseFont, screen->DInfo.dri_Font, screen->Scale, &metrics, IntuitionBase);
    if (!p) return FALSE;
    InstallScreenRenderContext(p, FALSE);
    ReleaseScreenRenderContext(p, TRUE, IntuitionBase);
    return TRUE;
}
void FreeScreenRenderContexts(struct IntScreen *screen, struct IntuitionBase *IntuitionBase)
{
    struct ScreenRenderGeneration *g = screen->RenderGenerations;
    screen->RenderContext = screen->RenderGenerations = NULL;
    while (g)
    {
        struct ScreenRenderGeneration *next = g->next;
        context_dispose(g, IntuitionBase); g = next;
    }
}
