/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL;typedef uintptr_t IPTR;
#define TRUE 1
#define FALSE 0
#define TILE_LIMIT 40
#define WBDRAWER 2
#define NRF_SEND_MESSAGE 1
#define NRF_WAIT_REPLY 2
#define WBAPPICONA_Cells 1
#define WBAPPICONA_Surface 2
#define WBAPPICONA_DefinitionPath 3
#define WBAPPICONA_PropagatePosition 4
#define TAG_DONE 0
#define WBAPPICON_CELLS(w,h) ((w)+(h))
struct TileConfig {unsigned cellsW,cellsH,contract;char env[128];};
struct DiskObject {int do_Type;const char **do_ToolTypes;};
struct NotifyRequest {char *nr_Name;IPTR nr_UserData;unsigned nr_Flags;struct {struct {void *nr_Port;}nr_Msg;}nr_stuff;};
struct TagItem {IPTR a,b;};
struct TileEntry {struct TileConfig config;struct DiskObject *fallback;void *icon,*bitmap,*staticBitmap;int active,valid,healthy,watching,live,dirty,group;char definitionPath[512],id[128],envPath[132];struct NotifyRequest notify;unsigned sentinel;};
struct ServiceGroup {char id[128];unsigned members[40],count;};
struct TileService {unsigned count;struct TileEntry tiles[40];struct ServiceGroup groups[8];void *screen,*apps,*notifications;int sources;};
static struct TileEntry *pending;static struct DiskObject disk;static unsigned contract=2,requested;
static BOOL definition_path(const char *s,char *d,unsigned n){assert(strlen(s)<n);strcpy(d,s);return 1;}
static const char *FilePart(const char *s){const char *p=strrchr(s,'/');return p?p+1:s;}
static struct DiskObject *GetDiskObject(const char *s){(void)s;return &disk;}
static void FreeDiskObject(struct DiskObject *d){assert(d==&disk);}
static BOOL TileConfigParse(const char *const *tt,struct TileConfig *c,char *e,unsigned n){(void)tt;(void)e;(void)n;memset(c,0,sizeof(*c));c->cellsW=c->cellsH=1;c->contract=contract;return 1;}
static BOOL TileSourcePending(int *sources,struct TileEntry *t){(void)sources;return t==pending;}
static void EndNotify(struct NotifyRequest *n){(void)n;}
static void RemoveAppIcon(void *p){(void)p;}
static void FreeBitMap(void *p){(void)p;}
static void notifications(struct TileService *s){(void)s;}
static void problem(struct TileEntry *t,const char *e){(void)t;(void)e;assert(0);}
static BOOL cache_static(struct TileService *s,struct TileEntry *t){(void)s;(void)t;return 1;}
static void *AddAppIconA(unsigned id,unsigned flags,char *name,void *port,unsigned lock,struct DiskObject *d,struct TagItem *tags){(void)id;(void)flags;(void)name;(void)port;(void)lock;(void)d;(void)tags;return (void*)1;}
static BOOL StartNotify(struct NotifyRequest *n){(void)n;return 1;}
static unsigned now_ms(void){return 1;}
static void request_value(struct TileService *s,struct TileEntry *t,unsigned now){(void)s;(void)now;assert(t!=pending);++requested;}
/* EXTRACT */
static void setup(struct TileService *s){memset(s,0,sizeof(*s));s->count=1;pending=&s->tiles[0];pending->fallback=&disk;pending->valid=1;pending->config.cellsW=pending->config.cellsH=1;pending->config.contract=1;pending->sentinel=0x1234;strcpy(pending->definitionPath,"HOME:Tiles/old");requested=0;contract=2;}
int main(void){struct TileService s;setup(&s);load_tile(&s,"HOME:Tiles/new",-1);assert(s.count==2&&s.tiles[1].active&&requested==1);assert(!pending->active&&pending->sentinel==0x1234&&pending->config.contract==1);/* Same pathname but changed contract must not overwrite outstanding owner. */setup(&s);load_tile(&s,"HOME:Tiles/old",-1);assert(s.count==2&&pending->sentinel==0x1234&&pending->config.contract==1);/* Exact contract move preserves owner address without a second request. */setup(&s);contract=1;load_tile(&s,"HOME:Tiles/group/old",-1);assert(s.count==1&&pending->active&&pending->sentinel==0x1234&&!requested);/* Once reply releases ownership, slot is reusable. */setup(&s);pending=NULL;load_tile(&s,"HOME:Tiles/new",-1);assert(s.count==1&&s.tiles[0].active&&s.tiles[0].config.contract==2&&!s.tiles[0].sentinel&&requested==1);puts("tiles-pending-host PASS");}
