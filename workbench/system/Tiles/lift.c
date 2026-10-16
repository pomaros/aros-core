/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include "lift.h"
#include <exec/memory.h>
#include <graphics/layers.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/cybergraphics.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/cybergraphics.h>
#include <string.h>
#define LIFT_LIMIT 40
struct TileLift {
    struct Window *window;
    struct MsgPort *port;
    struct Screen *screen;
    UBYTE *background;
    ULONG width, height, count;
    LONG left, top;
    struct TileLiftItem items[LIFT_LIMIT];
};
BOOL TileLiftUpdateBitmap(struct TileLift *lift, ULONG id, struct BitMap *bitmap)
{
    if (!lift || !bitmap) return FALSE;
    for (ULONG n=0;n<lift->count;++n) {
        struct TileLiftItem *item=&lift->items[n];
        if (item->id != id) continue;
        if (item->bitmap == bitmap || GetBitMapAttr(bitmap,BMA_WIDTH)<item->width ||
            GetBitMapAttr(bitmap,BMA_HEIGHT)<item->height) return FALSE;
        item->bitmap=bitmap;
        return TRUE;
    }
    return FALSE;
}
static void blit_items(struct TileLift *lift, ULONG first)
{
    struct RastPort *rp = lift->window->RPort;
    for (ULONG i=first;i<lift->count;++i) {
        const struct TileLiftItem *item=&lift->items[i];
        LONG x=item->left,y=item->top,sx=0,sy=0,w=item->width,h=item->height;
        if(x<0){sx=-x;w+=x;x=0;} if(y<0){sy=-y;h+=y;y=0;}
        if(x+w>(LONG)lift->width)w=lift->width-x;
        if(y+h>(LONG)lift->height)h=lift->height-y;
        if(w>0 && h>0) BltBitMapRastPort(item->bitmap,sx,sy,rp,x,y,w,h,0xc0);
    }
    WaitBlit();
}
void TileLiftRefreshItem(struct TileLift *lift, ULONG id)
{
    if (!lift || !lift->window) return;
    for (ULONG n=0;n<lift->count;++n) if(lift->items[n].id==id) {
        /* Opaque, fixed-size cache replaces its own pixels. Repaint later
         * items in stacking order so overlaps retain their original order.
         * At most forty cached blits; never rewrite the dimmed background. */
        blit_items(lift,n);
        return;
    }
}
void TileLiftRefresh(struct TileLift *lift)
{
    if (!lift || !lift->window) return;
    if(lift->background)
        WritePixelArray(lift->background,0,0,lift->width*4,lift->window->RPort,0,0,lift->width,lift->height,RECTFMT_ARGB);
    else SetRast(lift->window->RPort,0);
    blit_items(lift,0);
}
void TileLiftClose(struct TileLift *lift)
{
    if(!lift)return;
    if(lift->window) {
        /* Detach private port and drain replies before closing its window. */
        Forbid();
        lift->window->UserPort=NULL;
        ModifyIDCMP(lift->window,0);
        struct Message *message;
        while((message=GetMsg(lift->port)))ReplyMsg(message);
        Permit();
        CloseWindow(lift->window);
    }
    if(lift->port)DeleteMsgPort(lift->port);
    if(lift->background)FreeVec(lift->background);
    FreeVec(lift);
}
struct TileLift *TileLiftOpen(struct Screen *screen,const struct TileLiftItem *items,ULONG count)
{
    if(!screen || !items || !count || count>LIFT_LIMIT || screen->Width<1 || screen->Height<1 || screen->Width>4096 || screen->Height>4096)return NULL;
    for(ULONG i=0;i<count;++i)if(!items[i].bitmap || !items[i].width || !items[i].height || items[i].width>4096 || items[i].height>4096 || items[i].left < -4096 || items[i].top < -4096 || items[i].left>4096 || items[i].top>4096 || GetBitMapAttr(items[i].bitmap,BMA_WIDTH)<items[i].width || GetBitMapAttr(items[i].bitmap,BMA_HEIGHT)<items[i].height)return NULL;
    struct TileLift *lift=AllocVec(sizeof(*lift),MEMF_CLEAR);
    if(!lift)return NULL;
    lift->screen=screen;lift->width=screen->Width;lift->height=screen->Height;lift->count=count;
    memcpy(lift->items,items,count*sizeof(*items));
    lift->port=CreateMsgPort();lift->background=AllocVec(lift->width*lift->height*4,MEMF_ANY);
    if(!lift->port || !lift->background)goto failed;
    if(ReadPixelArray(lift->background,0,0,lift->width*4,&screen->RastPort,0,0,lift->width,lift->height,RECTFMT_ARGB)!=lift->width*lift->height)goto failed;
    /* TILE4: windows dimmed TO thirty percent, alpha preserved. */
    for(ULONG i=0;i<lift->width*lift->height;++i)for(ULONG c=1;c<4;++c)lift->background[i*4+c]=(lift->background[i*4+c]*30)/100;
    lift->window=OpenWindowTags(NULL,WA_CustomScreen,(IPTR)screen,WA_Left,0,WA_Top,0,
        WA_Width,lift->width,WA_Height,lift->height,WA_Borderless,TRUE,
        WA_ToolBox,TRUE,WA_TileOverlay,TRUE,WA_Activate,FALSE,WA_SimpleRefresh,TRUE,WA_BackFill,LAYERS_NOBACKFILL,TAG_DONE);
    if(!lift->window)goto failed;
    lift->window->UserPort=lift->port;
    if(!ModifyIDCMP(lift->window,IDCMP_REFRESHWINDOW))goto failed;
    TileLiftRefresh(lift);return lift;
failed:TileLiftClose(lift);return NULL;
}
struct TileLift *TilePanelOpen(struct Screen *screen,const struct TileLiftItem *items,ULONG count,
                              LONG left,LONG top,ULONG width,ULONG height)
{
    if(!screen || !items || !count || count>LIFT_LIMIT || !width || !height ||
       width>4096 || height>4096 || left<0 || top<0 || left>4096 || top>4096 ||
       left+(LONG)width>screen->Width || top+(LONG)height>screen->Height)return NULL;
    for(ULONG i=0;i<count;++i)if(!items[i].bitmap || !items[i].width || !items[i].height ||
       items[i].width>4096 || items[i].height>4096 || items[i].left < -4096 || items[i].top < -4096 ||
       items[i].left>4096 || items[i].top>4096 || GetBitMapAttr(items[i].bitmap,BMA_WIDTH)<items[i].width ||
       GetBitMapAttr(items[i].bitmap,BMA_HEIGHT)<items[i].height)return NULL;
    struct TileLift *lift=AllocVec(sizeof(*lift),MEMF_CLEAR);
    if(!lift)return NULL;
    lift->screen=screen;lift->left=left;lift->top=top;lift->width=width;lift->height=height;lift->count=count;
    memcpy(lift->items,items,count*sizeof(*items));
    lift->port=CreateMsgPort();if(!lift->port)goto failed;
    lift->window=OpenWindowTags(NULL,WA_CustomScreen,(IPTR)screen,WA_Left,left,WA_Top,top,
        WA_Width,width,WA_Height,height,WA_Borderless,TRUE,WA_ToolBox,TRUE,WA_TileOverlay,TRUE,
        WA_Activate,FALSE,WA_SimpleRefresh,TRUE,WA_BackFill,LAYERS_NOBACKFILL,TAG_DONE);
    if(!lift->window)goto failed;
    lift->window->UserPort=lift->port;
    if(!ModifyIDCMP(lift->window,IDCMP_REFRESHWINDOW))goto failed;
    TileLiftRefresh(lift);return lift;
failed:TileLiftClose(lift);return NULL;
}
ULONG TileLiftSignals(const struct TileLift *lift){return lift && lift->port ? 1UL<<lift->port->mp_SigBit : 0;}
BOOL TileLiftInput(struct TileLift *lift,LONG x,LONG y,BOOL escape,struct TileLiftEvent *event)
{
    if(!lift || !event)return FALSE;
    memset(event,0,sizeof(*event));event->type=TILE_LIFT_DISMISS;
    if(x<lift->left || y<lift->top || x-lift->left>=(LONG)lift->width || y-lift->top>=(LONG)lift->height)return TRUE;
    x-=lift->left;y-=lift->top;
    if(!escape)for(ULONG n=lift->count;n;--n){const struct TileLiftItem *i=&lift->items[n-1];
        if(x>=i->left && y>=i->top && x-i->left<(LONG)i->width && y-i->top<(LONG)i->height){event->type=TILE_LIFT_CLICK;event->id=i->id;event->x=x-i->left;event->y=y-i->top;break;}}
    return TRUE;
}
BOOL TileLiftPump(struct TileLift *lift,struct TileLiftEvent *event)
{
    if(!lift || !event)return FALSE;
    struct IntuiMessage *message;
    while((message=(struct IntuiMessage *)GetMsg(lift->port))){ULONG cls=message->Class;ReplyMsg(&message->ExecMessage);
        if(cls==IDCMP_REFRESHWINDOW){BeginRefresh(lift->window);TileLiftRefresh(lift);EndRefresh(lift->window,TRUE);}

    }
    return FALSE;
}
void TileLiftFront(struct TileLift *lift){if(lift && lift->window)WindowToFront(lift->window);}
