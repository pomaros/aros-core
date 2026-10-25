/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Distributed under the AROS Public License (APL) Version 1.1. */
#include "intuition_intern.h"
#include "windowframe.h"
#include "intuition_customize.h"
#include "windowsystemimage.h"
#include "scaleresult.h"
#include <intuition/windecorclass.h>
#include <intuition/extensions.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/alib.h>
#include <string.h>

/* Producer-owned immutable-generation lookup; no lock, allocation or methods. */
extern BOOL ScreenRenderContextBorders(const struct ScreenRenderContext *, BOOL,
    LONG *, LONG *, LONG *, LONG *);

struct FrameGadget
{
    struct Gadget desired;
    Object *image;
};
struct WindowFrameGeneration
{
    struct WindowFrameGeneration *next, *birth;
    const struct ScreenRenderContext *context;
    struct IntDrawInfo dri;
    struct FrameGadget gadgets[NUM_SYSGADS];
    LONG width, height, border[4], clientMinimum[4];
    ULONG birthScale;
    ULONG flags, extraButtons, slotMask;
    IPTR userbuffer;
    ULONG bufferSize;
    BOOL initialized, ownsFont;
};
struct PreparedWindowFrame
{
    struct Window *window;
    const struct ScreenRenderContext *context;
    struct WindowFrameGeneration *generation, *previous;
    struct Gadget *gadgets[NUM_SYSGADS];
    struct Gadget old[NUM_SYSGADS];
    LONG border[4], oldborder[4];
    IPTR oldbuffer;
    ULONG oldbufferSize;
    LONG oldSizeWidth, oldSizeHeight;
    struct Image *oldMenuImages[3];
    BOOL owned;
};

struct IntDrawInfo *PreparedWindowFrameDrawInfo(const struct PreparedWindowFrame *frame)
{
    return frame && frame->generation ? &frame->generation->dri : NULL;
}

IPTR PreparedWindowFrameUserBuffer(const struct PreparedWindowFrame *frame)
{
    return frame && frame->generation ? frame->generation->userbuffer : 0;
}

struct IntDrawInfo *WindowFrameDrawInfo(struct Window *window)
{
    return IW(window)->FrameGeneration ? &IW(window)->FrameGeneration->dri : NULL;
}

const struct ScreenRenderContext *WindowFrameContext(struct Window *window)
{
    return IW(window)->FrameGeneration->context;
}

ULONG WindowFrameScale(struct Window *window)
{
    return IW(window)->ScaleAware ? IW(window)->FrameGeneration->context->src_Scale : 100;
}

LONG WindowFrameTitleBorder(struct Window *window)
{
    LONG left, top, right, bottom;
    const struct ScreenRenderContext *context = WindowFrameContext(window);
    if (!ScreenRenderContextBorders(context, TRUE, &left, &top, &right, &bottom))
        return -1;
    return WindowFrameDrawInfo(window)->dri_Font->tf_YSize +
        ((top + 1) * WindowFrameScale(window) + 50) / 100;
}

BOOL WindowFrameBaseBorders(struct Window *window, LONG *left, LONG *top,
    LONG *right, LONG *bottom)
{
    return ScreenRenderContextBorders(IW(window)->FrameGeneration->context,
        !IW(window)->ScaleAware, left, top, right, bottom);
}

BOOL InitializeWindowFrame(struct Window *window, struct Screen *screen,
    const struct ScreenRenderContext *context, struct IntuitionBase *IntuitionBase)
{
    struct WindowFrameGeneration *generation;
    if (!window || !screen || !context || !context->src_LogicalDrawInfo ||
        !context->src_PhysicalDrawInfo ||
        context->src_LogicalDrawInfo->dri_Screen != screen ||
        context->src_PhysicalDrawInfo->dri_Screen != screen) return FALSE;
    generation = AllocMem(sizeof(*generation), MEMF_ANY | MEMF_CLEAR);
    if (!generation) return FALSE;
    generation->context = context;
    generation->birth = generation;
    generation->birthScale = IW(window)->ScaleAware ? context->src_Scale : 100;
    generation->dri = *(struct IntDrawInfo *)(IW(window)->ScaleAware ?
        context->src_PhysicalDrawInfo : context->src_LogicalDrawInfo);
    generation->dri.dri_Font = SafeReopenFont(IntuitionBase,
        &generation->dri.dri_Font);
    if (!generation->dri.dri_Font)
    {
        FreeMem(generation, sizeof(*generation));
        return FALSE;
    }
    generation->ownsFont = TRUE;
    window->WScreen = screen;
    IW(window)->FrameGeneration = IW(window)->FrameGenerations = generation;
    return TRUE;
}

