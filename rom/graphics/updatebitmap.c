/*
    Copyright (C) 2011-2026, The AROS Development Team. All rights reserved.
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.

    Desc: Update specified region of the bitmap, taking software composition into account.
          Private function for cybergraphics.library support.
*/

#include "graphics_intern.h"
#include "gfxfuncsupport.h"

#include <hidd/gfx.h>
#include <proto/utility.h>

AROS_LH5(void, UpdateBitMap,
         AROS_LHA(struct BitMap *, bitmap, A0),
         AROS_LHA(UWORD, x     , D0),
         AROS_LHA(UWORD, y     , D1),
         AROS_LHA(UWORD, width , D2),
         AROS_LHA(UWORD, height, D3),
         struct GfxBase *, GfxBase, 201, Graphics)
{
    AROS_LIBFUNC_INIT

    /* This function must be called only on HIDD bitmaps */
    update_bitmap(bitmap, HIDD_BM_OBJ(bitmap), x, y, width, height, GfxBase);

    AROS_LIBFUNC_EXIT
}

void update_bitmap(struct BitMap *bitmap, OOP_Object *bm, UWORD x, UWORD y, UWORD width, UWORD height,
                   struct GfxBase *GfxBase)
{
    /*
     * The bitmap's display (the software rasterizer's for bitmaps that are
     * not displayable). Damage goes to it: it re-composes the visible part
     * or passes it on to the bitmap. This runs after every rendering
     * operation, so the display's method was resolved once at display setup.
     */
    struct gfxdisplay_data *mdd = (struct gfxdisplay_data *)GET_BM_DRIVERDATA(bitmap);

    if (mdd && mdd->display_UpdateRect)
    {
        struct pHidd_Display_UpdateRect msg =
        {
            .mID    = HiddDisplayBase + moHidd_Display_UpdateRect,
            .bitMap = bm,
            .x      = x,
            .y      = y,
            .width  = width,
            .height = height
        };

        mdd->display_UpdateRect(mdd->display_UpdateRect_cl, mdd->display_obj, (OOP_Msg)&msg);
    }
    else
        HIDD_BM_UpdateRect(bm, x, y, width, height);

    if (IS_HIDD_BM(bitmap) && width && height && HIDD_BM_UPDATEHOOK(bitmap))
    {
        struct BitMapUpdateMessage message = {x, y, width, height};
        CallHookPkt(HIDD_BM_UPDATEHOOK(bitmap), bitmap, &message);
    }
}
