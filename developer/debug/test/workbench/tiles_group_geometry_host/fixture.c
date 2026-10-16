/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
typedef int BOOL;typedef int32_t LONG;typedef uint32_t ULONG;typedef uintptr_t IPTR;typedef void Object;
#define TRUE 1
#define FALSE 0
#define ILE_TYPE_APPICON 10
#define ICONTILE_SIZE 1
#define ICONTILE_ANCHOR 10
#define MUIM_IconList_Tile 1
#define ICON_LISTMODE_GRID 1
#define _mleft(obj) 0
#define _mtop(obj) 0
struct Rectangle {LONG MinX,MinY,MaxX,MaxY;};
struct IconEntry {struct {int type;} ie_IconListEntry;ULONG ie_IconWidth,ie_IconHeight,ie_AreaWidth,ie_AreaHeight,ie_GridWidth,ie_GridHeight;LONG ie_IconX,ie_IconY;};
struct IconList_DATA {LONG icld_ViewX,icld_ViewY;ULONG icld__Option_IconListMode,icld_IconLargestHeight,icld__Option_IconImageSpacing,icld_IconAreaLargestWidth,icld_IconAreaLargestHeight;};
struct IconTileRequest {unsigned action,width,height;void *rp;LONG x,y;};
struct Layer {struct Rectangle bounds;LONG Scroll_X,Scroll_Y;};
struct Screen {LONG Width,Height;};struct Window {struct Screen *WScreen;};
static ULONG imageWidth=64,labelWidth=92,anchorValue=2;static int layerLocks;
static IPTR DoMethod(Object*o,unsigned method,...){(void)o;assert(method==1&&!layerLocks);va_list v;va_start(v,method);(void)va_arg(v,IPTR);struct IconTileRequest*r=(void*)va_arg(v,IPTR);va_end(v);if(r->action==ICONTILE_ANCHOR)r->width=anchorValue;else {assert(r->action==ICONTILE_SIZE);r->width=imageWidth;r->height=64;}return 1;}
static void LockLayer(int zero,struct Layer*l){(void)zero;(void)l;assert(!layerLocks);++layerLocks;}
static void UnlockLayer(struct Layer*l){(void)l;assert(layerLocks==1);--layerLocks;}
static void IconList_GetIconImageRectangle(Object*o,struct IconList_DATA*d,struct IconEntry*e,struct Rectangle*r){(void)o;(void)d;e->ie_IconWidth=imageWidth;e->ie_IconHeight=64;r->MinX=r->MinY=0;r->MaxX=imageWidth-1;r->MaxY=63;}
static void IconList_GetIconLabelRectangle(Object*o,struct IconList_DATA*d,struct IconEntry*e,struct Rectangle*r){(void)o;(void)d;(void)e;r->MinX=r->MinY=0;r->MaxX=labelWidth-1;r->MaxY=19;}
#include "iconlist_visibility.h"
#include "functions.inc"
static void sample(struct IconList_DATA *data,struct IconEntry *entry,struct Window *window,struct Layer *layer,LONG*outX,LONG*outY)
{
    Object *obj=NULL;LONG ox,oy;BOOL anchorMoved=FALSE;struct IconTileRequest size={ICONTILE_SIZE,imageWidth,64,NULL,0,0};
#include "sample.inc"
    *outX=entry->ie_IconX+ox+layer->bounds.MinX-layer->Scroll_X-data->icld_ViewX;
    *outY=entry->ie_IconY+oy+layer->bounds.MinY-layer->Scroll_Y-data->icld_ViewY;
    (void)anchorMoved;
}
static void check(struct IconList_DATA*d,struct IconEntry*e,struct Window*w,struct Layer*l,LONG expectedX,LONG expectedY)
{
    LONG reportedX,reportedY,ox,oy;struct Rectangle area;
    sample(d,e,w,l,&reportedX,&reportedY);
    /* Actual draw recalculates area before deriving its offsets. */
    IconList_GetIconAreaRectangle(NULL,d,e,&area);IconList_GetIconImageOffsets(d,e,&ox,&oy);
    LONG drawnX=e->ie_IconX+ox+l->bounds.MinX-l->Scroll_X-d->icld_ViewX;
    LONG drawnY=e->ie_IconY+oy+l->bounds.MinY-l->Scroll_Y-d->icld_ViewY;
    assert(reportedX==drawnX&&reportedY==drawnY&&drawnX==expectedX&&drawnY==expectedY&&!layerLocks);
}
int main(void)
{
    struct IconList_DATA data={0,0,1,64,2,97,90};struct IconEntry entry={{10},64,64,64,64,64,64,5,275};
    struct Screen screen={1024,900};struct Window window={&screen};struct Layer layer={{0,15,0,0},0,0};
    check(&data,&entry,&window,&layer,19,836); /* observed one-cell group and wider label */
    screen.Height=768;check(&data,&entry,&window,&layer,19,704);
    anchorValue=3;check(&data,&entry,&window,&layer,0,704);
    anchorValue=4;check(&data,&entry,&window,&layer,960,704);
    anchorValue=1;check(&data,&entry,&window,&layer,960,0);
    anchorValue=0;entry.ie_IconX=5;entry.ie_IconY=275;check(&data,&entry,&window,&layer,19,290);
    imageWidth=128;entry.ie_GridWidth=128;check(&data,&entry,&window,&layer,5,290);
    imageWidth=64;entry.ie_GridWidth=64;data.icld_ViewX=7;layer.Scroll_X=3;anchorValue=4;check(&data,&entry,&window,&layer,960,290);
    puts("tiles-group-geometry PASS");
}