void AdoptWindowFrameBuffer(struct Window *window, BOOL initialized)
{
    struct WindowFrameGeneration *generation = IW(window)->FrameGeneration;
    generation->userbuffer = IW(window)->DecorUserBuffer;
    generation->bufferSize = IW(window)->DecorUserBufferSize;
    generation->initialized = initialized;
}

BOOL PrepareInitialWindowFrameLayout(struct Window *window,
    struct IntuitionBase *IntuitionBase)
{
    struct WindowFrameGeneration *generation = IW(window)->FrameGeneration;
    struct wdpPrepareScaleWindow prepare = {0};
    if (!IW(window)->ScaleAware) return TRUE;
    if (generation->initialized)
    {
        struct wdpExitWindow exit = {WDM_EXITWINDOW,
            !!(generation->dri.dri_Flags & DRIF_DIRECTCOLOR), generation->userbuffer};
        DoMethodA(IS(window->WScreen)->WinDecorObj, (Msg)&exit);
        generation->initialized = FALSE;
    }
    if (generation->userbuffer)
        memset((APTR)generation->userbuffer, 0, generation->bufferSize);
    prepare.base.MethodID = WDM_PREPARESCALEWINDOW;
    prepare.base.wdp_TrueColor = !!(generation->dri.dri_Flags & DRIF_DIRECTCOLOR);
    prepare.base.wdp_UserBuffer = generation->userbuffer;
    prepare.base.wdp_ScreenUserBuffer = IS(window->WScreen)->DecorUserBuffer;
    prepare.base.wdp_Screen = window->WScreen;
    prepare.context = generation->context;
    prepare.font = generation->dri.dri_Font;
    prepare.width = window->Width; prepare.height = window->Height;
    prepare.borderTop = window->BorderTop;
    if (!DoMethodA(IS(window->WScreen)->WinDecorObj, (Msg)&prepare)) return FALSE;
    generation->initialized = TRUE;
    return TRUE;
}

static ULONG frame_flags(struct Window *window)
{
    return window->Flags & (WFLG_SIZEGADGET | WFLG_DRAGBAR |
        WFLG_CLOSEGADGET | WFLG_DEPTHGADGET | WFLG_BORDERLESS |
        WFLG_GIMMEZEROZERO | WFLG_HASZOOM | WFLG_SIZEBRIGHT |
        WFLG_SIZEBBOTTOM | WFLG_TOOLBOX | WFLG_BACKDROP);
}

void RememberWindowFrameBorders(struct Window *window)
{
    struct WindowFrameGeneration *generation = IW(window)->FrameGeneration;
    generation->border[0] = window->BorderLeft;
    generation->border[1] = window->BorderTop;
    generation->border[2] = window->BorderRight;
    generation->border[3] = window->BorderBottom;
}

void CaptureWindowFrameClientBorders(struct Window *window)
{
    struct WindowFrameGeneration *generation = IW(window)->FrameGeneration;
    LONG borders[4] = {window->BorderLeft, window->BorderTop,
        window->BorderRight, window->BorderBottom};
    ULONG i;
    for (i = 0; i < 4; ++i)
        if (borders[i] > generation->border[i])
            generation->clientMinimum[i] = borders[i];
}

void CaptureWindowFrameSystemImages(struct Window *window)
{
    struct WindowFrameGeneration *generation = IW(window)->FrameGeneration;
    ULONG i;
    generation->width = window->Width; generation->height = window->Height;
    generation->flags = frame_flags(window);
    generation->extraButtons = IW(window)->extrabuttons;
    generation->border[0] = window->BorderLeft;
    generation->border[1] = window->BorderTop;
    generation->border[2] = window->BorderRight;
    generation->border[3] = window->BorderBottom;
    for (i = 0; i < NUM_SYSGADS; ++i)
        if (SYSGAD(window, i))
        {
            struct Gadget *gadget = (struct Gadget *)SYSGAD(window, i);
            generation->gadgets[i].desired = *gadget;
            generation->gadgets[i].image = gadget->GadgetRender;
            generation->slotMask |= 1UL << i;
        }
}

