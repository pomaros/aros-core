/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Author: John KNIPPER <code@jkn.me>
    SPDX-License-Identifier: APL-1.1
*/
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/oop.h>
#include <hidd/gfx.h>
#include <graphics/layertransaction.h>
#include <string.h>
#include "preparedpixels.h"

struct PreparedPixels
{
    struct LayerGeometryRefusal *refusal;
    ULONG kind;
    struct Library *oopBase;
    OOP_Object *object;
    struct HIDD_PixelMap map;
    struct HIDD_CursorState *proposedCursor;
    UBYTE *old, *stage, *oldPresentation, *newPresentation;
    UBYTE *oldCursor, *newCursor;
    ULONG cursorSize;
    BOOL mapped, writable, sealed;
};

/* The record names the refusing step; the batch adds the layer identity. */
static BOOL Refuse(struct LayerGeometryRefusal *r, ULONG kind, ULONG lease,
    LONG v0, LONG v1, LONG v2, LONG v3)
{
    if (!r) return FALSE;
    memset(r, 0, sizeof(*r));
    r->lgr_Check = LGR_PIXELS;
    r->lgr_Pixels = kind;
    r->lgr_Lease = lease;
    r->lgr_Value[0] = v0; r->lgr_Value[1] = v1;
    r->lgr_Value[2] = v2; r->lgr_Value[3] = v3;
    return FALSE;
}
static BOOL RefuseDriver(struct LayerGeometryRefusal *r, ULONG kind, ULONG lease,
    const struct HIDD_PixelMap *m)
{
    ULONG i;
    if (!r) return FALSE;
    Refuse(r, kind, lease, m->refusalValue[0], m->refusalValue[1], 0, 0);
    r->lgr_Driver = m->refusal;
    for (i = 0; m->refusalClass && m->refusalClass[i] && i + 1 < sizeof(r->lgr_Class); ++i)
        r->lgr_Class[i] = m->refusalClass[i];
    r->lgr_Class[i] = 0;
    return FALSE;
}
static BOOL MaskOK(ULONG mask, BOOL optional)
{
    ULONG bits = 0;
    if (!mask) return optional;
    while (!(mask & 1)) mask >>= 1;
    while (mask & 1) { ++bits; mask >>= 1; }
    return !mask && bits <= 16;
}
static BOOL MapOK(const struct HIDD_PixelMap *m)
{
    ULONG all = m->red | m->green | m->blue;
    if (!m->pixels || !m->width || !m->height || m->width > (~(ULONG)0)/4 ||
        m->stride < m->width*4 || m->stride > (~(ULONG)0)/m->height ||
        m->size != m->stride*m->height || !MaskOK(m->red,FALSE) ||
        !MaskOK(m->green,FALSE) || !MaskOK(m->blue,FALSE) ||
        !MaskOK(m->alpha,TRUE) || (m->red&m->green) ||
        (m->red&m->blue) || (m->green&m->blue) || (all&m->alpha)) return FALSE;
    if (m->presentation && (m->presentationStride < m->width*4 ||
        m->presentationStride > (~(ULONG)0)/m->height ||
        m->presentationSize != m->presentationStride*m->height)) return FALSE;
    if (m->cursorBackup && (!m->cursorARGB || !m->cursorW || !m->cursorH ||
        m->cursorX > m->width || m->cursorW > m->width-m->cursorX ||
        m->cursorY > m->height || m->cursorH > m->height-m->cursorY ||
        m->cursorW > (~(ULONG)0)/4 || m->cursorARGBStride < m->cursorW*4 ||
        m->cursorARGBStride > (~(ULONG)0)/m->cursorH ||
        m->cursorW*4 > (~(ULONG)0)/m->cursorH)) return FALSE;
    return TRUE;
}
VOID ReleasePreparedPixels(struct PreparedPixels *p)
{
    struct Library *OOPBase;
    if (!p) return;
    OOPBase = p->oopBase;
    if (p->mapped)
    {
        struct pHidd_BitMap_ReleasePixels msg = {
            OOP_GetMethodID(IID_Hidd_BitMap,moHidd_BitMap_ReleasePixels), &p->map};
        OOP_DoMethod(p->object,(OOP_Msg)&msg);
    }
    FreeVec(p->newCursor); FreeVec(p->oldCursor);
    FreeVec(p->newPresentation); FreeVec(p->oldPresentation);
    FreeVec(p->stage); FreeVec(p->old);
    if (OOPBase) CloseLibrary(OOPBase);
    FreeVec(p);
}
struct PreparedPixels *PreparePixels(struct BitMap *bitmap, BOOL writable,
    struct LayerGeometryRefusal *refusal, ULONG kind)
{
    struct PreparedPixels *p;
    struct Library *OOPBase;
    ULONG y;
    if (!bitmap || !IS_HIDD_BM(bitmap))
    { Refuse(refusal, kind, LGR_LEASE_NOT_HIDD, 0, 0, 0, 0); return NULL; }
    p = AllocVec(sizeof(*p),MEMF_ANY|MEMF_CLEAR);
    if (!p) { Refuse(refusal, kind, LGR_LEASE_MEMORY, sizeof(*p), 0, 0, 0); return NULL; }
    p->writable = writable;
    p->refusal = refusal; p->kind = kind;
    p->oopBase = OOPBase = OpenLibrary("oop.library",0);
    if (!OOPBase) { Refuse(refusal, kind, LGR_LEASE_OOP, 0, 0, 0, 0); goto fail; }
    p->object = HIDD_BM_OBJ(bitmap);
    if (!p->object) { Refuse(refusal, kind, LGR_LEASE_NO_OBJECT, 0, 0, 0, 0); goto fail; }
    {
        struct pHidd_BitMap_PreparePixels msg = {
            OOP_GetMethodID(IID_Hidd_BitMap,moHidd_BitMap_PreparePixels), &p->map};
        if (!OOP_DoMethod(p->object,(OOP_Msg)&msg))
        { RefuseDriver(refusal, kind, LGR_LEASE_DRIVER, &p->map); goto fail; }
    }
    p->mapped = TRUE;
    if (!MapOK(&p->map))
    {
        Refuse(refusal, kind, LGR_LEASE_MAP, p->map.width, p->map.height,
            p->map.stride, p->map.size);
        goto fail;
    }
    p->old = AllocVec(p->map.size,MEMF_ANY);
    if (!p->old) { Refuse(refusal, kind, LGR_LEASE_MEMORY, p->map.size, 0, 0, 0); goto fail; }
    memcpy(p->old,p->map.pixels,p->map.size);
    if (p->map.cursorBackup)
    {
        p->cursorSize = p->map.cursorW*p->map.cursorH*4;
        p->oldCursor = AllocVec(p->cursorSize,MEMF_ANY);
        if (!p->oldCursor) { Refuse(refusal, kind, LGR_LEASE_MEMORY, p->cursorSize, 0, 0, 0); goto fail; }
        memcpy(p->oldCursor,p->map.cursorBackup,p->cursorSize);
    }
    if (!writable) return p;
    p->stage = AllocVec(p->map.size,MEMF_ANY);
    if (!p->stage) { Refuse(refusal, kind, LGR_LEASE_MEMORY, p->map.size, 0, 0, 0); goto fail; }
    memcpy(p->stage,p->old,p->map.size);
    if (p->oldCursor)
    {
        p->newCursor = AllocVec(p->cursorSize,MEMF_ANY);
        if (!p->newCursor) { Refuse(refusal, kind, LGR_LEASE_MEMORY, p->cursorSize, 0, 0, 0); goto fail; }
        for (y=0;y<p->map.cursorH;++y)
            memcpy(p->stage+(y+p->map.cursorY)*p->map.stride+p->map.cursorX*4,
                p->oldCursor+y*p->map.cursorW*4,p->map.cursorW*4);
    }
    if (p->map.presentation)
    {
        p->oldPresentation = AllocVec(p->map.presentationSize,MEMF_ANY);
        p->newPresentation = AllocVec(p->map.presentationSize,MEMF_ANY);
        if (!p->oldPresentation || !p->newPresentation)
        { Refuse(refusal, kind, LGR_LEASE_MEMORY, p->map.presentationSize, 0, 0, 0); goto fail; }
        memcpy(p->oldPresentation,p->map.presentation,p->map.presentationSize);
        memcpy(p->newPresentation,p->oldPresentation,p->map.presentationSize);
    }
    return p;
fail:
    ReleasePreparedPixels(p);
    return NULL;
}
/* The batch owns the lease; the caller owns proposed until lease release. */
struct HIDD_PixelMap *PreparePixelsCursor(struct PreparedPixels *p,
    struct HIDD_CursorState *proposed)
{
    if (!p || !p->writable || p->sealed || p->proposedCursor || !proposed ||
        !p->map.cursorState || !p->map.cursorCookie || !p->map.cursorSemaphore)
        return NULL;
    p->proposedCursor = proposed;
    return &p->map;
}
ULONG PreparedPixelsWidth(const struct PreparedPixels *p) { return p?p->map.width:0; }
ULONG PreparedPixelsHeight(const struct PreparedPixels *p) { return p?p->map.height:0; }
static BOOL RectOK(const struct PreparedPixels *p,ULONG x,ULONG y,ULONG w,ULONG h)
{
    return p && w && h && x<=p->map.width && y<=p->map.height &&
        w<=p->map.width-x && h<=p->map.height-y;
}
static BOOL BufferOK(ULONG w,ULONG h,ULONG stride)
{
    return w<=(~(ULONG)0)/sizeof(struct PreparedPixel) &&
        stride>=w*sizeof(struct PreparedPixel) && !(stride % sizeof(UWORD)) &&
        h && stride<=(~(ULONG)0)/h;
}
static UWORD Component(ULONG value,ULONG mask)
{
    ULONG bits=0;
    if (!mask) return 0xffff;
    while (!(mask&1)) { mask>>=1; value>>=1; }
    value&=mask;
    while (mask) { ++bits; mask>>=1; }
    return value<<(16-bits);
}
BOOL PreparedPenColor(const struct PreparedPixels *p, struct BitMap *bitmap,
    ULONG pen, struct PreparedPixel *out)
{
    struct Library *OOPBase;
    ULONG value = pen;
    if (!p || !bitmap || !out) return FALSE;
    OOPBase = p->oopBase;
    if (HIDD_BM_COLMAP(bitmap))
    {
        struct pHidd_ColorMap_GetPixel message = {
            OOP_GetMethodID(IID_Hidd_ColorMap, moHidd_ColorMap_GetPixel), pen};
        value = OOP_DoMethod(HIDD_BM_COLMAP(bitmap), (OOP_Msg)&message);
    }
    out->red = Component(value, p->map.red);
    out->green = Component(value, p->map.green);
    out->blue = Component(value, p->map.blue);
    out->alpha = Component(value, p->map.alpha);
    return TRUE;
}

