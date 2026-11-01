/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Distributed under the AROS Public License 1.1. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef int32_t LONG;typedef uint32_t ULONG;typedef uint8_t UBYTE;typedef int BOOL;
#define FALSE 0
#define TRUE 1
#define EXCLUSIVE_LOCK 1
#define ATTR_READ_ONLY 1
#define ATTR_ARCHIVE 32
#define ERROR_OBJECT_IN_USE 11
#define ERROR_WRITE_PROTECTED 12
#define ERROR_DISK_FULL 13
#define ERROR_UNKNOWN 14
#define AROS_WORD2LE(x) (x)
#define AROS_LONG2LE(x) (x)
#define D(x)
#define CHECK(x,why) do {if(!(x)){fprintf(stderr,"%s\n",why);exit(1);}}while(0)
struct Block {unsigned refs,dirty;};static struct Block blocks[4];
struct FSSuper {void *cache;};
struct IOHandle {struct FSSuper *sb;void *block;UBYTE *data;ULONG first_cluster;};
struct GlobalLock {ULONG size,attr,first_cluster,dir_cluster,dir_entry;LONG access;};
struct ExtFileLock {struct IOHandle ioh;struct GlobalLock *gl;ULONG pos;BOOL do_notify;};
struct Globals {int unused;};struct DirHandle {int open;};
struct DirEntry {struct {struct {ULONG file_size,first_cluster_lo,first_cluster_hi,attr;} entry;} e;};
static unsigned error_after_pin,zero_after_pin,directory_error;
static unsigned pinned(void) {unsigned n=0;for(unsigned i=0;i<4;++i)n+=blocks[i].refs;return n;}
static LONG pin(struct IOHandle *h) {
    CHECK(!h->block,"previous transfer retained cache borrow");
    for(unsigned i=0;i<4;++i)if(!blocks[i].refs){blocks[i].refs=1;h->block=&blocks[i];h->data=(UBYTE *)&blocks[i];return 0;}
    return ERROR_UNKNOWN;
}
static void Cache_FreeBlock(void *cache,void *block) {(void)cache;struct Block *b=block;CHECK(b->refs==1,"balanced cache release");--b->refs;}
static LONG ReadFileChunk(struct IOHandle *h,ULONG pos,ULONG want,UBYTE *data,ULONG *done) {
    (void)pos;LONG err=pin(h);if(err)return err;*done=error_after_pin||zero_after_pin?0:want;
    if(*done)memset(data,0x57,*done);return error_after_pin?ERROR_UNKNOWN:0;
}
static LONG WriteFileChunk(struct IOHandle *h,ULONG pos,ULONG want,UBYTE *data,ULONG *done) {
    (void)pos;(void)data;LONG err=pin(h);if(err)return err;((struct Block *)h->block)->dirty=1;
    *done=error_after_pin||zero_after_pin?0:want;return error_after_pin?ERROR_UNKNOWN:0;
}
static void InitDirHandle(struct FSSuper *sb,ULONG c,struct DirHandle *d,BOOL reuse,struct Globals *g) {(void)sb;(void)c;(void)reuse;(void)g;d->open=1;}
static LONG GetDirEntry(struct DirHandle *d,ULONG index,struct DirEntry *e,struct Globals *g) {(void)d;(void)index;(void)g;CHECK(!pinned(),"data borrow released before directory metadata");memset(e,0,sizeof(*e));return directory_error?ERROR_UNKNOWN:0;}
static LONG UpdateDirEntry(struct DirEntry *e,struct Globals *g) {(void)e;(void)g;return 0;}
static LONG ReleaseDirHandle(struct DirHandle *d,struct Globals *g) {(void)g;d->open=0;return 0;}
/* ACTUAL_OPERATIONS */
int main(void) {
    struct FSSuper sb={0};struct Globals g={0};struct ExtFileLock handles[32];struct GlobalLock owners[32];UBYTE byte=0;ULONG done;
    memset(handles,0,sizeof(handles));memset(owners,0,sizeof(owners));
    for(unsigned i=0;i<32;++i){owners[i].size=64;owners[i].access=EXCLUSIVE_LOCK;owners[i].attr=ATTR_ARCHIVE;handles[i].gl=&owners[i];handles[i].ioh.sb=&sb;handles[i].ioh.first_cluster=2;}
    for(unsigned round=0;round<2;++round)for(unsigned i=0;i<32;++i){CHECK(!OpRead(&handles[i],&byte,1,&done,&g),"idle readers exceed cache capacity");CHECK(done==1 && byte==0x57,"read content/position preserved");CHECK(!handles[i].ioh.block && !handles[i].ioh.data,"read releases borrow");}
    CHECK(!pinned(),"all read borrows released");
    error_after_pin=1;CHECK(OpRead(&handles[0],&byte,1,&done,&g)==ERROR_UNKNOWN,"read error propagated");CHECK(!pinned(),"error read releases borrow");error_after_pin=0;
    for(unsigned i=0;i<32;++i){CHECK(!OpWrite(&handles[i],&byte,1,&done,&g),"idle writers exceed cache capacity");CHECK(!pinned() && !handles[i].ioh.data,"write releases borrow");}
    error_after_pin=1;CHECK(OpWrite(&handles[0],&byte,1,&done,&g)==ERROR_UNKNOWN,"write error propagated");CHECK(!pinned(),"error write releases borrow");error_after_pin=0;
    zero_after_pin=1;CHECK(!OpWrite(&handles[0],&byte,1,&done,&g) && !done,"zero write preserved");CHECK(!pinned(),"zero write releases borrow");zero_after_pin=0;
    handles[0].pos=owners[0].size;directory_error=1;CHECK(OpWrite(&handles[0],&byte,1,&done,&g)==ERROR_UNKNOWN,"directory failure propagated");CHECK(!pinned(),"metadata failure leaves no data borrow");
    CHECK(blocks[0].dirty,"dirty state retained by cache");puts("PASS transfer cache borrows:32 idle handles over4 modeled ranges, errors/zero/metadata/dirty state");return 0;
}
