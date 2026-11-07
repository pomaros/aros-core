/* Copyright (C) 2026 John KNIPPER. APL 1.1. */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef int32_t LONG;typedef uintptr_t IPTR;typedef uint32_t ULONG;
#define BADDR(p) ((void *)(p))
#define D(x) ((void)0)
#define DOSTRUE (-1)
#define DOSFALSE 0
#define SHARED_LOCK (-2)
#define EXCLUSIVE_LOCK (-1)
#define ERROR_OBJECT_NOT_FOUND 205
#define ERROR_NO_MORE_ENTRIES 232
#define ForeachNode(list,node) for((node)=(list)->head;(node);(node)=(node)->next)
struct GlobalLock{struct GlobalLock *next;ULONG dir_cluster,dir_entry;LONG access;const char *name;unsigned size;};
struct List{struct GlobalLock *head;};struct Info{struct List locks;};struct SB{struct Info *info;};struct Globals{struct SB *sb;};
struct ExtFileLock{struct GlobalLock *gl;struct{ULONG first_cluster;}ioh;};
struct FileInfoBlock{ULONG fib_DiskKey;char name[32];unsigned size;};struct DirHandle{ULONG cur_index;};struct DirEntry{int unused;};
struct Packet{IPTR dp_Arg1,dp_Arg2;};
static unsigned initialized,released,locked,freed,filled;static LONG test_error,next_error,lock_error,fill_error;static struct GlobalLock *filled_gl;
static struct GlobalLock allocated_gl={.dir_cluster=42,.dir_entry=17,.access=SHARED_LOCK,.name="ordinary",.size=7};static struct ExtFileLock allocated_fl={.gl=&allocated_gl};
static LONG TestLock(struct ExtFileLock *fl,struct Globals *glob){(void)fl;(void)glob;return test_error;}
static void InitDirHandle(struct SB *sb,ULONG cluster,struct DirHandle *dh,int zero,struct Globals *glob){(void)sb;(void)glob;assert(cluster==42&&!zero);dh->cur_index=0;++initialized;}
#define FALSE 0
static LONG GetNextDirEntry(struct DirHandle *dh,struct DirEntry *de,struct Globals *glob){(void)de;(void)glob;assert(dh->cur_index==9);dh->cur_index=17;return next_error;}
static void ReleaseDirHandle(struct DirHandle *dh,struct Globals *glob){(void)dh;(void)glob;++released;}
static LONG LockFile(ULONG cluster,ULONG entry,LONG access,struct ExtFileLock **out,struct Globals *glob){++locked;assert(cluster==42&&entry==17&&access==SHARED_LOCK);for(struct GlobalLock *g=glob->sb->info->locks.head;g;g=g->next)if(g->dir_cluster==cluster&&g->dir_entry==entry&&g->access==EXCLUSIVE_LOCK)return 202;if(lock_error)return lock_error;*out=&allocated_fl;return 0;}
static LONG FillFIB(struct ExtFileLock *fl,struct FileInfoBlock *fib,struct Globals *glob){(void)glob;++filled;assert(fl&&fl->gl);filled_gl=fl->gl;if(fill_error)return fill_error;snprintf(fib->name,sizeof(fib->name),"%s",fl->gl->name);fib->size=fl->gl->size;return 0;}
static void FreeLock(struct ExtFileLock *fl,struct Globals *glob){(void)glob;assert(fl==&allocated_fl);++freed;}
static void operation(struct Globals *glob,struct Packet *pkt,IPTR *result,LONG *error){IPTR res=DOSFALSE;LONG err=0;do
#include "case.inc"
while(0);*result=res;*error=err;}
static void reset(void){initialized=released=locked=freed=filled=0;test_error=next_error=lock_error=fill_error=0;filled_gl=0;}
int main(void){struct GlobalLock own={.dir_cluster=42,.dir_entry=17,.access=EXCLUSIVE_LOCK,.name="writer",.size=1234};struct GlobalLock foreign={.dir_cluster=43,.dir_entry=17,.access=EXCLUSIVE_LOCK,.name="foreign",.size=99};struct GlobalLock other={.dir_cluster=42,.dir_entry=16,.access=EXCLUSIVE_LOCK,.name="other",.size=9};struct Info info={0};struct SB sb={&info};struct Globals glob={&sb};struct ExtFileLock parent={.ioh={42}};struct FileInfoBlock fib={0};struct Packet pkt={(IPTR)&parent,(IPTR)&fib};IPTR res;LONG err;
#define RUN() do{fib.fib_DiskKey=9;memset(fib.name,0,sizeof(fib.name));operation(&glob,&pkt,&res,&err);}while(0)
 reset();RUN();assert(res==DOSTRUE&&!err&&fib.fib_DiskKey==17&&!strcmp(fib.name,"ordinary")&&locked==1&&freed==1&&released==1);
 reset();info.locks.head=&own;RUN();assert(res==DOSTRUE&&!err&&fib.fib_DiskKey==17&&!strcmp(fib.name,"writer")&&fib.size==1234&&!locked&&!freed&&released==1&&own.access==EXCLUSIVE_LOCK&&filled_gl==&own);
 reset();own.access=SHARED_LOCK;RUN();assert(res==DOSTRUE&&!err&&!locked&&!freed&&own.access==SHARED_LOCK);own.access=EXCLUSIVE_LOCK;
 reset();info.locks.head=&foreign;foreign.next=&other;other.next=&own;RUN();assert(res==DOSTRUE&&!err&&filled_gl==&own&&!strcmp(fib.name,"writer")&&!locked&&!freed);
 reset();test_error=218;RUN();assert(res==DOSFALSE&&err==218&&!initialized&&!released&&!filled);
 reset();next_error=ERROR_OBJECT_NOT_FOUND;RUN();assert(res==DOSFALSE&&err==ERROR_NO_MORE_ENTRIES&&released==1&&!locked&&!filled&&fib.fib_DiskKey==9);
 reset();next_error=218;RUN();assert(res==DOSFALSE&&err==218&&released==1&&!filled);
 reset();fill_error=218;RUN();assert(res==DOSFALSE&&err==218&&!locked&&!freed&&released==1&&fib.fib_DiskKey==9&&own.access==EXCLUSIVE_LOCK);
 reset();info.locks.head=0;lock_error=103;RUN();assert(res==DOSFALSE&&err==103&&locked==1&&!freed&&released==1&&!filled);
 reset();fill_error=218;RUN();assert(res==DOSFALSE&&err==218&&locked==1&&freed==1&&released==1&&fib.fib_DiskKey==9);
 puts("PASS FAT enumeration preserves exclusive ownership, metadata, cursor, errors and cleanup");return 0;}
