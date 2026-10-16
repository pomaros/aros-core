/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define TRUE 1
#define FALSE 0
#define GROUP_LIMIT 8
struct Message {int region;};
struct NotifyRequest {int dummy;};
struct TileEntry {int active,valid,healthy,watching,live,dirty;void *icon,*bitmap,*staticBitmap,*fallback;struct NotifyRequest notify;unsigned sentinel;};
struct ServiceGroup {int watching,dirty,traced;void *panel,*icon,*bitmap,*fallback;struct NotifyRequest notify;struct {char lift[32];} config;};
struct TileService {void *apps,*input,*screen;unsigned count,groupCount,generation;int argc;char **argv;int reloadPending,catalogWatching;struct NotifyRequest catalogNotify;struct ServiceGroup groups[8];struct TileEntry tiles[40];};
static struct Message queue[8];static unsigned queued,replied,icons,bitmaps,disks,panels,inputs,lifts,snapshots,scans,unlocks;
static int scanned;
static struct Message *GetMsg(void *port){assert(port);return queued?&queue[--queued]:NULL;}
static void ReplyMsg(struct Message *m){assert(m->region==1);assert(!icons);assert(!scanned);++replied;}
static void EndNotify(struct NotifyRequest *n){(void)n;}
static int StartNotify(struct NotifyRequest *n){(void)n;return 1;}
static void RemoveAppIcon(void *icon){assert(icon&&icons);--icons;/* Model an already queued old Region and removal-time final message. */assert(queued<8);queue[queued++].region=1;}
static void FreeBitMap(void *p){assert(p&&bitmaps);assert(!lifts&&!snapshots&&!panels);--bitmaps;}
static void FreeDiskObject(void *p){assert(p&&disks);--disks;}
static void TileLiftClose(void *p){assert(p&&panels);--panels;}
static void TileInputClose(void *p){assert(p&&inputs);--inputs;}
static void *TileInputOpen(void *s,const char **keys,unsigned n){assert(s&&keys&&n==1);++inputs;return (void*)1;}
static void drop_tiles(struct TileService *s){(void)s;lifts=snapshots=0;}
static void close_panels(struct TileService *s){for(unsigned i=0;i<s->groupCount;i++)if(s->groups[i].panel){TileLiftClose(s->groups[i].panel);s->groups[i].panel=NULL;}}
static void notifications(struct TileService *s){(void)s;}
static void scan_catalog(struct TileService *s){assert(!queued&&replied==3&&!icons&&!panels&&!inputs&&!lifts&&!snapshots);scanned=1;++scans;s->groupCount=1;/* The new ID must never receive the retired message. */s->tiles[0].active=1;assert(s->tiles[0].sentinel==0x1234);}
static void load_path(struct TileService *s,const char *p){(void)s;(void)p;assert(0);}
static void UnlockPubScreen(void *name,void *screen){assert(!name&&screen);assert(!icons&&!bitmaps&&!inputs&&!panels&&!lifts&&!snapshots&&!queued);++unlocks;}
/* EXTRACT */
static void setup(struct TileService *s){memset(s,0,sizeof(*s));queued=replied=scans=unlocks=0;scanned=0;icons=2;bitmaps=3;disks=2;panels=inputs=lifts=snapshots=1;s->apps=s->input=s->screen=(void*)1;s->groupCount=s->count=1;s->argc=1;s->reloadPending=1;s->groups[0].panel=s->groups[0].icon=s->groups[0].bitmap=s->groups[0].fallback=(void*)1;s->groups[0].watching=1;s->tiles[0].icon=s->tiles[0].bitmap=s->tiles[0].staticBitmap=s->tiles[0].fallback=(void*)1;s->tiles[0].active=s->tiles[0].valid=s->tiles[0].healthy=1;s->tiles[0].sentinel=0x1234;queue[queued++].region=1;}
int main(void){struct TileService s;setup(&s);reload_catalog(&s);assert(scans==1&&replied==3&&!s.reloadPending&&s.tiles[0].sentinel==0x1234);assert(bitmaps==2&&disks==1&&inputs==1);setup(&s);release_screen(&s);assert(unlocks==1&&!s.screen&&replied==3&&!s.tiles[0].live&&s.tiles[0].dirty);assert(s.tiles[0].sentinel==0x1234);puts("tiles-reload-host PASS");}
