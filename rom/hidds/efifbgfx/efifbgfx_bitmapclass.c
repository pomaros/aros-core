/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: Bitmap class for the EFI framebuffer Gfx hidd.
*/

#include <proto/oop.h>
#include <proto/utility.h>
#include <assert.h>
#include <exec/memory.h>
#include <exec/lists.h>
#include <graphics/rastport.h>
#include <graphics/gfx.h>
#include <hidd/gfx.h>
#include <oop/oop.h>
#include <aros/symbolsets.h>
#define DEBUG 0
#include <aros/debug.h>

#include <string.h>

#include "efifbgfx_hidd.h"

#include LC_LIBDEFS_FILE

#define MNAME_ROOT(x) EFIFBGfxBM__Root__ ## x
#define MNAME_BM(x) EFIFBGfxBM__Hidd_BitMap__ ## x

/*********** BitMap::New() *************************************/
OOP_Object *MNAME_ROOT(New)(OOP_Class *cl, OOP_Object *o, struct pRoot_New *msg)
{
    EnterFunc(bug("EFIFBGfx.BitMap::New()\n"));

    o = (OOP_Object *)OOP_DoSuperMethod(cl, o, (OOP_Msg) msg);
    if (o)
    {
        OOP_MethodID       disp_mid;
        struct EFIFBGfxBitMapData *data;
        HIDDT_ModeID       modeid;
        OOP_Object        *sync, *pf, *dmenum;

        data = OOP_INST_DATA(cl, o);

        /* Get attr values */
        OOP_GetAttr(o, aHidd_BitMap_Display , (APTR)&data->displayhidd);
        OOP_GetAttr(o, aHidd_BitMap_PixFmt  , (APTR)&data->pixfmtobj);
        OOP_GetAttr(o, aHidd_BitMap_ModeID  , &modeid);
        OOP_GetAttr(o, aHidd_ChunkyBM_Buffer, (IPTR *)&data->VideoData);

        OOP_GetAttr(data->displayhidd, aHidd_Display_DMEnumerator, (APTR)&dmenum);
        HIDD_DMEnum_GetMode(dmenum, modeid, &sync, &pf);

        data->width        = OOP_GET(o, aHidd_BitMap_Width);
        data->height       = OOP_GET(o, aHidd_BitMap_Height);
        data->bytesperline = OOP_GET(o, aHidd_BitMap_BytesPerRow);
        data->bytesperpix  = OOP_GET(data->pixfmtobj, aHidd_PixFmt_BytesPerPixel);
        data->bpp          = OOP_GET(data->pixfmtobj, aHidd_PixFmt_BitsPerPixel);
        data->disp_width   = OOP_GET(sync, aHidd_Sync_HDisp);
        data->disp_height  = OOP_GET(sync, aHidd_Sync_VDisp);

        D(bug("[EFIFBGfx:BitMap] Bitmap %ld x %ld, %u bytes per pixel, %u bytes per line\n",
              data->width, data->height, data->bytesperpix, data->bytesperline));
        D(bug("[EFIFBGfx:BitMap] Video data at 0x%p (%u bytes)\n", data->VideoData, data->bytesperline * data->height));

        if (OOP_GET(data->pixfmtobj, aHidd_PixFmt_ColorModel) != vHidd_ColorModel_Palette)
            ReturnPtr("EFIFBGfx.BitMap::New()", OOP_Object *, o);

        data->DAC = AllocMem(768, MEMF_ANY);
        D(bug("[EFIFBGfx:BitMap] Palette data at 0x%p\n", data->DAC));
        if (data->DAC)
            ReturnPtr("EFIFBGfx.BitMap::New()", OOP_Object *, o);

        disp_mid = OOP_GetMethodID(IID_Root, moRoot_Dispose);

        OOP_CoerceMethod(cl, o, (OOP_Msg) &disp_mid);
        o = NULL;
    } /* if created object */

    ReturnPtr("EFIFBGfx.BitMap::New()", OOP_Object *, o);
}

/**********  Bitmap::Dispose()  ***********************************/
VOID MNAME_ROOT(Dispose)(OOP_Class *cl, OOP_Object *o, OOP_Msg msg)
{
    struct EFIFBGfxBitMapData *data = OOP_INST_DATA(cl, o);

    D(bug("[EFIFBGfx:BitMap] Dispose(0x%p)\n", o));

    if (data->DAC)
        FreeMem(data->DAC, 768);

    OOP_DoSuperMethod(cl, o, msg);

    ReturnVoid("EFIFBGfx.BitMap::Dispose");
}

/*** BitMap::Get() *******************************************/

VOID MNAME_ROOT(Get)(OOP_Class *cl, OOP_Object *o, struct pRoot_Get *msg)
{
    struct EFIFBGfxBitMapData *data = OOP_INST_DATA(cl, o);
    ULONG              idx;

    if (IS_BM_ATTR(msg->attrID, idx))
    {
        switch (idx)
        {
        case aoHidd_BitMap_Visible:
            *msg->storage = data->disp;
            return;
        }
    }
    OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
}