BOOL WindowFrameOwnsImage(struct Window *window, const void *image)
{
    struct WindowFrameGeneration *generation;
    ULONG i;
    if (!image) return FALSE;
    for (generation = IW(window)->FrameGenerations; generation; generation = generation->next)
        for (i = 0; i < NUM_SYSGADS; ++i)
            if (generation->gadgets[i].image == image) return TRUE;
    return FALSE;
}

/* Every refusal names its exact check in the caller-owned receipt. */
static void frame_decorator_refusal(struct ScreenScaleResult *result, ULONG method,
    const struct wdpScaleRefusal *refusal)
{
    ScaleResultDecorator(result, method, refusal->wsr_Code, refusal->wsr_Value,
        refusal->wsr_Name, sizeof(refusal->wsr_Name));
}

static const char *system_image_name(ULONG type)
{
    switch (type)
    {
        case SIZEIMAGE: return "size";
        case CLOSEIMAGE: return "close";
        case DEPTHIMAGE: return "depth";
        case ZOOMIMAGE: return "zoom";
        case ICONIFYIMAGE: return "iconify";
        case LOCKIMAGE: return "lock";
        case MUIIMAGE: return "MUI";
        case POPUPIMAGE: return "popup";
        case SNAPSHOTIMAGE: return "snapshot";
        case JUMPIMAGE: return "jump";
        default: return "unknown";
    }
}