static ULONG Pack(UWORD value,ULONG mask)
{
    ULONG shift=0,bits=0,n=mask;
    if (!mask) return 0;
    while (!(n&1)) { ++shift; n>>=1; }
    while (n) { ++bits; n>>=1; }
    return ((ULONG)(value>>(16-bits))<<shift)&mask;
}
static ULONG Load(const UBYTE *p) { ULONG value; memcpy(&value,p,4); return value; }
static VOID Store(UBYTE *p,ULONG value) { memcpy(p,&value,4); }
static ULONG OldValue(const struct PreparedPixels *p,ULONG x,ULONG y)
{
    const struct HIDD_PixelMap *m=&p->map;
    if (p->oldCursor && x>=m->cursorX && x-m->cursorX<m->cursorW &&
        y>=m->cursorY && y-m->cursorY<m->cursorH)
        return Load(p->oldCursor+((y-m->cursorY)*m->cursorW+x-m->cursorX)*4);
    return Load(p->old+y*m->stride+x*4);
}
static BOOL ReadPixels(const struct PreparedPixels *p,ULONG x,ULONG y,ULONG w,ULONG h,
    struct PreparedPixel *out,ULONG stride,BOOL staged)
{
    ULONG row,col,v;
    if (!p) return FALSE;
    if (!out || !RectOK(p,x,y,w,h) || !BufferOK(w,h,stride))
        return Refuse(p->refusal, p->kind, LGR_LEASE_RANGE, x, y, w, h);
    if (staged && (!p->writable || p->sealed))
        return Refuse(p->refusal, p->kind, LGR_LEASE_SEALED, 0, 0, 0, 0);
    for(row=0;row<h;++row)
    {
        struct PreparedPixel *q=(struct PreparedPixel *)((UBYTE *)out+row*stride);
        for(col=0;col<w;++col)
        {
            v=staged?Load(p->stage+(y+row)*p->map.stride+(x+col)*4):OldValue(p,x+col,y+row);
            q[col].red=Component(v,p->map.red); q[col].green=Component(v,p->map.green);
            q[col].blue=Component(v,p->map.blue); q[col].alpha=Component(v,p->map.alpha);
        }
    }
    return TRUE;
}
BOOL ReadOldPixels(const struct PreparedPixels *p,ULONG x,ULONG y,ULONG w,ULONG h,
    struct PreparedPixel *out,ULONG stride) { return ReadPixels(p,x,y,w,h,out,stride,FALSE); }
