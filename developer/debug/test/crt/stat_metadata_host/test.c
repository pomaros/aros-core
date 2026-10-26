/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1.
 * HOST models for actual classic metadata filling/path lookup bodies.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#define off_t int32_t
#define dev_t uint64_t
typedef int64_t __off64_t;
typedef int64_t QUAD;
typedef uintptr_t BPTR;
typedef uint16_t UWORD;
typedef uint32_t ULONG;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define BNULL 0
#define BADDR(p) ((void *)(uintptr_t)(p))
#define MEMF_ANY 1
#define MEMF_PUBLIC 2
#define MEMF_CLEAR 4
#define SHARED_LOCK -2
#define __DOS64_EXAMINE 1
#define __DOS64_EXAMINE_NEXT 2
#define ERROR_LINE_TOO_LONG 120
#define ERROR_NOT_IMPLEMENTED 236
#define ERROR_ACTION_NOT_KNOWN 209
#define ERROR_NO_MORE_ENTRIES 232
#define TICKS_PER_SECOND 50
#define OFFSET_FROM_1970 252460800
#define ST_PIPEFILE -9
#define ST_ROOT 1
#define ST_USERDIR 2
#define ST_LINKDIR 4
#define ST_SOFTLINK 3
#define ST_FILE -3
#define ST_LINKFILE -4
#define S_IFCHR 0x1000
#define S_IFDIR 0x2000
#define S_IFLNK 0x4000
#define S_IFREG 0x8000
#define FIELDS uint64_t st_dev,st_ino; int64_t st_size,st_atime,st_ctime,st_mtime; struct {long tv_nsec;} st_atim,st_ctim,st_mtim; unsigned st_uid,st_gid,st_mode,st_nlink; int64_t st_blksize,st_blocks
struct stat { FIELDS; };
struct stat64 { FIELDS; };
struct DateStamp { int32_t ds_Days,ds_Minute,ds_Tick; };
struct FileInfoBlock64 { uint64_t fib_Size,fib_NumBlocks; struct DateStamp fib_Date; UWORD fib_OwnerUID,fib_OwnerGID; ULONG fib_Protection; int fib_DirEntryType; char fib_FileName[64]; };
struct FileLock { uint64_t fl_Volume; };
struct InfoData { int id_BytesPerBlock; };
static struct FileLock model_lock={77};
static unsigned allocations,owned,fail_allocation,locks,nexts;
static int dos_error,unsupported,examine_error,add_error;
static BPTR current_dir=500;
static uint64_t provider_size=123;
static void *AllocVec(size_t size,unsigned flags) {(void)flags;if(++allocations==fail_allocation)return NULL;void*p=calloc(1,size);assert(p);++owned;return p;}
static void FreeVec(void *p) {if(!p)return;assert(owned);--owned;free(p);}
static int IoErr(void) {return dos_error;}
static int __stdc_ioerr2errno(int error) {return error==101?EACCES:EIO;}
static BPTR CurrentDir(BPTR value) {BPTR old=current_dir;current_dir=value;return old;}
static int NameFromLock(BPTR lock,char *out,int size) {assert(lock==500);const char *s="VOL:cwd";if(size<=(int)strlen(s)){dos_error=ERROR_LINE_TOO_LONG;return 0;}strcpy(out,s);return 1;}
static int AddPart(char *out,const char *name,int size) {if(add_error){dos_error=101;return 0;}assert(strlen(out)+strlen(name)+2<=(size_t)size);strcat(out,"/");strcat(out,name);return 1;}
static char *FilePart(char *s) {char*a=strrchr(s,'/'),*b=strrchr(s,':');if(!a || (b&&b>a))a=b;return a?a+1:s;}
static BPTR Lock(const char *s,int mode) {assert(s&&mode==SHARED_LOCK&&!locks);++locks;return (BPTR)(uintptr_t)&model_lock;}
static void UnLock(BPTR lock) {assert(lock==(BPTR)(uintptr_t)&model_lock&&locks==1);--locks;}
static int __dos64_examine(BPTR lock,struct FileInfoBlock64 *fib,int operation) {
 assert(lock==(BPTR)(uintptr_t)&model_lock);
 if(operation==__DOS64_EXAMINE)nexts=0;
 else if(nexts++){dos_error=ERROR_NO_MORE_ENTRIES;return 0;}
 if(examine_error){dos_error=101;return 0;}
 if(unsupported){dos_error=ERROR_NOT_IMPLEMENTED;return 0;}
 memset(fib,0,sizeof *fib);fib->fib_Size=provider_size;fib->fib_Date.ds_Tick=73;
 fib->fib_DirEntryType=operation==__DOS64_EXAMINE?ST_ROOT:ST_FILE;strcpy(fib->fib_FileName,"file");return 1;
}
static int stricmp(const char*a,const char*b) {return strcasecmp(a,b);}
static int __stdc_gmtoffset(void) {return 0;}
static unsigned __id_a2u(UWORD id) {return id;}
static unsigned __prot_a2u(ULONG protection) {return protection;}
static int Info(BPTR lock,struct InfoData *info) {assert(lock==(BPTR)(uintptr_t)&model_lock);info->id_BytesPerBlock=4096;return 1;}
static void hashlittle2(const void *data,size_t size,uint32_t *a,uint32_t *b) {assert(data&&size);*a=1;*b=2;}
/* PRODUCTION */
static void reset(void) {assert(!owned&&!locks&&current_dir==500);allocations=fail_allocation=nexts=0;provider_size=123;dos_error=unsupported=examine_error=add_error=0;errno=0;}
int main(int argc,char **argv) {
 assert(argc==2);
 if(!strcmp(argv[1],"size")) {
  struct stat64 s={0};struct FileInfoBlock64 fib={0};fib.fib_Size=UINT64_C(0x10000007b);fib.fib_DirEntryType=ST_FILE;
  __fill_stat64buffer(&s,"VOL:file",&fib,0,0);assert(s.st_size==INT64_C(0x10000007b));
 } else if(!strcmp(argv[1],"path-size")) {
  struct stat64 b={0};reset();provider_size=UINT64_C(0x10000007b);
  assert(__stat64_from_path("VOL:file",&b)==0&&b.st_size==INT64_C(0x10000007b)&&!owned&&!locks);
 } else for(unsigned wide=0;wide<2;wide++) {
  struct stat a={0};struct stat64 b={0};reset();
  if(!strcmp(argv[1],"root")) {
   assert((wide?__stat64_from_path("VOL:",&b):__stat_from_path("VOL:",&a))==0);
   assert((wide?b.st_mode:a.st_mode)&S_IFDIR);
  } else if(!strcmp(argv[1],"fallback")) {
   unsupported=1;assert((wide?__stat64_from_path("VOL:file",&b):__stat_from_path("VOL:file",&a))==0);
   assert((wide?b.st_size:a.st_size)==0);
  } else if(!strcmp(argv[1],"allocation")) {
   assert((wide?__stat64_from_path("file",&b):__stat_from_path("file",&a))==0);assert(!owned&&!locks);
   unsigned total=allocations;
   for(unsigned cut=1;cut<=total;cut++) {
    reset();fail_allocation=cut;
    assert((wide?__stat64_from_path("file",&b):__stat_from_path("file",&a))==-1);
    assert(errno==ENOMEM&&!owned&&!locks&&current_dir==500);
   }
  } else if(!strcmp(argv[1],"missing")) {
   assert((wide?__stat64_from_path("VOL:absent",&b):__stat_from_path("VOL:absent",&a))==-1);
   assert(errno==ENOENT&&!owned&&!locks);
  } else if(!strcmp(argv[1],"provider")) {
   examine_error=1;assert((wide?__stat64_from_path("VOL:file",&b):__stat_from_path("VOL:file",&a))==-1);
   assert(errno==EACCES&&!owned&&!locks);
  } else if(!strcmp(argv[1],"join")) {
   add_error=1;assert((wide?__stat64_from_path("file",&b):__stat_from_path("file",&a))==-1);
   assert(errno==EACCES&&!owned&&!locks);
  } else assert(!"unknown scenario");
  assert(!owned&&!locks&&current_dir==500);
 }
 puts("PASS actual classic metadata scenario");return 0;
}