struct PreparedWindowFrame *PrepareWindowFrame(struct Window *window,
    const struct ScreenRenderContext *context, struct ScreenScaleResult *result,
    struct IntuitionBase *IntuitionBase)
{
    struct PreparedWindowFrame *frame;
    LONG left, top, right, bottom;
    BOOL logical;
    if (!window || !window->WScreen || !IW(window)->FrameGeneration)
    {
        ScaleResultDetail(result, SSR_DETAIL_FRAME_NO_GENERATION, 0, 0, 0, NULL);
        return NULL;
    }
    if (!context || !context->src_LogicalDrawInfo || !context->src_PhysicalDrawInfo ||
        context->src_LogicalDrawInfo->dri_Screen != window->WScreen ||
        context->src_PhysicalDrawInfo->dri_Screen != window->WScreen)
    {
        ScaleResultDetail(result, SSR_DETAIL_FRAME_CONTEXT, 0, 0, 0, NULL);
        return NULL;
    }
    logical = !IW(window)->ScaleAware;
    frame = AllocMem(sizeof(*frame), MEMF_ANY | MEMF_CLEAR);
    if (!frame) { ScaleResultReason(result, SSR_REASON_NO_MEMORY); return NULL; }
    frame->window = window;
    frame->context = context;
    frame->previous = IW(window)->FrameGeneration;
    frame->oldborder[0] = window->BorderLeft;
    frame->oldborder[1] = window->BorderTop;
    frame->oldborder[2] = window->BorderRight;
    frame->oldborder[3] = window->BorderBottom;
    frame->oldbuffer = IW(window)->DecorUserBuffer;
    frame->oldbufferSize = IW(window)->DecorUserBufferSize;
    frame->oldSizeWidth = IW(window)->sizeimage_width;
    frame->oldSizeHeight = IW(window)->sizeimage_height;
    frame->oldMenuImages[0] = IW(window)->Checkmark;
    frame->oldMenuImages[1] = IW(window)->AmigaKey;
    frame->oldMenuImages[2] = IW(window)->SubMenuImage;
    if (logical)
    {
        /* Legacy canvases keep one coherent100% frame, including its font,
         * theme metrics, private buffer and menu stock images. Fitting may
         * resize the canvas but cannot turn it into a new rendering domain. */
        frame->context = frame->previous->context;
        memcpy(frame->border, frame->oldborder, sizeof(frame->border));
        return frame;
    }
    if (!ScreenRenderContextBorders(context, FALSE, &left, &top, &right, &bottom))
    {
        ScaleResultDetail(result, SSR_DETAIL_FRAME_CONTEXT_BORDERS, 0, 0, 0, NULL);
        goto fail;
    }
    struct TextFont *font = context->src_PhysicalDrawInfo->dri_Font;
    if (!font)
    {
        ScaleResultDetail(result, SSR_DETAIL_FRAME_NO_FONT, 0, 0, 0, NULL);
        goto fail;
    }
    if (window->Flags & WFLG_BORDERLESS) left = top = right = bottom = 0;
    if (window->Title || (window->Flags &
        (WFLG_DRAGBAR | WFLG_CLOSEGADGET | WFLG_DEPTHGADGET)))
    {
        LONG unusedLeft, logicalTop, unusedRight, unusedBottom;
        if (!ScreenRenderContextBorders(context, TRUE, &unusedLeft,
            &logicalTop, &unusedRight, &unusedBottom))
        {
            ScaleResultDetail(result, SSR_DETAIL_FRAME_CONTEXT_BORDERS, 1, 0, 0, NULL);
            goto fail;
        }
        top = font->tf_YSize + ((logicalTop + 1) * context->src_Scale + 50) / 100;
    }
    if (frame->previous)
    {
        const struct WindowFrameGeneration *birth = frame->previous->birth;
        LONG *borders[] = {&left, &top, &right, &bottom};
        ULONG scale = context->src_Scale, i;
        for (i = 0; i < 4; ++i)
        {
            LONG minimum = ((UQUAD)birth->clientMinimum[i] * scale + birth->birthScale / 2) /
                birth->birthScale;
            if (*borders[i] < minimum) *borders[i] = minimum;
        }
    }
    if (window->Flags & (WFLG_SIZEBRIGHT | WFLG_SIZEBBOTTOM))
    {
        ULONG imageWidth = 0, imageHeight = 0;
        struct wdpGetScaleDefSizeSysImage query = {0};
        struct wdpScaleRefusal refusal = {0};
        IPTR status;
        query.base.MethodID = WDM_GETSCALEDEFSIZE_SYSIMAGE;
        query.base.wdp_TrueColor = !!(context->src_PhysicalDrawInfo->dri_Flags & DRIF_DIRECTCOLOR);
        query.base.wdp_Dri = context->src_PhysicalDrawInfo;
        query.base.wdp_ReferenceFont = font;
        query.base.wdp_Which = SIZEIMAGE;
        query.base.wdp_SysiSize = SYSISIZE_MEDRES;
        query.base.wdp_Width = &imageWidth; query.base.wdp_Height = &imageHeight;
        query.context = context; query.logical = FALSE;
        query.refusal = &refusal;
        /* NULL prepared is an explicit allocation-free metrics query. */
        status = DoMethodA(IS(window->WScreen)->WinDecorObj, (Msg)&query);
        if (status == WDP_SCALE_UNAVAILABLE)
            status = WindowScaleVectorSize(&query, IntuitionBase);
        if (status == WDP_SCALE_FAILED && refusal.wsr_Code != WDP_REFUSE_NONE)
        {
            frame_decorator_refusal(result, WDM_GETSCALEDEFSIZE_SYSIMAGE, &refusal);
            goto fail;
        }
        if (status != WDP_SCALE_READY || !imageWidth || !imageHeight ||
            imageWidth > 127 || imageHeight > 127)
        {
            ScaleResultDetail(result, SSR_DETAIL_FRAME_SIZE_IMAGE, status,
                imageWidth, imageHeight, NULL);
            goto fail;
        }
        if ((window->Flags & WFLG_SIZEBRIGHT) && right < (LONG)imageWidth) right = imageWidth;
        if ((window->Flags & WFLG_SIZEBBOTTOM) && bottom < (LONG)imageHeight) bottom = imageHeight;
    }
    {
        LONG proposed[4] = {left, top, right, bottom};
        ULONG side;
        for (side = 0; side < 4; ++side)
            if (proposed[side] < 0 || proposed[side] > 127)
            {
                ScaleResultDetail(result, SSR_DETAIL_FRAME_BORDER_RANGE, side,
                    proposed[side], 0, NULL);
                goto fail;
            }
    }
    frame->border[0] = left; frame->border[1] = top;
    frame->border[2] = right; frame->border[3] = bottom;
    return frame;
fail:
    FreeMem(frame, sizeof(*frame));
    return NULL;
}

