/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Distributed under the AROS Public License 1.1. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef int64_t QUAD;
typedef uint8_t UBYTE;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define EXCLUSIVE_LOCK 1
#define ATTR_READ_ONLY 1
#define ATTR_ARCHIVE 32
#define OFFSET_BEGINNING -1
#define OFFSET_CURRENT 0
#define OFFSET_END 1
#define ERROR_OBJECT_IN_USE 11
#define ERROR_WRITE_PROTECTED 12
#define ERROR_SEEK_ERROR 13
#define ERROR_NOT_A_DOS_DISK 14
#define ERROR_UNKNOWN 15
#define ERROR_DISK_FULL 16
#define AROS_WORD2LE(x) (x)
#define AROS_LONG2LE(x) (x)
#define NCLUSTERS 16
#define CLUSTERSIZE 512
#define EOC UINT32_C(0xffff)
struct FSSuper { ULONG clustersize,clustersize_bits,eoc_mark,clusters_count; int fat_io_error; };
struct IOHandle { struct FSSuper *sb; ULONG first_cluster; void *block; };
struct DirHandle { int open; };
struct Entry { ULONG first_cluster_lo,first_cluster_hi,file_size,attr; };
struct DirEntry { struct { struct Entry entry; } e; };
struct GlobalLock { int access; ULONG attr,size,dir_cluster,dir_entry,first_cluster; };
struct ExtFileLock { struct GlobalLock *gl; struct IOHandle ioh; ULONG pos; };
struct Globals { struct FSSuper *sb; };
static ULONG fat[NCLUSTERS];
static UBYTE bytes[NCLUSTERS][CLUSTERSIZE];
static struct DirEntry disk_entry;
static ULONG old_size, old_first, desired;
static int failure,writes,updates,allocations,releases,fail_at;
static struct IOHandle *caller;
static void reset(struct IOHandle *h) {
    assert(h->block==NULL || h->block==(void *)(uintptr_t)1);
    h->block=NULL;
}
#define RESET_HANDLE(h) reset(h)
#define FIRST_FILE_CLUSTER(de) ((de)->e.entry.first_cluster_lo | ((de)->e.entry.first_cluster_hi<<16))
static ULONG next_cluster(struct FSSuper *sb,ULONG cluster) {
    assert(cluster>=2 && cluster<NCLUSTERS);sb->fat_io_error=0;return fat[cluster];
}
#define GET_NEXT_CLUSTER(sb,c) next_cluster(sb,c)
static BOOL set_next(struct FSSuper *sb,ULONG cluster,ULONG next) {
    (void)sb;assert(cluster>=2 && cluster<NCLUSTERS);fat[cluster]=next;return TRUE;
}
#define SET_NEXT_CLUSTER(sb,c,n) set_next(sb,c,n)
static LONG FindFreeCluster(struct FSSuper *sb,ULONG *cluster) {
    (void)sb;
    if(failure==1 && allocations==fail_at)return ERROR_DISK_FULL;
    for(ULONG i=2;i<NCLUSTERS;i++)if(!fat[i]){*cluster=i;return 0;}
    return ERROR_DISK_FULL;
}
static BOOL AllocCluster(struct FSSuper *sb,ULONG cluster) {
    (void)sb;assert(!fat[cluster]);fat[cluster]=EOC;allocations++;return TRUE;
}
static void FreeCluster(struct FSSuper *sb,ULONG cluster) { (void)sb;fat[cluster]=0; }
static LONG FreeClusterChain(struct FSSuper *sb,ULONG cluster) {
    while(cluster>=2 && cluster<EOC-7){ULONG next=fat[cluster];FreeCluster(sb,cluster);cluster=next;}
    return 0;
}
static void InitDirHandle(struct FSSuper *sb,ULONG cluster,struct DirHandle *dh,BOOL b,struct Globals *g) {
    (void)sb;(void)cluster;(void)b;(void)g;dh->open=1;
}
static LONG GetDirEntry(struct DirHandle *dh,ULONG index,struct DirEntry *de,struct Globals *g) {
    (void)index;(void)g;assert(dh->open);*de=disk_entry;return 0;
}
static void ReleaseDirHandle(struct DirHandle *dh,struct Globals *g) { (void)g;assert(dh->open);dh->open=0;releases++; }
static UBYTE *at(ULONG first,ULONG pos) {
    ULONG steps=pos/CLUSTERSIZE;
    while(steps--){assert(first>=2 && first<NCLUSTERS);first=fat[first];}
    assert(first>=2 && first<NCLUSTERS);return &bytes[first][pos%CLUSTERSIZE];
}
static LONG WriteFileChunk(struct IOHandle *h,ULONG pos,ULONG length,UBYTE *src,ULONG *written) {
    assert(h!=caller && length && length<=512 && disk_entry.e.entry.file_size==old_size);
    writes++;h->block=(void *)(uintptr_t)1;
    if((failure==2 || failure==3) && writes==fail_at){*written=0;return failure==2?ERROR_UNKNOWN:0;}
    ULONG count=length>137?137:length;
    for(ULONG i=0;i<count;i++)*at(h->first_cluster,pos+i)=src[i];
    *written=count;return 0;
}
static LONG UpdateDirEntry(struct DirEntry *de,struct Globals *g) {
    (void)g;updates++;
    if(de->e.entry.file_size>old_size) {
        ULONG first=FIRST_FILE_CLUSTER(de);
        for(ULONG i=0;i<old_size;i++)assert(*at(first,i)==0x7b && "resize preserves the original prefix");
        for(ULONG i=old_size;i<de->e.entry.file_size;i++)assert(*at(first,i)==0 && "zero before size publication");
    }
    if(failure==4)return ERROR_UNKNOWN;
    disk_entry=*de;return 0;
}
/* RESIZE */
static void setup(struct FSSuper *sb,struct GlobalLock *gl,struct ExtFileLock *lock,ULONG size) {
    memset(fat,0,sizeof(fat));memset(bytes,0xa5,sizeof(bytes));memset(&disk_entry,0,sizeof(disk_entry));
    *gl=(struct GlobalLock){.access=EXCLUSIVE_LOCK,.size=size,.first_cluster=size?2:0xffffffff};
    *lock=(struct ExtFileLock){.gl=gl,.pos=1900,.ioh={.sb=sb,.first_cluster=gl->first_cluster}};
    ULONG count=(size+CLUSTERSIZE-1)/CLUSTERSIZE;
    for(ULONG i=0;i<count;i++)fat[2+i]=i+1<count?3+i:EOC;
    for(ULONG i=0;i<size;i++)*at(2,i)=0x7b;
    disk_entry.e.entry.first_cluster_lo=size?2:0;disk_entry.e.entry.file_size=size;
    old_size=size;old_first=size?2:0;caller=&lock->ioh;writes=updates=allocations=releases=0;
}
int main(void) {
    struct FSSuper sb={.clustersize=512,.clustersize_bits=9,.eoc_mark=EOC,.clusters_count=NCLUSTERS-2};
    struct Globals g={&sb};struct GlobalLock gl;struct ExtFileLock lock;
    const ULONG sizes[]={0,9,511,512,513,1031};
    for(unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++) {
        for(failure=0;failure<=4;failure++)for(fail_at=1;fail_at<=3;fail_at++) {
            setup(&sb,&gl,&lock,sizes[i]);desired=1801;LONG result=-99;
            LONG err=OpSetFileSize(&lock,desired,OFFSET_BEGINNING,&result,&g);
            assert(lock.pos==1900 && lock.ioh.block==NULL && releases==1);
            if(!err)assert(result==(LONG)desired && gl.size==desired && disk_entry.e.entry.file_size==desired);
            else {
                assert(result==-99 && gl.size==old_size && disk_entry.e.entry.file_size==old_size);
                ULONG count=(old_size+511)/512;
                for(ULONG c=2;c<NCLUSTERS;c++)assert(fat[c]==(c<2+count?(c+1<2+count?c+1:EOC):0));
                for(ULONG p=0;p<old_size;p++)assert(*at(old_first,p)==0x7b);
            }
        }
    }
    failure=0;setup(&sb,&gl,&lock,1031);LONG result;
    assert(!OpSetFileSize(&lock,9,OFFSET_BEGINNING,&result,&g));
    old_size=9;assert(!OpSetFileSize(&lock,1600,OFFSET_BEGINNING,&result,&g));
    for(ULONG p=9;p<1600;p++)assert(*at(2,p)==0);
    int before=updates;assert(!OpSetFileSize(&lock,1600,OFFSET_BEGINNING,&result,&g) && updates==before);
    assert(OpSetFileSize(&lock,-1,OFFSET_BEGINNING,&result,&g)==ERROR_SEEK_ERROR);
    gl.access=0;assert(OpSetFileSize(&lock,2,OFFSET_BEGINNING,&result,&g)==ERROR_OBJECT_IN_USE);
    gl.access=EXCLUSIVE_LOCK;gl.attr=ATTR_READ_ONLY;
    assert(OpSetFileSize(&lock,2,OFFSET_BEGINNING,&result,&g)==ERROR_WRITE_PROTECTED);
    puts("PASS: actual FAT resize zeroes before publication, preserves position/prefix and rolls back failed growth");
}
