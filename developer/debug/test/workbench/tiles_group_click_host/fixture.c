/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include <assert.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef int BOOL;typedef int LONG;typedef uintptr_t IPTR;typedef void Object;
#define TRUE 1
#define FALSE 0
#define ICONTILE_ANCHOR 10
#define ICONTILE_HIT 5
#define ICONTILE_CLICK 2
#define MUIM_IconList_Tile 1
#define MUIM_Window_RemEventHandler 2
#define MUIM_Window_AddEventHandler 3
#define ILE_TYPE_APPICON 9
#define ICONLIST_DISP_MODELIST 1
#define MUIA_IconList_DoubleClick 2
#define LEFT_BUTTON 1
#define IDCMP_MOUSEMOVE 4
#define MUI_EventHandlerRC_Eat 1
#define _win(obj) (obj)
struct IconEntry {struct {int type;} ie_IconListEntry;LONG ie_IconX,ie_IconY;};
struct IconList_DATA {struct IconEntry *icld_SelectionLastClicked;unsigned icld_DisplayFlags,mouse_pressed,last_secs,last_mics;LONG icld_ViewX,icld_ViewY,click_x,click_y;BOOL icld_LassoActive;struct {unsigned ehn_Events;} ehn;};
struct IconTileRequest {unsigned action,width,height;void *rp;LONG x,y;};
struct Input {unsigned Seconds,Micros;};struct Message {struct Input *imsg;};
static unsigned group,hit,opens,hitQueries,anchorQueries,motions,clicks;static LONG lastX,lastY;
#define SET(obj,attr,value) do{(void)(obj);assert((attr)==MUIA_IconList_DoubleClick&&(value));++opens;}while(0)
static IPTR DoMethod(Object *obj,unsigned method,...)
{
    (void)obj;
    if(method!=MUIM_IconList_Tile){++motions;return 0;}
    va_list args;va_start(args,method);(void)va_arg(args,IPTR);struct IconTileRequest *r=(void*)va_arg(args,IPTR);va_end(args);
    if(r->action==ICONTILE_ANCHOR){++anchorQueries;r->width=0;return group;}
    if(r->action==ICONTILE_CLICK){++clicks;return 1;}
    assert(r->action==ICONTILE_HIT);++hitQueries;lastX=r->x;lastY=r->y;return hit;
}
static void IconList_GetIconImageOffsets(struct IconList_DATA*d,struct IconEntry*e,LONG*x,LONG*y){(void)d;(void)e;*x=7;*y=11;}
#include "helper.inc"
static int dispatch(struct IconList_DATA *data,BOOL doubleclicked,LONG mx,LONG my)
{
    Object *obj=NULL;struct Input input={123,456};struct Message storage={&input},*message=&storage;
#include "dispatch.inc"
}
static void release(struct IconList_DATA *data,LONG mx,LONG my)
{
    Object *obj=NULL;
#include "release.inc"
}
int main(void)
{
    struct IconEntry entry={{ILE_TYPE_APPICON},100,200};
    struct IconList_DATA data={0};data.icld_SelectionLastClicked=&entry;data.icld_ViewX=30;data.icld_ViewY=40;data.click_x=95;data.click_y=180;
    group=hit=1;assert(dispatch(&data,TRUE,95,180)==1);
    assert(opens==1&&!data.mouse_pressed&&!hitQueries&&anchorQueries==1&&!motions);
    release(&data,95,180);assert(!clicks);
    /* No armed SELECTUP means the double-click's second release cannot send a child click. */
    group=0;opens=0;assert(dispatch(&data,TRUE,95,180)==1);
    assert(!opens&&data.mouse_pressed==LEFT_BUTTON&&hitQueries==1&&motions==2);
    release(&data,95,180);assert(clicks==1);
    assert(lastX==18&&lastY==9&&data.last_secs==123&&data.last_mics==456);
    /* A stale/outside ordinary tile still gets conventional double-open. */
    data.mouse_pressed=0;hit=0;assert(dispatch(&data,TRUE,95,180)==1);assert(opens==1&&!data.mouse_pressed);
    /* First group click still arms normal child-region release. */
    opens=0;group=hit=1;unsigned old=anchorQueries;assert(dispatch(&data,FALSE,95,180)==1);
    assert(!opens&&data.mouse_pressed==LEFT_BUTTON&&anchorQueries==old);
    release(&data,95,180);assert(clicks==2);
    puts("tiles-group-doubleclick PASS");
}