/*** BitMap::Set() *******************************************/

VOID MNAME_ROOT(Set)(OOP_Class *cl, OOP_Object *o, struct pRoot_Set *msg)
{
    struct EFIFBGfxBitMapData *data = OOP_INST_DATA(cl, o);
    struct TagItem  *tag, *tstate;
    ULONG           idx;
    IPTR xoffset = data->xoffset;
    IPTR yoffset = data->yoffset;

    tstate = msg->attrList;
    while((tag = NextTagItem(&tstate)))
    {
        if(IS_BM_ATTR(tag->ti_Tag, idx))
        {
            switch(idx)
            {
            case aoHidd_BitMap_Visible:
                D(bug("[EFIFBGfx:BitMap] Setting Visible to %d\n", tag->ti_Data));
                data->disp = tag->ti_Data;
                if (data->disp) {
                    if (data->DAC)
                        DACLoad(XSD(cl), data->DAC, 0, 256);
                }
                break;
            }
        }
    }

    OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);

    /* Bring local X/Y offsets back into sync with validated values */
    OOP_GetAttr(o, aHidd_BitMap_LeftEdge, &xoffset);
    OOP_GetAttr(o, aHidd_BitMap_TopEdge, &yoffset);

    if ((xoffset != data->xoffset) || (yoffset != data->yoffset))
    {
        D(bug("[EFIFBGfx:BitMap] Scroll to (%d, %d)\n", xoffset, yoffset));
        data->xoffset = xoffset;
        data->yoffset = yoffset;

        if (data->disp)
        {
            LOCK_FRAMEBUFFER(XSD(cl));
            efifbDoRefreshArea(&XSD(cl)->data, data, 0, 0, data->width, data->height);
            UNLOCK_FRAMEBUFFER(XSD(cl));
        }
    }
}

/*** BitMap::SetColors() *************************************/

BOOL MNAME_BM(SetColors)(OOP_Class *cl, OOP_Object *o, struct pHidd_BitMap_SetColors *msg)
{
    struct EFIFBGfxBitMapData *data = OOP_INST_DATA(cl, o);
    struct HWData *hwdata = &XSD(cl)->data;
    ULONG xc_i, col_i;
    UBYTE p_shift;
    UWORD red, green, blue;

    D(bug("[EFIFBGfx:BitMap] SetColors(%u, %u)\n", msg->firstColor, msg->numColors));

    if (!OOP_DoSuperMethod(cl, o, (OOP_Msg)msg)) {
        D(bug("[EFIFBGfx:BitMap] DoSuperMethod() failed\n"));
        return FALSE;
    }

    if ((msg->firstColor + msg->numColors) > (1 << data->bpp))
        return FALSE;

    if (data->DAC) {
        for ( xc_i = msg->firstColor, col_i = 0;
              col_i < msg->numColors;
              xc_i ++, col_i ++) {
            red   = msg->colors[col_i].red   >> 8;
            green = msg->colors[col_i].green >> 8;
            blue  = msg->colors[col_i].blue  >> 8;

            /* Update DAC register values */
            p_shift = 8 - hwdata->palettewidth;
            data->DAC[xc_i*3] = red >> p_shift;
            data->DAC[xc_i*3+1] = green >> p_shift;
            data->DAC[xc_i*3+2] = blue >> p_shift;
        }

        /* Upload palette to the DAC if the current bitmap is on display */
        if (data->disp)
            DACLoad(XSD(cl), data->DAC, msg->firstColor, msg->numColors);
    }
    return TRUE;
}

VOID MNAME_BM(UpdateRect)(OOP_Class *cl, OOP_Object *o, struct pHidd_BitMap_UpdateRect *msg)
{
    struct EFIFBGfxBitMapData *data = OOP_INST_DATA(cl, o);

    D(bug("[EFIFBGfx:BitMap] UpdateRect(%d, %d, %d, %d), bitmap 0x%p\n", msg->x, msg->y, msg->width, msg->height, o));
    if (data->disp) {
        LOCK_FRAMEBUFFER(XSD(cl));
        efifbDoRefreshArea(&XSD(cl)->data, data, msg->x, msg->y, msg->x + msg->width, msg->y + msg->height);
        UNLOCK_FRAMEBUFFER(XSD(cl));
    }
}

static BOOL efifb_refuse(struct HIDD_PixelMap *m, ULONG cause, LONG v0, LONG v1)
{
    m->refusal = cause;
    m->refusalValue[0] = v0; m->refusalValue[1] = v1;
    m->refusalClass = CLID_Hidd_BitMap_EFIFB;
    return FALSE;
}

