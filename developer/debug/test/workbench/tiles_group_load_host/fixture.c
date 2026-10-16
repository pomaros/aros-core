/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tiles_group.h"
typedef int BOOL;typedef int BPTR;
#define TRUE 1
#define FALSE 0
#define TILE_LIMIT 40
#define GROUP_LIMIT 8
#define DOS_FIB 1
#define ACCESS_READ 1
#define NO_ICON_POSITION 0x8000000
#define ERROR_NO_MORE_ENTRIES 232
struct DiskObject {const char *const *do_ToolTypes;int do_CurrentX,do_CurrentY;};
struct FileInfoBlock {int fib_DirEntryType;char fib_FileName[256];};
struct NotifyRequest {char *nr_Name;unsigned nr_Flags;struct {struct {void *nr_Port;} nr_Msg;} nr_stuff;};
#define NRF_SEND_MESSAGE 1
#define NRF_WAIT_REPLY 2
static int StartNotify(struct NotifyRequest *n){(void)n;return 1;}
struct TileEntry {int active,valid;struct {unsigned cellsW,cellsH;} config;struct DiskObject *fallback;};
struct ServiceGroup {char id[128],path[512];struct DiskObject *fallback;struct NotifyRequest notify;int dirty,watching;unsigned members[40];struct TileGroupConfig config;struct TileGroupLayout layout;unsigned first,count;int preferredX,preferredY;};
struct TileService {void *notifications;unsigned count,groupCount;struct TileEntry tiles[40];struct ServiceGroup groups[8];};
static const char *types[]={"TILE=group","SHOW=lifted",NULL};
static struct DiskObject disk={types,NO_ICON_POSITION,NO_ICON_POSITION};
static struct FileInfoBlock fib;
static unsigned entries[2],position,phase,loads;
static int failAt[2],lastError,failSecondExamine;
static unsigned locks,fibs,disks;
static BPTR Lock(const char *path,int mode){(void)path;(void)mode;++locks;return 1;}
static void UnLock(BPTR lock){assert(lock&&locks);--locks;}
static void *AllocDosObject(int type,void *tags){(void)type;(void)tags;++fibs;return &fib;}
static void FreeDosObject(int type,void *data){(void)type;assert(data==&fib&&fibs);--fibs;}
static int Examine(BPTR lock,struct FileInfoBlock *f){(void)lock;++phase;position=0;f->fib_DirEntryType=1;return !(phase==2&&failSecondExamine);}
static int ExNext(BPTR lock,struct FileInfoBlock *f){(void)lock;assert(phase==1||phase==2);
 if(failAt[phase-1]>=0&&position==(unsigned)failAt[phase-1]){lastError=99;return 0;}
 if(position==entries[phase-1]){lastError=ERROR_NO_MORE_ENTRIES;return 0;}
 snprintf(f->fib_FileName,sizeof(f->fib_FileName),"tile%u.info",position++);f->fib_DirEntryType=-1;return 1;
}
static int NameFromLock(BPTR lock,char *out,unsigned cap){(void)lock;assert(cap>20);strcpy(out,"HOME:Tiles/Control");return 1;}
static int IoErr(void){return lastError;}
static struct DiskObject *GetDiskObject(const char *p){(void)p;++disks;return &disk;}
static void FreeDiskObject(struct DiskObject *d){assert(d==&disk&&disks);--disks;}
static const char *FilePart(const char *s){const char *last=strrchr(s,'/');return last?last+1:s;}
static void load_tile(struct TileService *s,const char *path,int group){(void)path;assert(group>=0);++loads;
 unsigned slot=0;while(slot<s->count&&s->tiles[slot].active)slot++;
 if(slot==40)return;if(slot==s->count)s->count++;
 struct TileEntry *t=&s->tiles[slot];t->active=t->valid=1;t->config.cellsW=t->config.cellsH=1;t->fallback=&disk;
 s->groups[group].members[s->groups[group].count++]=slot;
}
/* EXTRACT_LOAD_PATH */
static void reset(unsigned first,unsigned second){entries[0]=first;entries[1]=second;phase=position=loads=0;failAt[0]=failAt[1]=-1;lastError=failSecondExamine=0;assert(!locks&&!fibs&&!disks);}
static void clean(struct TileService *s){for(unsigned g=0;g<s->groupCount;g++)if(s->groups[g].fallback){FreeDiskObject(s->groups[g].fallback);s->groups[g].fallback=NULL;}assert(!locks&&!fibs&&!disks);}
int main(void){struct TileService service;
 memset(&service,0,sizeof(service));reset(40,40);load_path(&service,"HOME:Tiles/Control");
 assert(service.count==40&&service.groupCount==1&&service.groups[0].layout.count==40&&loads==40);clean(&service);
 memset(&service,0,sizeof(service));reset(41,41);load_path(&service,"HOME:Tiles/Control");
 assert(!service.count&&!service.groupCount&&!loads);clean(&service);
 memset(&service,0,sizeof(service));service.count=39;for(unsigned n=0;n<39;n++)service.tiles[n].active=1;reset(2,2);load_path(&service,"HOME:Tiles/Control");
 assert(service.count==39&&!service.groupCount&&!loads);clean(&service);
 memset(&service,0,sizeof(service));reset(5,5);failAt[0]=3;load_path(&service,"HOME:Tiles/Control");
 assert(!service.count&&!service.groupCount&&!loads);clean(&service);
 memset(&service,0,sizeof(service));reset(5,5);failSecondExamine=1;load_path(&service,"HOME:Tiles/Control");
 assert(!service.count&&!service.groupCount&&!loads);clean(&service);
 memset(&service,0,sizeof(service));reset(5,5);failAt[1]=3;load_path(&service,"HOME:Tiles/Control");
 assert(service.count==3&&service.groupCount==0&&!service.groups[0].layout.count);
 for(unsigned i=0;i<service.count;++i)assert(!service.tiles[i].active);clean(&service);
 /* Directory grows after preflight: no truncated liftable layout is accepted. */
 memset(&service,0,sizeof(service));reset(2,41);load_path(&service,"HOME:Tiles/Control");
 assert(service.count==40&&service.groupCount==0&&!service.groups[0].layout.count);
 for(unsigned i=0;i<service.count;++i)assert(!service.tiles[i].active);clean(&service);
 /* Failed partial loads retain entries for normal resource cleanup, but not presentation. */
 memset(&service,0,sizeof(service));service.groupCount=1;strcpy(service.groups[0].id,"Control");reset(2,2);
 load_path(&service,"HOME:Tiles/Control");assert(service.groupCount==1&&!service.count&&!loads);clean(&service);
 /* Retired slots do not consume active capacity; membership uses stable indices. */
 memset(&service,0,sizeof(service));service.count=40;service.tiles[0].active=1;reset(2,2);
 load_path(&service,"HOME:Tiles/Control");assert(service.groupCount==1&&service.count==40&&service.groups[0].count==2&&service.groups[0].members[0]==1&&service.groups[0].members[1]==2);clean(&service);
 puts("tiles-group-load-host PASS");
}
