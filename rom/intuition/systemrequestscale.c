/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Distributed under the AROS Public License, version 1.1. */
#include "intuition_intern.h"
#include "requesters.h"
#include "systemrequestscale.h"
#include "screenrendercontext.h"
#include "windowfit.h"
#include "gadgets.h"
#include <dos/dosextens.h>
#include <intuition/gadgetclass.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <string.h>

#define SYSTEMREQ_MAX_BUTTONS 256
#define SYSTEMREQ_UP ((UWORD)0xfffd)
#define SYSTEMREQ_DOWN ((UWORD)0xfffc)
#define SYSTEMREQ_STROKES 4
struct SystemRequesterText
{
    struct SystemRequesterText *next;
    STRPTR text;
    LONG x, y;
};
struct SystemRequesterButton
{
    LONG x, y, width, height;
    struct Border normal[SYSTEMREQ_STROKES * 2];
    struct Border selected[SYSTEMREQ_STROKES * 2];
    WORD points[SYSTEMREQ_STROKES * 2][6];
};
struct SystemRequesterGeneration
{
    struct SystemRequesterGeneration *next;
    const struct ScreenRenderContext *context;
    struct Rectangle client, text;
    LONG lineheight, rows, visible;
    ULONG count;
    struct SystemRequesterButton button[1];
};
struct SystemRequesterOwner
{
    struct IntRequestUserData common;
    struct SystemRequesterOwner *next;
    struct IntuitionBase *base;
    struct Window *window;
    struct SystemRequesterText *text;
    STRPTR *labels;
    STRPTR title;
    struct Gadget **gadgets;
    ULONG count;
    LONG scroll;
    BOOL plain;
    struct SystemRequesterGeneration *current, *generations;
};
struct PreparedSystemRequesterScale
{
    struct SystemRequesterOwner *owner;
    struct SystemRequesterGeneration *old, *proposed;
    struct Gadget *first, *tail, *aftertail;
    BOOL created;

};
/* Access only under IBase. No arbitrary Window.UserData is dereferenced. */
static struct SystemRequesterOwner *SystemRequesters;
static struct SystemRequesterOwner *systemreq_owner(struct Window *window,
    struct IntuitionBase *IntuitionBase)
{
    struct SystemRequesterOwner *owner;
    for (owner = SystemRequesters; owner; owner = owner->next)
        if (owner->window == window && owner->base == IntuitionBase) return owner;
    return NULL;
}
static LONG systemreq_point(LONG value, ULONG scale)
{
    QUAD n = (QUAD)value * scale;
    return n < 0 ? -(LONG)((-n + 50) / 100) : (LONG)((n + 50) / 100);
}
static LONG systemreq_width(struct TextFont *font, CONST_STRPTR text, ULONG count,
    struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct RastPort rp;
    InitRastPort(&rp); SetFont(&rp, font);
    return TextLength(&rp, text, count);
}
static LONG systemreq_label_width(struct TextFont *font, CONST_STRPTR text,
    struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct RastPort rp;
    InitRastPort(&rp); SetFont(&rp, font);
    return LabelWidth(&rp, (STRPTR)text, strlen(text), IntuitionBase);
}
/* A whole word is preferred, but a long word still makes forward progress. */
static ULONG systemreq_fragment(struct TextFont *font, CONST_STRPTR text,
    LONG width, struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct RastPort rp;
    struct TextExtent extent;
    ULONG length = 0, count, word = 0, i;
    if (width <= 0 || width > 32767) return 0;
    while (text[length] && text[length] != '\n' && length < 65535) ++length;
    InitRastPort(&rp); SetFont(&rp, font);
    count = TextFit(&rp, text, length, &extent, NULL, 1, width, 32767);
    if (count < length)
    {
        for (i = 0; i < count; ++i) if (text[i] == ' ') word = i + 1;
        if (word) count = word;
    }
    return count;
}
static LONG systemreq_rows(struct SystemRequesterOwner *owner,
    struct SystemRequesterGeneration *generation, struct IntuitionBase *IntuitionBase)
{
    struct SystemRequesterText *line;
    LONG rows = 0, width = generation->text.MaxX - generation->text.MinX + 1;
    struct TextFont *font = generation->context->src_PhysicalDrawInfo->dri_Font;
    for (line = owner->text; line; line = line->next)
    {
        CONST_STRPTR text = line->text;
        LONG base = owner->plain ? rows*generation->lineheight :
            systemreq_point(line->y, generation->context->src_Scale);
        LONG count = 0;
        LONG available = width - (owner->plain ? 0 :
            systemreq_point(line->x, generation->context->src_Scale));
        do
        {
            ULONG n = systemreq_fragment(font, text, available, IntuitionBase);
            if (!n && *text && *text != '\n') return -1;
            ++count; text += n;
            if (*text == '\n') ++text;
        } while (*text);
        LONG extent = (base+count*generation->lineheight+generation->lineheight-1)/generation->lineheight;
        if (extent > rows) rows = extent;
    }
    return rows;
}
static void systemreq_border(struct SystemRequesterButton *button, ULONG scale,
    const struct DrawInfo *dri)
{
    ULONG stroke = systemreq_point(1, scale), i;
    if (stroke > SYSTEMREQ_STROKES) stroke = SYSTEMREQ_STROKES;
    for (i = 0; i < stroke * 2; ++i)
    {
        LONG inset = i / 2;
        WORD *p = button->points[i];
        struct Border *a = &button->normal[i], *b = &button->selected[i];
        if (!(i & 1))
        { p[0]=inset; p[1]=button->height-1-inset; p[2]=inset; p[3]=inset;
          p[4]=button->width-1-inset; p[5]=inset; }
        else
        { p[0]=button->width-1-inset; p[1]=inset; p[2]=p[0];
          p[3]=button->height-1-inset; p[4]=inset; p[5]=p[3]; }
        a->FrontPen = dri->dri_Pens[(i & 1) ? SHADOWPEN : SHINEPEN];
        a->DrawMode = JAM1; a->Count = 3; a->XY = p;
        a->NextBorder = i+1 < stroke*2 ? &button->normal[i+1] : NULL;
        *b = *a; b->FrontPen = dri->dri_Pens[(i & 1) ? SHINEPEN : SHADOWPEN];
        b->NextBorder = i+1 < stroke*2 ? &button->selected[i+1] : NULL;
    }
}
static struct SystemRequesterGeneration *systemreq_generation(
    struct SystemRequesterOwner *owner, const struct ScreenRenderContext *context,
    const struct WindowFitGeometry *desired, LONG left, LONG top, LONG right, LONG bottom,
    struct IntuitionBase *IntuitionBase)
{
    struct SystemRequesterGeneration *generation;
    struct TextFont *font;
    LONG innerwidth, innerheight, pad, gap, buttonwidth = 0, buttonheight;
    LONG columns, rows, i, textwidth, controls;
    ULONG scale;
    if (!context || !context->src_PhysicalDrawInfo ||
        context->src_PhysicalDrawInfo->dri_Screen != owner->window->WScreen ||
        !(font = context->src_PhysicalDrawInfo->dri_Font) ||
        context->src_Scale < 100 || context->src_Scale > 400 || context->src_Scale % 25)
        return NULL;
    scale = context->src_Scale;
    pad = systemreq_point(8, scale); gap = systemreq_point(8, scale);
    innerwidth = desired->width - left - right;
    innerheight = desired->height - top - bottom;
    buttonheight = font->tf_YSize + systemreq_point(8, scale);
    for (i = 0; i < (LONG)owner->count; ++i)
    {
        LONG w = systemreq_label_width(font, owner->labels[i], IntuitionBase);
        if (w > buttonwidth) buttonwidth = w;
    }
    buttonwidth += systemreq_point(16, scale);
    if (innerwidth < buttonwidth + pad*2 || innerheight <= 0) return NULL;
    columns = (innerwidth - pad*2 + gap) / (buttonwidth + gap);
    if (columns > (LONG)owner->count) columns = owner->count;
    if (!columns) return NULL;
    rows = (owner->count + columns - 1) / columns;
    controls = rows * (buttonheight + gap) - gap;
    textwidth = innerwidth - pad*2 - systemreq_point(32, scale);
    if (textwidth <= 0 || innerheight - controls - pad*3 < font->tf_YSize) return NULL;
    generation = AllocVec(sizeof(*generation) +
        (owner->count + 1) * sizeof(generation->button[0]), MEMF_ANY | MEMF_CLEAR);
    if (!generation) return NULL;
    generation->context = context; generation->count = owner->count + 2;
    generation->client = (struct Rectangle){left, top, desired->width-right-1, desired->height-bottom-1};
    generation->text = (struct Rectangle){left+pad, top+pad,
        left+pad+textwidth-1, desired->height-bottom-controls-pad*2-1};
    generation->lineheight = font->tf_YSize + systemreq_point(2, scale);
    generation->visible = (generation->text.MaxY-generation->text.MinY+1) / generation->lineheight;
    if (!generation->visible) { FreeVec(generation); return NULL; }
    generation->rows = systemreq_rows(owner, generation, IntuitionBase);
    if (generation->rows < 0) { FreeVec(generation); return NULL; }
    for (i = 0; i < (LONG)owner->count; ++i)
    {
        struct SystemRequesterButton *button = &generation->button[i];
        LONG row = i / columns, col = i % columns;
        LONG inrow = owner->count - row*columns;
        if (inrow > columns) inrow = columns;
        button->x = left + (innerwidth - inrow*buttonwidth - (inrow-1)*gap)/2 + col*(buttonwidth+gap);
        button->y = desired->height - bottom - pad - controls + row*(buttonheight+gap);
        button->width = buttonwidth; button->height = buttonheight;
        systemreq_border(button, scale, context->src_PhysicalDrawInfo);
    }
    for (i = 0; i < 2; ++i)
    {
        struct SystemRequesterButton *button = &generation->button[owner->count+i];
        button->x = generation->text.MaxX + systemreq_point(4,scale);
        button->y = i ? generation->text.MaxY-buttonheight+1 : generation->text.MinY;
        if (generation->rows > generation->visible)
        {
            button->width = systemreq_point(24,scale); button->height = buttonheight;
            if (generation->text.MaxY-generation->text.MinY+1 < buttonheight*2+gap)
            { FreeVec(generation); return NULL; }
            systemreq_border(button, scale, context->src_PhysicalDrawInfo);
        }
    }
    return generation;
}
BOOL PrepareSystemRequesterScale(struct Window *window, const struct ScreenRenderContext *context,
    const struct WindowFitGeometry *desired, LONG left, LONG top, LONG right, LONG bottom,
    struct PreparedSystemRequesterScale **result, struct IntuitionBase *IntuitionBase)
{
    struct SystemRequesterOwner *owner = systemreq_owner(window, IntuitionBase);
    struct PreparedSystemRequesterScale *prepared;
    struct Gadget *gadget, *tail = NULL;
    *result = NULL;
    if (!owner) return TRUE;
    if (!context || !desired) return FALSE;
    prepared = AllocVec(sizeof(*prepared), MEMF_ANY | MEMF_CLEAR);
    if (!prepared) return FALSE;
    prepared->owner = owner; prepared->old = owner->current;
    prepared->first = window->FirstGadget;
    for (gadget = window->FirstGadget; gadget && gadget != owner->gadgets[0]; gadget = gadget->NextGadget)
        tail = gadget;
    if (!gadget) { FreeVec(prepared); return FALSE; }
    prepared->tail = tail; prepared->aftertail = tail ? tail->NextGadget : window->FirstGadget;
    /* The immutable source and exact accepted client rectangle fully identify
     * a layout. Reuse it on repeated scale/reset cycles; keep borrowed render
     * generations until WindowClose without allocating one on every revisit. */
    for (prepared->proposed = owner->generations; prepared->proposed;
         prepared->proposed = prepared->proposed->next)
    {
        const struct Rectangle *box = &prepared->proposed->client;
        if (prepared->proposed->context == context && box->MinX == left && box->MinY == top &&
            box->MaxX == desired->width-right-1 && box->MaxY == desired->height-bottom-1)
            break;
    }
    if (!prepared->proposed)
    {
        prepared->proposed = systemreq_generation(owner, context, desired, left, top, right, bottom, IntuitionBase);
        prepared->created = TRUE;
    }
    if (!prepared->proposed) { FreeVec(prepared); return FALSE; }
    *result = prepared;
    return TRUE;
}
static void systemreq_install(struct SystemRequesterOwner *owner,
    struct SystemRequesterGeneration *generation)
{
    ULONG i;
    for (i = 0; i < generation->count; ++i)
    {
        struct ExtGadget *gadget = (struct ExtGadget *)owner->gadgets[i];
        struct SystemRequesterButton *button = &generation->button[i];
        gadget->LeftEdge = gadget->BoundsLeftEdge = button->x;
        gadget->TopEdge = gadget->BoundsTopEdge = button->y;
        gadget->Width = gadget->BoundsWidth = button->width;
        gadget->Height = gadget->BoundsHeight = button->height;
        gadget->GadgetRender = button->normal;
        gadget->SelectRender = button->selected;
    }
    owner->current = generation;
}
void InstallSystemRequesterScale(struct PreparedSystemRequesterScale *prepared, BOOL rollback)
{
    if (!prepared) return;
    systemreq_install(prepared->owner, rollback ? prepared->old : prepared->proposed);
    /* Frame snapshots include NextGadget: assert the unchanged user chain at
       the precise boundary following frame install/rollback. */
    if (prepared->tail) prepared->tail->NextGadget = prepared->aftertail;
    prepared->owner->window->FirstGadget = prepared->first;

}
void FinishSystemRequesterScale(struct PreparedSystemRequesterScale *prepared, BOOL committed,
    struct IntuitionBase *IntuitionBase)
{
    if (!prepared) return;
    if (committed && prepared->created)
    {
        prepared->proposed->next = prepared->owner->generations;
        prepared->owner->generations = prepared->proposed;
    }
    else if (!committed && prepared->created) FreeVec(prepared->proposed);
    FreeVec(prepared);
}
BOOL SystemRequesterScaleBounds(const struct PreparedSystemRequesterScale *prepared,
    struct Rectangle *bounds)
{
    if (!prepared || !bounds) return FALSE;
    *bounds = prepared->proposed->client; return TRUE;
}
static BOOL systemreq_paint(struct SystemRequesterOwner *owner,
    struct SystemRequesterGeneration *generation, struct RastPort *rp,
    struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct DrawInfo *dri = generation->context->src_PhysicalDrawInfo;
    struct TextFont *font = dri->dri_Font;
    struct SystemRequesterText *line;
    LONG scroll = owner->scroll, row = 0;
    LONG maximum = generation->rows - generation->visible;
    ULONG i;
    if (maximum < 0) maximum = 0;
    if (scroll > maximum) scroll = maximum;
    if (scroll < 0) scroll = 0;
    SetFont(rp, font);
    SetABPenDrMd(rp, dri->dri_Pens[BACKGROUNDPEN], 0, JAM1);
    RectFill(rp, generation->client.MinX, generation->client.MinY,
        generation->client.MaxX, generation->client.MaxY);
    SetAPen(rp, dri->dri_Pens[TEXTPEN]);
    for (line = owner->text; line; line = line->next)
    {
        CONST_STRPTR text = line->text;
        LONG x = generation->text.MinX;
        if (!owner->plain)
        {
            x += systemreq_point(line->x, generation->context->src_Scale);
            row = 0;
        }
        do
        {
            ULONG n = systemreq_fragment(font, text, generation->text.MaxX-x+1, IntuitionBase);
            if (!n && *text && *text != '\n') return FALSE;
            LONG y = (row-scroll)*generation->lineheight +
                (owner->plain ? 0 : systemreq_point(line->y,generation->context->src_Scale));
            if (y >= 0 && y+font->tf_YSize <= generation->text.MaxY-generation->text.MinY+1)
            {
                Move(rp, x, generation->text.MinY + y + font->tf_Baseline);
                Text(rp, text, n);
            }
            ++row; text += n;
            if (*text == '\n') ++text;
        } while (*text);
    }
    for (i = 0; i < generation->count; ++i)
    {
        struct SystemRequesterButton *button = &generation->button[i];
        CONST_STRPTR label = i < owner->count ? owner->labels[i] : (CONST_STRPTR)
            (i == owner->count ? "^" : "v");
        LONG width;
        if (!button->width || !button->height) continue;
        DrawBorder(rp, (owner->gadgets[i]->Flags & GFLG_SELECTED) ?
            button->selected : button->normal, button->x, button->y);
        width = systemreq_label_width(font, label, IntuitionBase);
        SetAPen(rp, dri->dri_Pens[TEXTPEN]);
        Move(rp, button->x+(button->width-width)/2,
            button->y+(button->height-font->tf_YSize)/2+font->tf_Baseline);
        RenderLabel(rp, (STRPTR)label, strlen(label), IntuitionBase);
    }
    return TRUE;
}
BOOL PaintSystemRequesterScale(struct PreparedSystemRequesterScale *prepared,
    struct RastPort *rp, struct IntuitionBase *IntuitionBase)
{
    return prepared && rp && systemreq_paint(prepared->owner, prepared->proposed, rp, IntuitionBase);
}
static STRPTR systemreq_copy(CONST_STRPTR text)
{
    IPTR length;
    STRPTR copy;
    if (!text || (length = strlen(text)) > 65535) return NULL;
    copy = AllocVec(length + 1, MEMF_ANY);
    if (copy) CopyMem(text, copy, length + 1);
    return copy;
}
static void systemreq_free_owner(struct SystemRequesterOwner *owner,
    struct IntuitionBase *IntuitionBase)
{
    ULONG i;
    struct SystemRequesterGeneration *generation;
    struct SystemRequesterText *line;
    if (owner->gadgets)
        for (i = 0; i < owner->count + 2; ++i)
            if (owner->gadgets[i]) DisposeObject((Object *)owner->gadgets[i]);
    FreeVec(owner->gadgets);
    if (owner->labels)
        for (i = 0; i < owner->count; ++i) FreeVec(owner->labels[i]);
    FreeVec(owner->labels);
    FreeVec(owner->title);
    while ((line = owner->text))
    { owner->text = line->next; FreeVec(line->text); FreeVec(line); }
    while ((generation = owner->generations))
    { owner->generations = generation->next; FreeVec(generation); }
    FreeVec(owner);
}
static BOOL systemreq_copy_text(struct SystemRequesterOwner *owner,
    CONST_STRPTR plain, const struct IntuiText *body)
{
    struct SystemRequesterText **next = &owner->text;
    ULONG lines = 0;
    do
    {
        struct SystemRequesterText *line;
        if (++lines > 4096) return FALSE;
        line = AllocVec(sizeof(*line), MEMF_ANY | MEMF_CLEAR);
        if (!line) return FALSE;
        *next = line; next = &line->next;
        line->text = systemreq_copy(plain ? plain : body->IText);
        if (!line->text) return FALSE;
        if (plain) break;
        line->x = body->LeftEdge; line->y = body->TopEdge;
        body = body->NextText;
    } while (body);
    return TRUE;
}
/* Process-only initial construction. Custom font contracts stay in the
 * ordinary logical requester path; they are never silently declared aware. */