/* The first way the shown bitmap differs from the native scanout, or 0. */
static ULONG efifb_scanout(const struct EFIFBGfxBitMapData *data,
    const struct HWData *hw, const struct HIDD_PixelMap *m, LONG *value)
{
    *value = 0;
    if (!hw->framebuffer) return HIDD_SCANOUT_NO_FRAMEBUFFER;
    if (hw->present) return HIDD_SCANOUT_CALLBACK;
    if (!hw->owned) return HIDD_SCANOUT_NOT_OWNED;
    if (data->xoffset || data->yoffset)
    { *value = data->xoffset ? (LONG)data->xoffset : (LONG)data->yoffset; return HIDD_SCANOUT_PANNED; }
    if (m->width != hw->width) { *value = hw->width; return HIDD_SCANOUT_WIDTH; }
    if (m->height != hw->height) { *value = hw->height; return HIDD_SCANOUT_HEIGHT; }
    if (hw->bytesperpixel != 4) { *value = hw->bytesperpixel; return HIDD_SCANOUT_DEPTH; }
    if (m->red != hw->redmask || m->green != hw->greenmask || m->blue != hw->bluemask)
    { *value = (LONG)hw->redmask; return HIDD_SCANOUT_MASKS; }
    if (!hw->bytesperline || hw->bytesperline > (~(ULONG)0) / hw->height ||
        hw->fbsize < hw->bytesperline * hw->height)
    { *value = hw->bytesperline; return HIDD_SCANOUT_PITCH; }
    return 0;
}

/* Freeze the audited RAM shadow and direct native scanout together. Callback
 * presentation and panned/partial screens need another backend and refuse. */
BOOL MNAME_BM(PreparePixels)(OOP_Class *cl, OOP_Object *o,
    struct pHidd_BitMap_PreparePixels *msg)
{
    struct EFIFBGfxBitMapData *data = OOP_INST_DATA(cl, o);
    struct EFIFBGfx_staticdata *xsd = XSD(cl);
    struct HWData *hw = &xsd->data;
    struct HIDD_PixelMap *m = msg->map;
    IPTR v;
    if (!m) return FALSE;
    if (OOP_OCLASS(o) != cl) return efifb_refuse(m, HIDD_PIXELS_REFUSE_SUBCLASS, 0, 0);
    if (!data->VideoData || data->bytesperpix != 4)
        return efifb_refuse(m, HIDD_PIXELS_REFUSE_STORAGE, data->bytesperpix, data->VideoData != NULL);
    OOP_GetAttr(data->pixfmtobj, aHidd_PixFmt_ColorModel, &v);
    if (v != vHidd_ColorModel_TrueColor)
        return efifb_refuse(m, HIDD_PIXELS_REFUSE_COLORMODEL, (LONG)v, 0);
    OOP_GetAttr(data->pixfmtobj, aHidd_PixFmt_SwapPixelBytes, &v);
    if (v) return efifb_refuse(m, HIDD_PIXELS_REFUSE_SWAPPED, 0, 0);
    m->width = data->width; m->height = data->height;
    m->stride = data->bytesperline;
    if (!m->height || m->stride > (~(ULONG)0) / m->height)
        return efifb_refuse(m, HIDD_PIXELS_REFUSE_GEOMETRY, (LONG)m->stride, (LONG)m->height);
    m->size = m->stride * m->height;
    OOP_GetAttr(o, aHidd_ChunkyBM_StorageSize, &v);
    if (v < m->size) return efifb_refuse(m, HIDD_PIXELS_REFUSE_STORAGE_SIZE, (LONG)v, (LONG)m->size);
    OOP_GetAttr(data->pixfmtobj, aHidd_PixFmt_RedMask, &v); m->red = v;
    OOP_GetAttr(data->pixfmtobj, aHidd_PixFmt_GreenMask, &v); m->green = v;
    OOP_GetAttr(data->pixfmtobj, aHidd_PixFmt_BlueMask, &v); m->blue = v;
    OOP_GetAttr(data->pixfmtobj, aHidd_PixFmt_AlphaMask, &v); m->alpha = v;
    LOCK_FRAMEBUFFER(xsd);
    if (data->disp)
    {
        LONG value;
        ULONG scanout = efifb_scanout(data, hw, m, &value);
        if (scanout)
        {
            UNLOCK_FRAMEBUFFER(xsd);
            return efifb_refuse(m, HIDD_PIXELS_REFUSE_SCANOUT, (LONG)scanout, value);
        }
        m->presentation = hw->framebuffer;
        m->presentationStride = hw->bytesperline;
        m->presentationSize = hw->fbsize;
    }
    m->pixels = data->VideoData;
    m->driverCookie = o;
    return TRUE;
}
VOID MNAME_BM(ReleasePixels)(OOP_Class *cl, OOP_Object *o,
    struct pHidd_BitMap_ReleasePixels *msg)
{
    if (msg->map->driverCookie == o)
    {
        msg->map->driverCookie = NULL;
        UNLOCK_FRAMEBUFFER(XSD(cl));
    }
}