BOOL WindowFrameBorders(const struct PreparedWindowFrame *frame,
    LONG *left, LONG *top, LONG *right, LONG *bottom)
{
    if (!frame || !left || !top || !right || !bottom) return FALSE;
    *left = frame->border[0]; *top = frame->border[1];
    *right = frame->border[2]; *bottom = frame->border[3];
    return TRUE;
}

/* DEPTHIMAGE is 0, so "no system image" needs its own value. */
#define NO_SYSTEM_IMAGE (~(ULONG)0)

static ULONG system_image_type(ULONG slot)
{
    switch (slot)
    {
        case SIZEGAD: return SIZEIMAGE;
        case CLOSEGAD: return CLOSEIMAGE;
        case DEPTHGAD: return DEPTHIMAGE;
        case ZOOMGAD: return ZOOMIMAGE;
        case ICONIFYGAD: return ICONIFYIMAGE;
        case LOCKGAD: return LOCKIMAGE;
        case MUIGAD: return MUIIMAGE;
        case POPUPGAD: return POPUPIMAGE;
        case SNAPSHOTGAD: return SNAPSHOTIMAGE;
        case JUMPGAD: return JUMPIMAGE;
        default: return NO_SYSTEM_IMAGE;
    }
}

/* Resolve the canonical aspect before rounding to physical pixels. Deriving
 * width from already rounded physical defaults can make a 15px birth icon
 * become29px at200%. Explicit final extents also pin eager theme resources. */
APTR CreateWindowFrameImage(struct Window *window,
    const struct ScreenRenderContext *context, struct IntDrawInfo *dri,
    ULONG type, LONG preferredHeight, struct ScreenScaleResult *result,
    struct IntuitionBase *IntuitionBase)
{
    ULONG width = 0, height = 0, scale = IW(window)->ScaleAware ? context->src_Scale : 100;
    struct wdpGetScaleDefSizeSysImage query = {0};
    struct wdpScaleRefusal refusal = {0};
    APTR image;
    IPTR status;
    query.base.MethodID = WDM_GETSCALEDEFSIZE_SYSIMAGE;
    query.base.wdp_TrueColor = !!(dri->dri_Flags & DRIF_DIRECTCOLOR);
    query.base.wdp_Dri = context->src_LogicalDrawInfo;
    query.base.wdp_ReferenceFont = context->src_LogicalDrawInfo->dri_Font;
    query.base.wdp_Which = type;
    query.base.wdp_SysiSize = SYSISIZE_MEDRES;
    query.base.wdp_Width = &width; query.base.wdp_Height = &height;
    query.context = context; query.logical = TRUE;
    query.refusal = &refusal;
    status = DoMethodA(IS(window->WScreen)->WinDecorObj, (Msg)&query);
    if (status == WDP_SCALE_UNAVAILABLE)
        status = WindowScaleVectorSize(&query, IntuitionBase);
    if (status == WDP_SCALE_FAILED && refusal.wsr_Code != WDP_REFUSE_NONE)
    {
        frame_decorator_refusal(result, WDM_GETSCALEDEFSIZE_SYSIMAGE, &refusal);
        return NULL;
    }
    if (status != WDP_SCALE_READY || !width || !height || width > 32767 || height > 32767)
    {
        ScaleResultDetail(result, SSR_DETAIL_FRAME_IMAGE_QUERY, status, width, height,
            system_image_name(type));
        return NULL;
    }
    if (preferredHeight < 0 || preferredHeight > 32767)
    {
        ScaleResultDetail(result, SSR_DETAIL_FRAME_IMAGE_EXTENT, width, height,
            preferredHeight, system_image_name(type));
        return NULL;
    }
    if (preferredHeight)
    {
        ULONG canonicalHeight = ((UQUAD)preferredHeight * 100 + scale / 2) / scale;
        if (!canonicalHeight) canonicalHeight = 1;
        width = ((UQUAD)width * canonicalHeight + height / 2) / height;
    }
    width = ((UQUAD)width * scale + 50) / 100;
    height = preferredHeight ? preferredHeight : ((UQUAD)height * scale + 50) / 100;
    if (!width || !height || width > 32767 || height > 32767)
    {
        ScaleResultDetail(result, SSR_DETAIL_FRAME_IMAGE_EXTENT, width, height,
            preferredHeight, system_image_name(type));
        return NULL;
    }
    struct TagItem tags[] = {
        {SYSIA_Which, type}, {SYSIA_DrawInfo, (IPTR)dri},
        {SYSIA_ReferenceFont, (IPTR)dri->dri_Font},
        {SYSIA_RenderContext, (IPTR)context}, {SYSIA_Logical, !IW(window)->ScaleAware},
        {IA_Width, width}, {IA_Height, height}, {TAG_DONE, 0}};
    image = NewObjectA(NULL, SYSICLASS, tags);
    if (!image)
        ScaleResultDetail(result, SSR_DETAIL_FRAME_IMAGE_OBJECT, width, height, 0,
            system_image_name(type));
    return image;
}