BOOL ReadStagedPixels(const struct PreparedPixels *p,ULONG x,ULONG y,ULONG w,ULONG h,
    struct PreparedPixel *out,ULONG stride) { return ReadPixels(p,x,y,w,h,out,stride,TRUE); }
BOOL WritePreparedPixels(struct PreparedPixels *p,ULONG x,ULONG y,ULONG w,ULONG h,
    const struct PreparedPixel *in,ULONG stride)
{
    ULONG row,col,mask;
    if (!p) return FALSE;
    if (!in || !RectOK(p,x,y,w,h) || !BufferOK(w,h,stride))
        return Refuse(p->refusal, p->kind, LGR_LEASE_RANGE, x, y, w, h);
    if (!p->writable || p->sealed)
        return Refuse(p->refusal, p->kind, LGR_LEASE_SEALED, 0, 0, 0, 0);
    mask=p->map.red|p->map.green|p->map.blue|p->map.alpha;
    for(row=0;row<h;++row)
    {
        const struct PreparedPixel *q=(const struct PreparedPixel *)((const UBYTE *)in+row*stride);
        for(col=0;col<w;++col)
        {
            UBYTE *dst=p->stage+(y+row)*p->map.stride+(x+col)*4;
            Store(dst,(Load(dst)&~mask)|Pack(q[col].red,p->map.red)|Pack(q[col].green,p->map.green)|
                Pack(q[col].blue,p->map.blue)|Pack(q[col].alpha,p->map.alpha));
        }
    }
    return TRUE;
}
BOOL CopyPreparedPixels(struct PreparedPixels *dst,ULONG dx,ULONG dy,
    const struct PreparedPixels *src,ULONG sx,ULONG sy,ULONG w,ULONG h,BOOL staged)
{
    ULONG row,col;
    if (!RectOK(dst,dx,dy,w,h) || !RectOK(src,sx,sy,w,h) || !dst->writable || dst->sealed ||
        (staged&&(!src->writable||src->sealed)) || dst->map.red!=src->map.red ||
        dst->map.green!=src->map.green || dst->map.blue!=src->map.blue || dst->map.alpha!=src->map.alpha)
        return FALSE;
    if(staged)
    {
        for(row=0;row<h;++row)
        {
            ULONG r=(src==dst&&dy>sy)?h-1-row:row;
            memmove(dst->stage+(dy+r)*dst->map.stride+dx*4,
                src->stage+(sy+r)*src->map.stride+sx*4,w*4);
        }
    }
    else for(row=0;row<h;++row) for(col=0;col<w;++col)
        Store(dst->stage+(dy+row)*dst->map.stride+(dx+col)*4,OldValue(src,sx+col,sy+row));
    return TRUE;
}
BOOL SealPreparedPixels(struct PreparedPixels *p)
{
    ULONG y;
    struct Library *OOPBase;
    if (!p) return FALSE;
    if (!p->writable || p->sealed) return Refuse(p->refusal, p->kind, LGR_LEASE_SEALED, 0, 0, 0, 0);
    OOPBase = p->oopBase;
    if(p->oldCursor || p->proposedCursor)
    {
        struct pHidd_BitMap_RenderPreparedCursor msg = {
            OOP_GetMethodID(IID_Hidd_BitMap,moHidd_BitMap_RenderPreparedCursor),
            &p->map, p->stage, p->proposedCursor};
        if (!p->proposedCursor) for(y=0;y<p->map.cursorH;++y)
            memcpy(p->newCursor+y*p->map.cursorW*4,
                p->stage+(y+p->map.cursorY)*p->map.stride+p->map.cursorX*4,p->map.cursorW*4);
        p->map.refusal = HIDD_PIXELS_REFUSE_NONE;
        p->map.refusalClass = NULL;
        if (!OOP_DoMethod(p->object,(OOP_Msg)&msg))
            return RefuseDriver(p->refusal, p->kind, LGR_LEASE_CURSOR, &p->map);
    }
    if(p->newPresentation) for(y=0;y<p->map.height;++y)
        memcpy(p->newPresentation+y*p->map.presentationStride,p->stage+y*p->map.stride,p->map.width*4);
    p->sealed=TRUE;
    return TRUE;
}
VOID CommitPreparedPixels(struct PreparedPixels *p)
{
    if (!p || !p->writable || !p->sealed) return;
    if(p->newCursor && !p->proposedCursor) memcpy(p->map.cursorBackup,p->newCursor,p->cursorSize);
    memcpy(p->map.pixels,p->stage,p->map.size);
    if(p->newPresentation) memcpy(p->map.presentation,p->newPresentation,p->map.presentationSize);
}
VOID RestorePreparedPixels(struct PreparedPixels *p)
{
    if (!p || !p->writable) return;
    if(p->oldCursor) memcpy(p->map.cursorBackup,p->oldCursor,p->cursorSize);
    memcpy(p->map.pixels,p->old,p->map.size);
    if(p->oldPresentation) memcpy(p->map.presentation,p->oldPresentation,p->map.presentationSize);
}
