/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef unsigned ULONG;typedef int BOOL;typedef intptr_t IPTR;
#define TRUE 1
#define FALSE 0
#define TILE_LIMIT 40
#define TAG_DONE 0
#define WBCTRLA_ActivateAppIconSnapshot 117
#define TILE_INPUT_ALL 0
#define TILE_INPUT_GROUP 1
#define TILE_INPUT_DROP 2
#define TILE_INPUT_CLICK 3
#define TILE_INPUT_PANEL_CLICK 4
#define TILE_INPUT_PANEL_DROP 5
#define TILE_LIFT_DISMISS 2
#define TILE_LIFT_CLICK 1
struct Screen {int id;};
struct {struct Screen *FirstScreen;} intuition,*IntuitionBase=&intuition;
struct TileInputEvent {unsigned kind,group;int x,y;};
struct TileLiftEvent {unsigned type,id;int x,y;};
struct TileEntry {int unused;};
struct ServiceGroup {void *panel;int left,top;};
struct TileService {struct Screen *screen;void *lift,*input,*snapshot;unsigned groupCount,count;struct TileEntry tiles[40];struct ServiceGroup groups[8];};
struct AppIconSnapshotActivate {void *aisa_Handle;unsigned aisa_Index;int aisa_X,aisa_Y;};
struct TagItem {ULONG ti_Tag;IPTR ti_Data;};
static unsigned locks,lockCalls,opens,drops,localClicks,externalClicks,pos,total,switchAt;
static struct Screen screenA={1},screenB={2};
static struct TileInputEvent events[4];
static ULONG LockIBase(ULONG arg){assert(!arg&&!locks);++locks;++lockCalls;return 93;}
static void UnlockIBase(ULONG token){assert(token==93&&locks==1);--locks;}
static void drop_tiles(struct TileService *service){assert(!locks);if(service->lift){++drops;service->lift=NULL;}}
static unsigned panelCloses,panelClicks;
static void close_panels(struct TileService *s){for(unsigned i=0;i<s->groupCount;i++)if(s->groups[i].panel){s->groups[i].panel=NULL;++panelCloses;}}
static void TileInputSetPanels(void *input,void *panels,unsigned count){(void)input;assert(!panels&&!count);}
static void TileLiftClose(void *panel){assert(panel);++panelCloses;}
static void group_click(struct TileService *s,unsigned group,int x,int y){assert(group==0&&s->groups[0].panel&&x==5&&y==7);++panelClicks;}
/* EXTRACT_HELPER */
static BOOL lift_tiles(struct TileService *service,int group){(void)group;
/* EXTRACT_OPEN */
 ++opens;service->lift=(void*)1;return TRUE;
}
static BOOL TileInputNext(void *input,struct TileInputEvent *out){(void)input;if(pos==total)return FALSE;if(pos==switchAt)intuition.FirstScreen=&screenB;*out=events[pos++];return TRUE;}
static BOOL TileLiftInput(void *lift,int x,int y,BOOL drop,struct TileLiftEvent *event){assert(lift&&!locks);event->type=drop?TILE_LIFT_DISMISS:TILE_LIFT_CLICK;event->id=x;event->x=11;event->y=y;return TRUE;}
static void activate_tile(struct TileService *service,struct TileEntry *tile,int x){assert(service->lift&&!locks&&tile==&service->tiles[0]&&x==11);++localClicks;}
static BOOL WorkbenchControlA(void *unused,struct TagItem *tags){(void)unused;assert(!locks&&tags[0].ti_Tag==117);struct AppIconSnapshotActivate *a=(void*)tags[0].ti_Data;assert(a->aisa_Handle==(void*)2&&a->aisa_Index==0&&a->aisa_X==11);++externalClicks;return TRUE;}
static void route(struct TileService *service){struct TileInputEvent input;struct TileLiftEvent event;
/* EXTRACT_ROUTE */
}
static void tick(struct TileService *service){
/* EXTRACT_TICK */
}
int main(void){struct TileService service={.screen=&screenA,.snapshot=(void*)2,.groupCount=1,.count=1};
 intuition.FirstScreen=&screenB;assert(!lift_tiles(&service,-1)&&!opens&&!service.lift&&!locks);
 intuition.FirstScreen=&screenA;assert(lift_tiles(&service,-1)&&opens==1&&service.lift);
 events[0]=(struct TileInputEvent){TILE_INPUT_CLICK,0,1,4};events[1]=(struct TileInputEvent){TILE_INPUT_CLICK,0,41,5};total=2;switchAt=99;route(&service);
 assert(localClicks==1&&externalClicks==1&&!drops);
 pos=0;switchAt=1;route(&service);assert(localClicks==2&&externalClicks==1&&drops==1&&!service.lift);
 /* Off-screen queued hotkeys also cannot reopen the old screen presenter. */
 events[0]=(struct TileInputEvent){TILE_INPUT_ALL,0,0,0};events[1]=(struct TileInputEvent){TILE_INPUT_GROUP,0,0,0};pos=0;switchAt=99;route(&service);
 assert(opens==1&&!service.lift);
 intuition.FirstScreen=&screenA;assert(lift_tiles(&service,-1));intuition.FirstScreen=&screenB;tick(&service);assert(!service.lift&&drops==2);
 intuition.FirstScreen=&screenA;service.groups[0]=(struct ServiceGroup){(void*)3,10,20};
 events[0]=(struct TileInputEvent){TILE_INPUT_PANEL_CLICK,0,15,27};pos=0;total=1;switchAt=99;route(&service);assert(panelClicks==1);
 intuition.FirstScreen=&screenB;pos=0;route(&service);assert(panelClicks==1&&panelCloses==1&&!service.groups[0].panel);
 assert(lockCalls>=8&&!locks);puts("tiles-front-host PASS");
}