BOOL PrepareWindowFrameLayout(struct PreparedWindowFrame *frame,
    LONG width, LONG height, struct ScreenScaleResult *result,
    struct IntuitionBase *IntuitionBase)
{
    struct WindowFrameGeneration *generation;
    struct Window *window;
    struct Window detached;
    struct wdpPrepareScaleWindow prepare = {0};
    struct wdpScaleRefusal refusal = {0};
    IPTR size = 0;
    ULONG i, slots = 0;
    if (!frame || frame->generation || width < 1 ||
        height < 1 || width > 32767 || height > 32767)
    {
        ScaleResultDetail(result, SSR_DETAIL_FRAME_LAYOUT_SIZE, width, height, 0, NULL);
        return FALSE;
    }
    window = frame->window;
    if (!IW(window)->ScaleAware)
    {
        frame->generation = frame->previous;
        return TRUE;
    }
    for (i = 0; i < NUM_SYSGADS; ++i)
    {
        frame->gadgets[i] = (struct Gadget *)SYSGAD(window, i);
        if (frame->gadgets[i])
        {
            frame->old[i] = *frame->gadgets[i];
            slots |= 1UL << i;
        }
    }
    /* Cache identity includes every layout input. Font/theme/scale ownership
     * is carried by exact immutable context identity. Retention is per exact
     * geometry/gadget set as well; it is not globally bounded by13 scales. */
    for (generation = IW(window)->FrameGenerations; generation;
        generation = generation->next)
        if (generation->context == frame->context && generation->width == width &&
            generation->height == height && generation->flags == frame_flags(window) &&
            !memcmp(generation->border, frame->border, sizeof(frame->border)) &&
            generation->extraButtons == IW(window)->extrabuttons &&
            generation->slotMask == slots)
        {
            frame->generation = generation;
            return TRUE;
        }
    generation = AllocMem(sizeof(*generation), MEMF_ANY | MEMF_CLEAR);
    if (!generation) { ScaleResultReason(result, SSR_REASON_NO_MEMORY); return FALSE; }
    frame->generation = generation;
    frame->owned = TRUE;
    generation->context = frame->context;
    generation->birth = frame->previous->birth;
    generation->width = width; generation->height = height;
    generation->flags = frame_flags(window);
    generation->extraButtons = IW(window)->extrabuttons;
    generation->slotMask = slots;
    memcpy(generation->border, frame->border, sizeof(frame->border));
    generation->dri = *(struct IntDrawInfo *)(IW(window)->ScaleAware ?
        frame->context->src_PhysicalDrawInfo : frame->context->src_LogicalDrawInfo);
    if (!IW(window)->ScaleAware && frame->previous)
        generation->dri = frame->previous->dri;
    GetAttr(WDA_UserBuffer, IS(window->WScreen)->WinDecorObj, &size);
    if (size > 0x7fffffff)
    {
        ScaleResultDetail(result, SSR_DETAIL_FRAME_USER_BUFFER, (LONG)size, 0, 0, NULL);
        return FALSE;
    }
    generation->bufferSize = size;
    if (size)
    {
        generation->userbuffer = (IPTR)AllocMem(size, MEMF_ANY | MEMF_CLEAR);
        if (!generation->userbuffer)
        { ScaleResultReason(result, SSR_REASON_NO_MEMORY); return FALSE; }
    }
    prepare.base.MethodID = WDM_PREPARESCALEWINDOW;
    prepare.base.wdp_TrueColor = !!(generation->dri.dri_Flags & DRIF_DIRECTCOLOR);
    prepare.base.wdp_UserBuffer = generation->userbuffer;
    prepare.base.wdp_ScreenUserBuffer = IS(window->WScreen)->DecorUserBuffer;
    prepare.base.wdp_Screen = window->WScreen;
    prepare.context = frame->context;
    prepare.logical = !IW(window)->ScaleAware;
    prepare.font = generation->dri.dri_Font;
    prepare.width = width; prepare.height = height;
    prepare.borderTop = frame->border[1];
    prepare.refusal = &refusal;
    /* Failed typed preparation must dispose its partial private allocations.
     * In particular an unsupported class must not receive an old Exit hook. */
    if (!DoMethodA(IS(window->WScreen)->WinDecorObj, (Msg)&prepare))
    {
        frame_decorator_refusal(result, WDM_PREPARESCALEWINDOW, &refusal);
        return FALSE;
    }
    generation->initialized = TRUE;
    detached = *window;
    detached.Width = width; detached.Height = height;
    detached.BorderLeft = frame->border[0]; detached.BorderTop = frame->border[1];
    detached.BorderRight = frame->border[2]; detached.BorderBottom = frame->border[3];
    for (i = 0; i < NUM_SYSGADS; ++i)
    {
        struct FrameGadget *gadget = &generation->gadgets[i];
        struct wdpLayoutScaleBorderGadgets layout = {0};
        ULONG type;
        if (!frame->gadgets[i]) continue;
        gadget->desired = frame->old[i];
        gadget->desired.NextGadget = NULL;
        type = system_image_type(i);
        if (frame->old[i].GadgetRender && type == NO_SYSTEM_IMAGE)
        {
            ScaleResultDetail(result, SSR_DETAIL_FRAME_UNTYPED_GADGET, i, 0, 0, NULL);
            return FALSE;
        }
        if (type != NO_SYSTEM_IMAGE)
        {
            gadget->image = CreateWindowFrameImage(window, frame->context,
                &generation->dri, type, type == SIZEIMAGE ? 0 : frame->border[1],
                result, IntuitionBase);
            if (!gadget->image) return FALSE;
            gadget->desired.GadgetRender = gadget->image;
            gadget->desired.Width = ((struct Image *)gadget->image)->Width;
            gadget->desired.Height = ((struct Image *)gadget->image)->Height;
        }
        if (i == DRAGBAR)
            gadget->desired.Height = frame->border[1];
        layout.base.MethodID = WDM_LAYOUTSCALEBORDERGADGETS;
        layout.base.wdp_TrueColor = prepare.base.wdp_TrueColor;
        layout.base.wdp_Dri = (struct DrawInfo *)&generation->dri;
        layout.base.wdp_Window = &detached;
        layout.base.wdp_Gadgets = &gadget->desired;
        layout.base.wdp_Flags = WDF_LBG_SYSTEMGADGET;
        layout.base.wdp_ExtraButtons = IW(window)->extrabuttons;
        layout.base.wdp_UserBuffer = generation->userbuffer;
        layout.context = frame->context;
        layout.logical = prepare.logical;
        refusal = (struct wdpScaleRefusal){0};
        layout.refusal = &refusal;
        if (!DoMethodA(IS(window->WScreen)->WinDecorObj, (Msg)&layout))
        {
            frame_decorator_refusal(result, WDM_LAYOUTSCALEBORDERGADGETS, &refusal);
            return FALSE;
        }
    }
    return TRUE;
}