struct Window *OpenScaledSystemRequester(struct Screen *screen, CONST_STRPTR title,
    CONST_STRPTR plain, const struct IntuiText *body, STRPTR *labels, ULONG count,
    ULONG idcmp, LONG minimumwidth, LONG minimumheight, BOOL *handled,
    struct IntuitionBase *IntuitionBase)
{
    struct SystemRequesterOwner *owner;
    const struct IntuiText *line;
    struct Task *task = FindTask(NULL);
    const struct ScreenRenderContext *context, *observed = NULL;
    LONG observedBorders[4];
    struct WindowFitGeometry geometry;
    struct SystemRequesterGeneration *generation;
    struct Window *window;
    IPTR value = 0;
    ULONG i, attempt;
    LONG left, top, right, bottom, width = 0, height = 0;
    struct TextFont *font;
    *handled = FALSE;
    if (!task || task->tc_Node.ln_Type != NT_PROCESS || !screen ||
        GetPrivIBase(IntuitionBase)->ReqFont || !count || count > SYSTEMREQ_MAX_BUTTONS)
        return NULL;
    for (line = body; line; line = line->NextText)
        if (line->ITextFont || line->LeftEdge < 0 || line->TopEdge < 0 ||
            line->FrontPen != 1 || line->BackPen != 0 ||
            (line->DrawMode != JAM1 && line->DrawMode != JAM2)) return NULL;
    /* Unsupported decorators retain the established logical requester path.
     * Once admitted, allocation and stale-context failures remain failures. */
    if (!GetAttr(WDA_PreparedScaleWindow, IS(screen)->WinDecorObj, &value) || !value)
        return NULL;
    *handled = TRUE;
    owner = AllocVec(sizeof(*owner), MEMF_ANY | MEMF_CLEAR);
    if (!owner) return NULL;
    owner->base = IntuitionBase; owner->count = count; owner->plain = plain != NULL;
    owner->title = systemreq_copy(title ? title : (CONST_STRPTR)"");
    owner->common.IDCMP = idcmp; owner->common.ReqDims.gadgets = count;
    owner->labels = AllocVec(count * sizeof(*owner->labels), MEMF_ANY | MEMF_CLEAR);
    owner->gadgets = AllocVec((count + 2) * sizeof(*owner->gadgets), MEMF_ANY | MEMF_CLEAR);
    if (!owner->title || !owner->labels || !owner->gadgets ||
        !systemreq_copy_text(owner, plain, body)) goto failure;
    for (i = 0; i < count; ++i)
        if (!(owner->labels[i] = systemreq_copy(labels[i]))) goto failure;
    for (attempt = 0; attempt < 6; ++attempt)
    {
        LONG pad, maxlabel = 0, lines = 1;
        width = height = 0;
        if (!GetAttr(SA_RenderContext, (Object *)screen, &value) || !value) goto failure;
        context = (const struct ScreenRenderContext *)value;
        if (!ScreenRenderContextBorders(context, FALSE, &left, &top, &right, &bottom)) goto failure;
        if (observed == context)
        {
            left=observedBorders[0]; top=observedBorders[1];
            right=observedBorders[2]; bottom=observedBorders[3];
        }
        font = context->src_PhysicalDrawInfo->dri_Font;
        pad = systemreq_point(8, context->src_Scale);
        for (i = 0; i < count; ++i)
        {
            LONG w = systemreq_label_width(font, owner->labels[i], IntuitionBase);
            if (w > maxlabel) maxlabel = w;
        }
        for (struct SystemRequesterText *text = owner->text; text; text = text->next)
        {
            CONST_STRPTR start = text->text, end = start;
            do
            {
                while (*end && *end != '\n') ++end;
                LONG w = systemreq_width(font, start, end-start, IntuitionBase) +
                    systemreq_point(text->x, context->src_Scale);
                if (w > width) width = w;
                if (*end) { ++lines; start = ++end; } else break;
            } while (*start);
            if (!owner->plain)
            {
                LONG h = systemreq_point(text->y, context->src_Scale) + font->tf_YSize;
                if (h > height) height = h;
            }
        }
        if (owner->plain) height = lines * (font->tf_YSize + systemreq_point(2, context->src_Scale));
        LONG labelwidth = count * (maxlabel + pad*3) - pad;
        if (labelwidth > width) width = labelwidth;
        width += left + right + pad*2 + systemreq_point(32,context->src_Scale);
        height += top + bottom + pad*4 + font->tf_YSize;
        LONG minimum = systemreq_point(minimumwidth, context->src_Scale);
        if (width < minimum) width = minimum;
        minimum = systemreq_point(minimumheight, context->src_Scale);
        if (height < minimum) height = minimum;
        if (width > screen->Width) width = screen->Width;
        LONG available = width-left-right-pad*2;
        LONG columns = (available+pad)/(maxlabel+pad*3);
        if (columns < 1) goto failure;
        if (columns > (LONG)count) columns = count;
        LONG buttonrows = (count+columns-1)/columns;
        LONG wrapwidth = available-systemreq_point(32,context->src_Scale);
        struct SystemRequesterGeneration measure = {
            .context=context, .text={0,0,wrapwidth-1,0},
            .lineheight=font->tf_YSize+systemreq_point(2,context->src_Scale)
        };
        LONG textrows = systemreq_rows(owner, &measure, IntuitionBase);
        if (textrows < 0) goto failure;
        LONG needed = top+bottom+pad*3+textrows*measure.lineheight+
            buttonrows*(font->tf_YSize+pad*2)-pad;
        if (height < needed) height = needed;
        if (height > screen->Height) height = screen->Height;
        struct ScaleWindowGeometry points = {
            (screen->Width-width)*50/context->src_Scale,
            (screen->Height-height)*50/context->src_Scale,
            width*100/context->src_Scale, height*100/context->src_Scale,
            1,1,0,0
        };
        struct TagItem tags[] = {
            {WA_CustomScreen,(IPTR)screen},{WA_Title,(IPTR)owner->title},
            {WA_Width,width},{WA_Height,height},
            {WA_Left,(screen->Width-width)/2},{WA_Top,(screen->Height-height)/2},
            {WA_DragBar,TRUE},{WA_DepthGadget,TRUE},{WA_RMBTrap,TRUE},
            {WA_Activate,FALSE},{WA_Hidden,TRUE},{WA_ScaleAware,TRUE},{WA_AutoAdjust,TRUE},
            {WA_RenderContext,(IPTR)context},{WA_ScaleGeometry,(IPTR)&points},
            {WA_IDCMP,IDCMP_GADGETUP|IDCMP_RAWKEY|IDCMP_CHANGESCALE|IDCMP_REFRESHWINDOW|(idcmp&~IDCMP_VANILLAKEY)},
            {TAG_DONE,0}
        };
        window = OpenWindowTagList(NULL, tags);
        if (!window) continue;
        /* Screen border padding is not the full native title/frame extent.
         * Probe the actual hidden constructor and retry using its accepted
         * metrics from this exact immutable context, never guess/double-scale. */
        if (window->BorderLeft != left || window->BorderTop != top ||
            window->BorderRight != right || window->BorderBottom != bottom)
        {
            observed = context;
            observedBorders[0]=window->BorderLeft; observedBorders[1]=window->BorderTop;
            observedBorders[2]=window->BorderRight; observedBorders[3]=window->BorderBottom;
            CloseWindow(window);
            continue;
        }
        owner->window = window;
        geometry = (struct WindowFitGeometry){.width=window->Width,.height=window->Height};
        generation = systemreq_generation(owner, context, &geometry,
            window->BorderLeft, window->BorderTop, window->BorderRight, window->BorderBottom, IntuitionBase);
        if (!generation) { CloseWindow(window); owner->window=NULL; goto failure; }
        generation->next = owner->generations; owner->generations = generation;
        for (i = 0; i < count + 2; ++i)
        {
            CONST_STRPTR label = i < count ? owner->labels[i] : (CONST_STRPTR) (i == count ? "^" : "v");
            struct TagItem gadgettags[] = {
                {GA_ID,i<count ? (i==count-1 ? 0 : i+1) : (i==count ? SYSTEMREQ_UP : SYSTEMREQ_DOWN)},
                {GA_Text,(IPTR)label},{GA_RelVerify,TRUE},{TAG_DONE,0}
            };
            owner->gadgets[i] = (struct Gadget *)NewObjectA(NULL, FRBUTTONCLASS, gadgettags);
            if (!owner->gadgets[i]) { CloseWindow(window); owner->window=NULL; goto failure; }
            owner->gadgets[i]->Flags &= ~GFLG_GADGIMAGE;
            owner->gadgets[i]->Flags |= GFLG_GADGHIMAGE;
            if (i) owner->gadgets[i-1]->NextGadget = owner->gadgets[i];
        }
        systemreq_install(owner, generation);
        owner->common.Gadgets = owner->gadgets[0];
        window->UserData = (BYTE *)&owner->common;
        if (AddGList(window, owner->gadgets[0], (UWORD)-1, count+2, NULL) == (UWORD)-1)
        { CloseWindow(window); owner->window=NULL; goto failure; }
        ULONG lock = LockIBase(0);
        owner->next = SystemRequesters; SystemRequesters = owner;
        systemreq_paint(owner, generation, window->RPort, IntuitionBase);
        UnlockIBase(lock);
        if (ShowWindow(window, NULL)) { ActivateWindow(window); return window; }
        RemoveGList(window, owner->gadgets[0], count+2);
        CloseWindow(window); owner->window=NULL;
        for (i=0;i<count+2;++i) { DisposeObject((Object *)owner->gadgets[i]); owner->gadgets[i]=NULL; }
    }
failure:
    systemreq_free_owner(owner, IntuitionBase);
    return NULL;
}
/* actionCloseWindow calls this under IBase before removing/freeing Window.
 * The requesting Process still owns the detached object until Close returns. */
