/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL;typedef uint32_t ULONG;typedef int32_t LONG;typedef uintptr_t IPTR;
#define TRUE 1
#define FALSE 0
#define BMA_DEPTH 1
#define BMF_CLEAR 1
#define TAG_DONE 0
#define DRAW_INTERVAL 500
#define FAST_INTERVAL 33
#define TILE_LIMIT 40
#define APPICONQF_VISIBLE 1
#define APPICONQF_HOVERED 2
#define WBCTRLA_PublishAppIcon 1
#define WBCTRLA_RefreshAppIconSnapshot 2
#define WBAPPICONA_Cells 3
#define WBAPPICONA_Surface 4
#define WBAPPICONA_DefinitionPath 5
#define WBAPPICONA_PropagatePosition 6
#define WBAPPICONA_Regions 7
#define WBAPPICONA_Cadence 8
#define WBAPPICONA_Pointer 9
#define WBAPPICONA_HoverCadence 10
#define WBAPPICON_CELLS(w,h) ((w)*65536+(h))
#define TILE_LOG(...) ((void)0)
struct BitMap {int number;};struct AppIcon {int number;};struct DiskObject {int number;};
struct RastPort {struct BitMap *BitMap;};struct Screen {struct RastPort RastPort;};
struct TagItem {ULONG tag;IPTR value;};
struct AppIconPublish {struct AppIcon *aip_Icon;struct BitMap *aip_Surface;};
struct AppIconRegions {unsigned count;struct {int x,y,w,h;char name[16];} region[1];};
struct AppIconSnapshotEntry {struct AppIcon *ais_Icon;struct BitMap *ais_Bitmap;};
struct Config {unsigned cellsW,cellsH,pointerEnabled,hoverCadence;char label[32];};
struct Value {char text[32];int pointerX,pointerY,hovered;};
struct TileEntry {struct Config config;struct Value value;struct BitMap *bitmap,*staticBitmap;struct AppIcon *icon;struct DiskObject *fallback;int group;BOOL active,healthy,live,dirty,publishPending;ULONG lastFrame,lastHeartbeat,lastPublishAttempt;char id[32],definitionPath[64];};
struct ServiceGroup {struct AppIcon *icon;struct BitMap *bitmap;BOOL dirty;};
struct TileService {struct TileEntry tiles[2];struct ServiceGroup groups[1];unsigned count,groupCount,draws;ULONG lastFastDraw,lastDraw,snapshotTick,liftRefreshTick,snapshotCount;struct Screen *screen;void *apps,*lift,*snapshot;BOOL trace;int sources;};
static ULONG clockNow;static unsigned renders,publications,copies,paints;static BOOL accept,held;
static BOOL TileSourcePending(int*s,struct TileEntry*t){(void)s;(void)t;return held;}
static ULONG now_ms(void){return clockNow;}
static BOOL WorkbenchControlA(void*a,struct TagItem*t){(void)a;if(t->tag==WBCTRLA_RefreshAppIconSnapshot){++copies;return 1;}assert(t->tag==WBCTRLA_PublishAppIcon);struct AppIconPublish*p=(void*)t->value;assert(p->aip_Icon&&p->aip_Surface);++publications;return accept;}
static unsigned GetBitMapAttr(struct BitMap*b,int attr){(void)b;(void)attr;return 32;}
static struct BitMap *AllocBitMap(unsigned w,unsigned h,unsigned d,unsigned f,struct BitMap*b){(void)w;(void)h;(void)d;(void)f;return b;}
static void InitRastPort(struct RastPort*r){memset(r,0,sizeof(*r));}
static BOOL TileRender(struct RastPort*r,struct Config*c,struct Value*v,unsigned w,unsigned h){(void)c;(void)v;assert(r->BitMap&&w==64&&h==64);++renders;return 1;}
static void RemoveAppIcon(struct AppIcon*i){(void)i;assert(0);}
static struct AppIcon *AddAppIconA(unsigned a,unsigned b,const char*c,void*d,unsigned e,struct DiskObject*f,struct TagItem*g){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;assert(0);return NULL;}
static BOOL TileLiftUpdateBitmap(void*l,unsigned id,struct BitMap*b){(void)l;(void)id;assert(b);return 0;}
static void TileLiftRefreshItem(void*l,unsigned id){assert(l&&id);++paints;}
static ULONG presentation(struct TileService*s,struct TileEntry*t){(void)s;return t->value.hovered?3:1;}
static void drop_tiles(struct TileService*s){s->lift=NULL;}
#include "functions.inc"
static void retry(struct TileService *service,struct TileEntry *tile,ULONG now){ULONG delay=500;
#include "retry.inc"
(void)delay;}
static void refresh(struct TileService *service,ULONG now){ULONG delay=500;unsigned i;
#include "refresh.inc"
(void)delay;}
int main(void)
{
    struct BitMap live={1},fallback={2},foreign={3},groupBitmap={4};struct AppIcon own={1},other={2},groupIcon={3};
    struct TileService s={0};s.count=1;struct TileEntry*t=&s.tiles[0];t->config.cellsW=t->config.cellsH=1;t->config.hoverCadence=33;t->group=-1;t->icon=&own;t->bitmap=&live;t->staticBitmap=&fallback;t->active=t->healthy=t->live=t->dirty=1;
    clockNow=100;accept=0;assert(draw_tile(&s,t,clockNow,TRUE)==1);
    assert(renders==1&&s.draws==1&&s.lastFastDraw==100&&t->lastFrame==100&&!t->dirty&&t->publishPending);
    retry(&s,t,599);assert(publications==1&&renders==1);
    accept=1;held=1;retry(&s,t,600);assert(publications==1&&t->publishPending);held=0;retry(&s,t,600);assert(publications==2&&!t->publishPending&&renders==1&&t->lastHeartbeat==600);
    struct AppIconSnapshotEntry entry={&own,&foreign};assert(snapshot_lift_bitmap(&s,&entry)==&live);
    t->healthy=0;assert(snapshot_lift_bitmap(&s,&entry)==&fallback);
    entry.ais_Icon=&other;assert(snapshot_lift_bitmap(&s,&entry)==&foreign);
    s.groupCount=1;s.groups[0].icon=&groupIcon;s.groups[0].bitmap=&groupBitmap;entry.ais_Icon=&groupIcon;assert(snapshot_lift_bitmap(&s,&entry)==&groupBitmap);
    s.lift=s.snapshot=&s;s.snapshotCount=1;t->value.hovered=1;paints=0;
    clockNow=33;refresh(&s,33);assert(!copies&&paints==1);
    clockNow=66;refresh(&s,66);assert(!copies&&paints==2);
    clockNow=500;refresh(&s,500);assert(copies==1&&paints==3&&renders==1);
    puts("tiles-lift-cache PASS");
}