void InstallWindowFrame(struct PreparedWindowFrame *frame, BOOL rollback)
{
    struct Window *window = frame->window;
    if (!IW(window)->ScaleAware) return;
    const LONG *border = rollback ? frame->oldborder : frame->border;
    ULONG i;
    window->BorderLeft = border[0]; window->BorderTop = border[1];
    window->BorderRight = border[2]; window->BorderBottom = border[3];
    IW(window)->FrameGeneration = rollback ? frame->previous : frame->generation;
    IW(window)->DecorUserBuffer = rollback ? frame->oldbuffer : frame->generation->userbuffer;
    IW(window)->DecorUserBufferSize = rollback ? frame->oldbufferSize : frame->generation->bufferSize;
    if (rollback)
    {
        IW(window)->Checkmark = frame->oldMenuImages[0];
        IW(window)->AmigaKey = frame->oldMenuImages[1];
        IW(window)->SubMenuImage = frame->oldMenuImages[2];
    }
    else
    {
        struct IntDrawInfo *dri = &frame->generation->dri;
        if (IW(window)->FrameDefaultMenuImages & 1) IW(window)->Checkmark = dri->dri_CheckMark;
        if (IW(window)->FrameDefaultMenuImages & 2) IW(window)->AmigaKey = dri->dri_AmigaKey;
        if (IW(window)->FrameDefaultMenuImages & 4) IW(window)->SubMenuImage = dri->dri_Customize->submenu;
    }
    if (rollback)
    {
        IW(window)->sizeimage_width = frame->oldSizeWidth;
        IW(window)->sizeimage_height = frame->oldSizeHeight;
    }
    else if (frame->gadgets[SIZEGAD])
    {
        IW(window)->sizeimage_width = frame->generation->gadgets[SIZEGAD].desired.Width;
        IW(window)->sizeimage_height = frame->generation->gadgets[SIZEGAD].desired.Height;
    }
    for (i = 0; i < NUM_SYSGADS; ++i)
    {
        struct Gadget *gadget = frame->gadgets[i];
        const struct Gadget *value = rollback ? &frame->old[i] :
            &frame->generation->gadgets[i].desired;
        if (!gadget) continue;
        gadget->LeftEdge = value->LeftEdge;
        gadget->TopEdge = value->TopEdge;
        gadget->Width = value->Width;
        gadget->Height = value->Height;
        gadget->Flags = rollback ? value->Flags :
            ((frame->old[i].Flags & ~(GFLG_RELRIGHT | GFLG_RELBOTTOM | GFLG_RELWIDTH | GFLG_RELHEIGHT)) |
             (value->Flags & (GFLG_RELRIGHT | GFLG_RELBOTTOM | GFLG_RELWIDTH | GFLG_RELHEIGHT)));
        gadget->GadgetRender = value->GadgetRender;
    }
}