void DetachSystemRequesterScale(struct Window *window, struct IntuitionBase *IntuitionBase)
{
    struct SystemRequesterOwner **link = &SystemRequesters, *owner;
    while (*link && ((*link)->window != window || (*link)->base != IntuitionBase))
        link = &(*link)->next;
    owner = *link;
    if (!owner) return;
    *link = owner->next;
    owner->window = NULL;
}
/* Called before ordinary FreeSysRequest accesses UserData. Registry ownership
 * persists through Close's action: Scale never observes an unowned live dialog. */
BOOL FreeScaledSystemRequester(struct Window *window, struct IntuitionBase *IntuitionBase)
{
    ULONG lock = LockIBase(0);
    struct SystemRequesterOwner *owner = systemreq_owner(window, IntuitionBase);
    UnlockIBase(lock);
    if (!owner) return FALSE;
    RemoveGList(window, owner->gadgets[0], owner->count+2);
    window->UserData = NULL;
    CloseWindow(window);
    systemreq_free_owner(owner, IntuitionBase);
    return TRUE;
}
/* Reserved events never become user responses. The public result remains
 * -2 unless an ordinary response/IDCMP event is received by SysReqHandler. */
BOOL HandleScaledSystemRequesterEvent(struct Window *window,
    struct IntuiMessage *message, struct IntuitionBase *IntuitionBase)
{
    ULONG lock = LockIBase(0);
    struct SystemRequesterOwner *owner = systemreq_owner(window, IntuitionBase);
    BOOL handled = FALSE;
    if (owner)
    {
        BOOL draw = FALSE;
        if (message->Class == IDCMP_CHANGESCALE)
        {
            handled = TRUE;
            draw = message->IAddress == window->WScreen;
        }
        else if (message->Class == IDCMP_REFRESHWINDOW) draw = handled = TRUE;
        else if (message->Class == IDCMP_GADGETUP &&
            (message->IAddress == owner->gadgets[owner->count] ||
             message->IAddress == owner->gadgets[owner->count+1]))
        {
            LONG maximum = owner->current->rows-owner->current->visible;
            LONG delta = message->IAddress == owner->gadgets[owner->count] ? -1 : 1;
            if (maximum < 0) maximum = 0;
            owner->scroll += delta;
            if (owner->scroll < 0) owner->scroll = 0;
            if (owner->scroll > maximum) owner->scroll = maximum;
            draw = handled = TRUE;
        }
        if (draw)
        {
            if (message->Class == IDCMP_REFRESHWINDOW) BeginRefresh(window);
            systemreq_paint(owner, owner->current, window->RPort, IntuitionBase);
            if (message->Class == IDCMP_REFRESHWINDOW) EndRefresh(window, TRUE);
        }
        /* Reserved scroll controls are navigation, never answers. Preserve
         * explicitly requested ordinary redraw/scale event returns only. */
        if (draw && (message->Class == IDCMP_REFRESHWINDOW ||
            message->Class == IDCMP_CHANGESCALE) &&
            (owner->common.IDCMP & message->Class)) handled = FALSE;
    }
    UnlockIBase(lock);
    return handled;
}
static LONG systemreq_logical_position(LONG value, ULONG factor)
{
    return value < 0 ? -(LONG)((-(QUAD)value+factor-1)/factor) : value/(LONG)factor;
}
/* Legacy constructors must measure in their actual100% drawing domain.
 * This is a metrics-only Screen view, never passed to public Screen methods.
 * All font/borders/factor values come from one retained immutable snapshot. */
