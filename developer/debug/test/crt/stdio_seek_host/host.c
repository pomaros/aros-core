/* Copyright (C) 2026 John KNIPPER. AROS Public License 1.1. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
typedef int32_t LONG;
typedef int64_t QUAD;
typedef int64_t off64_t;
typedef uintptr_t BPTR;
#define BADDR(v) ((void *)(v))
#define OFFSET_BEGINNING -1
#define OFFSET_CURRENT 0
#define OFFSET_END 1
#define ERROR_INVALID_LOCK 211
#define ERROR_OBJECT_TOO_LARGE 207
/* @FHFLAGS@ */
#define _FCB_ISDIR 1
#define __POSIXC_STDIO_EOF 1
struct FileHandle { LONG fh_Pos, fh_End; uint32_t fh_Flags; };
struct Library { int unused; };
struct Dos64Base { int unused; };
typedef struct { BPTR handle; int privflags; } fcb;
typedef struct { fcb *fcb; } fdesc;
typedef struct { int fd, flags; } MockFile;
static struct FileHandle handle;
static fcb file;
static fdesc descriptor;
static MockFile stream;
static struct Library wide_library;
static struct Dos64Base wide_base;
static QUAD position, length;
static int wide, locks, flush_failure, seeks, flushes, valid, checks;
static LONG error;
static void require(int ok, const char *name)
{
    if (!ok) { fprintf(stderr, "FAIL: %s\n", name); exit(1); }
    checks++;
}
static void SetIoErr(LONG n) { error=n; }
static LONG IoErr(void) { return error; }
static int __stdc_ioerr2errno(LONG n) { return n==207 ? EOVERFLOW : EIO; }
static fdesc *__getfdesc(int fd) { return valid && fd==3 ? &descriptor : NULL; }
static void __fcb_lock(fcb *f) { locks++; }
static void __fcb_unlock(fcb *f) { require(locks>0,"balanced lock"); locks--; }
static LONG Flush(BPTR h)
{
    struct FileHandle *f=BADDR(h); flushes++;
    if (f->fh_Flags & FHF_WRITE) {
        if (flush_failure) { SetIoErr(222); return 0; }
        position+=f->fh_Pos; f->fh_Pos=0; return 1;
    }
    if (f->fh_Pos<f->fh_End) {
        LONG offset=f->fh_Pos-f->fh_End;
        f->fh_Pos=f->fh_End=0;
        if (position>INT32_MAX) { SetIoErr(207); return 0; }
        position+=offset;
    }
    return 1;
}
static QUAD dos64_Seek(struct Dos64Base *b,struct FileHandle *f,QUAD offset,LONG mode)
{
    QUAD old=position, target;
    seeks++;
    target=mode==OFFSET_CURRENT ? position+offset : mode==OFFSET_END ? length+offset : offset;
    if(target<0) { SetIoErr(219); return -1; }
    position=target; return old;
}
/* @BUFFERED@ */
static QUAD Seek64(BPTR h,LONG mode,QUAD offset) { return dos64_SeekBuffered(&wide_base,h,offset,mode); }
static QUAD GetFilePosition64(BPTR h)
{
    struct FileHandle *f=BADDR(h);
    return position+((f->fh_Flags&FHF_WRITE) ? f->fh_Pos : f->fh_Pos-f->fh_End);
}
static LONG Seek(BPTR h,LONG offset,LONG mode)
{
    struct FileHandle *f=BADDR(h);
    /* The legacy function ignores write-flush failure. */
    if(f->fh_Flags&FHF_WRITE) (void)Flush(h);
    if(position>INT32_MAX) { SetIoErr(207); return -1; }
    return (LONG)dos64_Seek(&wide_base,f,offset,mode);
}
/* @RAW_SEEK@ */
/* @RAW_TELL@ */
static QUAD __dos64_seek(fcb *f,QUAD offset,LONG mode)
{ return __dos64_rawseek(wide ? &wide_library : NULL,f->handle,offset,mode); }
static QUAD __dos64_getpos(fcb *f)
{ return __dos64_rawgetpos(wide ? &wide_library : NULL,f->handle); }
/* @SEEK@ */
/* @TELL@ */
static void reset(void)
{
    memset(&handle,0,sizeof(handle)); file.handle=(BPTR)&handle; file.privflags=0;
    descriptor.fcb=&file; stream.fd=3; stream.flags=__POSIXC_STDIO_EOF;
    position=0; length=INT64_C(3000000000); wide=valid=1;
    locks=flush_failure=seeks=flushes=error=errno=0;
}
int main(void)
{
    reset(); position=INT64_C(2605440512)+1024; handle.fh_Pos=1; handle.fh_End=1024;
    require(__fseeko64(&stream,28,SEEK_CUR)==0,"wide relative seek");
    require(position==INT64_C(2605440541) && !flushes && !handle.fh_End && !stream.flags && !locks,"wide logical position");
    reset(); position=INT64_C(2605440512)+1024; handle.fh_Pos=1; handle.fh_End=1024;
    require(__ftello64(&stream)==INT64_C(2605440513) && handle.fh_End==1024 && handle.fh_Pos==1 && !flushes,"tell preserves buffer");
    reset(); handle.fh_Flags=FHF_WRITE; handle.fh_Pos=17;
    require(__ftello64(&stream)==17 && !flushes && handle.fh_Pos==17,"tell pending writes");
    reset(); handle.fh_Flags=FHF_WRITE; handle.fh_Pos=17; flush_failure=1;
    require(__fseeko64(&stream,10,SEEK_SET)==-1 && errno==EIO && !seeks && position==0 && handle.fh_Pos==17 && stream.flags && !locks,"wide flush failure");
    flush_failure=0;
    require(__fseeko64(&stream,10,SEEK_SET)==0 && position==10 && handle.fh_Pos==0,"wide flush retry");
    reset(); wide=0; handle.fh_Flags=FHF_WRITE; handle.fh_Pos=17; flush_failure=1;
    require(__fseeko64(&stream,10,SEEK_SET)==-1 && errno==EIO && !seeks && handle.fh_Pos==17,"narrow flush failure");
    reset(); wide=0; handle.fh_Flags=FHF_WRITE; handle.fh_Pos=17; flush_failure=1;
    require(__ftello64(&stream)==-1 && errno==EIO && !seeks,"narrow tell flush failure");
    reset(); wide=0; position=1024; handle.fh_Pos=1; handle.fh_End=1024;
    require(__fseeko64(&stream,28,SEEK_CUR)==0 && position==29,"narrow read seek");
    reset(); wide=0;
    require(__fseeko64(&stream,INT64_C(2605440512),SEEK_SET)==-1 && errno==EOVERFLOW && !seeks,"narrow overflow");
    reset();
    require(__fseeko64(&stream,-10,SEEK_END)==0 && position==length-10,"wide end seek");
    reset(); valid=0;
    require(__fseeko64(&stream,0,SEEK_SET)==-1 && errno==EBADF && !seeks,"seek invalid descriptor");
    require(__ftello64(&stream)==-1 && errno==EBADF,"tell invalid descriptor");
    reset(); file.privflags=_FCB_ISDIR;
    require(__fseeko64(&stream,0,SEEK_SET)==-1 && errno==EISDIR && !seeks,"seek directory");
    require(__ftello64(&stream)==-1 && errno==EISDIR,"tell directory");
    reset();
    require(__fseeko64(&stream,0,999)==-1 && errno==EINVAL && !seeks && !locks,"invalid whence");
    printf("%d stdio buffered-seek checks PASS\n",checks);
    return 0;
}