static void free_generation(struct Window *window,
    struct WindowFrameGeneration *generation, struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    ULONG i;
    for (i = 0; i < NUM_SYSGADS; ++i) DisposeObject(generation->gadgets[i].image);
    if (generation->initialized)
    {
        struct wdpExitWindow exit = {WDM_EXITWINDOW,
            !!(generation->dri.dri_Flags & DRIF_DIRECTCOLOR), generation->userbuffer};
        DoMethodA(IS(window->WScreen)->WinDecorObj, (Msg)&exit);
    }
    if (generation->ownsFont && generation->dri.dri_Font)
        CloseFont(generation->dri.dri_Font);
    if (generation->userbuffer)
        FreeMem((APTR)generation->userbuffer, generation->bufferSize);
    FreeMem(generation, sizeof(*generation));
}

void ReleaseWindowFrame(struct PreparedWindowFrame *frame, BOOL committed,
    struct IntuitionBase *IntuitionBase)
{
    if (!frame) return;
    if (frame->owned && frame->generation)
    {
        if (committed)
        {
            frame->generation->next = IW(frame->window)->FrameGenerations;
            IW(frame->window)->FrameGenerations = frame->generation;
        }
        else free_generation(frame->window, frame->generation, IntuitionBase);
    }
    FreeMem(frame, sizeof(*frame));
}

void FreeWindowFrames(struct Window *window, struct IntuitionBase *IntuitionBase)
{
    struct WindowFrameGeneration *generation = IW(window)->FrameGenerations;
    IW(window)->FrameGeneration = IW(window)->FrameGenerations = NULL;
    while (generation)
    {
        struct WindowFrameGeneration *next = generation->next;
        free_generation(window, generation, IntuitionBase);
        generation = next;
    }
}