BOOL LogicalSystemRequesterScreen(struct Screen *screen, struct Screen *view,
    const struct ScreenRenderContext **captured, struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    const struct ScreenRenderContext *context;
    const struct ScreenScaleMetrics *metrics;
    IPTR value = 0;
    ULONG factor;
    *captured = NULL;
    if (!GetAttr(SA_RenderContext, (Object *)screen, &value) || !value) return FALSE;
    context = (const struct ScreenRenderContext *)value;
    factor = context->src_LegacyScaleFactor;
    metrics = ScreenRenderContextLogicalMetrics(context);
    if (!metrics || factor < 1 || factor > 4 || !context->src_LogicalDrawInfo ||
        context->src_LogicalDrawInfo->dri_Screen != screen ||
        !context->src_LogicalDrawInfo->dri_Font) return FALSE;
    memset(view, 0, sizeof(*view));
    view->Width = screen->Width / factor; view->Height = screen->Height / factor;
    view->LeftEdge = systemreq_logical_position(screen->LeftEdge, factor);
    view->TopEdge = systemreq_logical_position(screen->TopEdge, factor);
    view->ViewPort.DWidth = view->Width; view->ViewPort.DHeight = view->Height;
    view->WBorLeft = metrics->WBorLeft; view->WBorTop = metrics->WBorTop;
    view->WBorRight = metrics->WBorRight; view->WBorBottom = metrics->WBorBottom;
    InitRastPort(&view->RastPort);
    SetFont(&view->RastPort, context->src_LogicalDrawInfo->dri_Font);
    *captured = context;
    return view->Width > 0 && view->Height > 0;
}
