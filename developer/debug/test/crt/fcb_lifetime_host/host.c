/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
/* Actual lifetime, read/write/close/dup functions with DOS and table boundaries. */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
typedef int64_t QUAD;
typedef int BOOL;
typedef long LONG;
typedef intptr_t BPTR;
typedef intptr_t SIPTR;
typedef uintptr_t IPTR;
typedef void *APTR;
#define TRUE 1
#define FALSE 0
#define D(x) do {} while (0)
#define __FDH_READ 1
#define __FDH_WRITE 2
#define __FDH_CLOSE 3
#define __FDH_FCNTL 5
#define __FDH_DUP 6
#define O_WRONLY 1
#define O_RDWR 2
#define F_GETFD 1
#define FD_OWNER_POSIXC 1
#define FD_CLOEXEC 1
#define STDERR_FILENO 2
#define _FCB_ISDIR 1
#define _FCB_DONTCLOSE_FH 2
struct reclock_file;
struct SignalSemaphore { pthread_mutex_t lock; };
struct Library { int unused; };
struct PosixCIntBase { int unused; };
struct __fd_namespace { struct Library *registry; };
static struct __fd_namespace namespace_boundary;
static struct __fd_namespace *__fd_space(struct PosixCIntBase *base) {
    (void)base;return &namespace_boundary;
}
/* FCB_TYPES */
/* FDESC_TYPES */
static _Thread_local int metadata_pause, metadata_cleanup, metadata_file_depth;
static _Thread_local fcb *metadata_original;
static void pause_metadata(void);
static struct SignalSemaphore *metadata_table_guard;
static void ObtainSemaphore(struct SignalSemaphore *s) { if(metadata_pause && s!=metadata_table_guard)pause_metadata(); assert(!pthread_mutex_lock(&s->lock)); if(metadata_original && s==&metadata_original->io_lock)metadata_file_depth++; }
static void ReleaseSemaphore(struct SignalSemaphore *s) { if(metadata_original && s==&metadata_original->io_lock)metadata_file_depth--; assert(!pthread_mutex_unlock(&s->lock)); }
/* FCB_LOCK */
/* FCB_UNLOCK */
/* FCB_RETAIN */
#define SLOTS 16
static fdesc *table[SLOTS];
static struct SignalSemaphore table_guard, reclock_sem;
static _Thread_local int table_depth;
static struct PosixCIntBase base;
static struct PosixCIntBase *__cwd_runtime_base(void) { return &base; }
#define FD_LOCK(b) do { (void)(b); __fdesc_lock(); } while (0)
#define FD_UNLOCK(b) do { (void)(b); __fdesc_unlock(); } while (0)
static atomic_int handle_closes[64], handle_unlocks[64], block_frees, record_puts, record_closes;
static int fail_slot;
static _Thread_local LONG native_error;
static void __fdesc_lock(void) { ObtainSemaphore(&table_guard); table_depth++; }
static void __fdesc_unlock(void) { assert(table_depth); table_depth--; ReleaseSemaphore(&table_guard); if(metadata_pause)pause_metadata(); }
static fdesc *__getfdesc(int fd) { return fd>=0 && fd<SLOTS ? table[fd] : NULL; }
static void __setfdesc(int fd, fdesc *d) { assert(table_depth && fd>=0 && fd<SLOTS); table[fd]=d; }
static void __set_errno(struct PosixCIntBase *b,int e) { assert(b==&base); errno=e; }
static int __fdlib_available(struct PosixCIntBase *b) { assert(b==&base); return FALSE; }
static fdesc *__alloc_fdesc(void) { return calloc(1,sizeof(fdesc)); }
static void __free_fdesc(fdesc *d) { free(d); }
static int __getfirstfd(int minimum) { while(minimum<SLOTS && table[minimum])minimum++; return minimum; }
static LONG mock_FD_Alloc(struct Library *fd_base,LONG n,LONG owner,void *data,LONG *allocated) {
    (void)fd_base;(void)n;(void)owner;(void)data;(void)allocated;abort();
}
static void mock_FD_Free(struct Library *fd_base,LONG n,LONG owner) { (void)fd_base;(void)n;(void)owner;abort(); }
#define FD_Alloc(n,o,d,a) mock_FD_Alloc(FDBase,n,o,d,a)
#define FD_Free(n,o) mock_FD_Free(FDBase,n,o)
static int ar_close(int fd);
static int __getfdslot(int fd) {
    if(fail_slot || fd<0 || fd>=SLOTS){errno=ENOMEM;return -1;}
    if(table[fd])assert(!ar_close(fd));
    return fd;
}
static void __posixc_reclock_close(fdesc *d) {
    assert(!table_depth && d && d->fcb);
    atomic_fetch_add(&record_closes,1);
}
static void put_file(struct reclock_file *file) {
    assert(file==(void *)(uintptr_t)99); atomic_fetch_add(&record_puts,1);
}
/* RELEASE_RECORD */
static LONG Close(BPTR handle) { atomic_fetch_add(&handle_closes[handle],1);native_error=909;return 1; }
static void UnLock(BPTR handle) { atomic_fetch_add(&handle_unlocks[handle],1);native_error=909; }
static void FreeVec(void *p) {
    fcb *f=p;assert(!f->reclock);assert(!pthread_mutex_destroy(&f->io_lock.lock));
    atomic_fetch_add(&block_frees,1);free(p);
    if(metadata_cleanup)errno=ENXIO;
}
static LONG IoErr(void) { return native_error; }
static int __stdc_ioerr2errno(LONG e) { assert(e==777);return EIO; }
fcb *__holdfcb(int fd,int *error);
void __releasefcb(fcb *file);
/* HOLD_FILE */
/* RELEASE_FILE */
static atomic_int foreign_calls;
static SIPTR __fdhook(int fd,int operation,IPTR a,IPTR b,IPTR c,LONG *error) {
    (void)fd;(void)operation;(void)a;(void)b;(void)c;
    atomic_fetch_add(&foreign_calls,1);*error=0;return 217;
}
static pthread_mutex_t io_gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t io_changed=PTHREAD_COND_INITIALIZER;
static int io_waiting,io_proceed,io_fail;
static LONG backend(fcb *file,void *buffer,size_t size) {
    (void)buffer;(void)size;
    assert(!table_depth); /* Do not hold table admission across blocking DOS I/O. */
    assert(!pthread_mutex_lock(&io_gate));io_waiting=1;assert(!pthread_cond_broadcast(&io_changed));
    while(!io_proceed)assert(!pthread_cond_wait(&io_changed,&io_gate));
    assert(!pthread_mutex_unlock(&io_gate));
    __fcb_lock(file);LONG result=io_fail?-1:file->handle;__fcb_unlock(file);
    if(io_fail)native_error=777;
    return result;
}
static LONG __dos64_read(fcb *file,void *buffer,size_t count) { return backend(file,buffer,count); }
static LONG __dos64_write(fcb *file,void *buffer,size_t count) { return backend(file,buffer,count); }
/* READ */
/* WRITE */
struct stat { int identity; };
struct stat64 { int identity; };
struct MsgPort { BPTR handle; };
struct FileHandle { struct MsgPort *fh_Type; };
struct FileLock { struct MsgPort *fl_Task; };
static struct MsgPort metadata_ports[64];
static struct FileHandle metadata_handles[64];
#define BADDR(h) ((void *)&metadata_handles[h])
#define ACTION_FLUSH 27
static LONG SetIoErr(LONG error) { LONG old=native_error;native_error=error;return old; }
static void pause_metadata(void) {
    metadata_pause=0;
    assert(!table_depth);
    assert(!pthread_mutex_lock(&io_gate));io_waiting=1;assert(!pthread_cond_broadcast(&io_changed));
    while(!io_proceed)assert(!pthread_cond_wait(&io_changed,&io_gate));
    assert(!pthread_mutex_unlock(&io_gate));
}
static int metadata_backend(BPTR handle) {
    if(metadata_pause)pause_metadata();
    assert(!table_depth && !atomic_load(&handle_closes[handle]));
    if(io_fail){errno=EIO;native_error=777;return -1;}
    return 0;
}
static int __stat(BPTR h,struct stat *s,BOOL isfile) {
    assert(isfile && !metadata_file_depth);int result=metadata_backend(h);if(!result)s->identity=h;return result;
}
static int __stat64(BPTR h,struct stat64 *s,BOOL isfile) {
    assert(isfile && !metadata_file_depth);int result=metadata_backend(h);if(!result)s->identity=h;return result;
}
static int flush_calls;
static int resize_active, resize_failure, resize_writes, resize_calls, resize_seeks;
static unsigned char resize_data[16384];
static LONG Flush(BPTR h) { flush_calls++;if(resize_active && resize_failure==6){native_error=777;return 0;} return metadata_backend(h)==0; }
static LONG DoPkt(struct MsgPort *port,LONG action,LONG a,LONG b,LONG c,LONG d,LONG e) {
    assert(action==ACTION_FLUSH && !a&&!b&&!c&&!d&&!e);
    return metadata_backend(port->handle)==0;
}
/* FSTAT */
/* FSTAT64 */
/* FSYNC */
#define OFFSET_BEGINNING -1
#define OFFSET_CURRENT 0
#define OFFSET_END 1
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define ERROR_OBJECT_TOO_LARGE 223
#define _FCB_FLUSHONREAD 4
#define FLUSHONREADCHECK do { if(fdesc->fcb->privflags&_FCB_FLUSHONREAD){fdesc->fcb->privflags&=~_FCB_FLUSHONREAD;Flush(fdesc->fcb->handle);} } while(0);
static QUAD positions[64], sizes[64];
static int position_failure, seek_calls;
static QUAD __dos64_seek(fcb *f,QUAD offset,LONG whence) {
    if(metadata_pause)pause_metadata();
    assert(!table_depth && !atomic_load(&handle_closes[f->handle]));
    seek_calls++;
    if(resize_active) {
        resize_seeks++;
        if((resize_failure==4 && resize_seeks==1) || (resize_failure==7 && resize_seeks==2) || resize_failure==8) {
            native_error=777;return -1;
        }
    }
    if(io_fail){native_error=777;return -1;}
    QUAD old=positions[f->handle];
    positions[f->handle]=(whence==OFFSET_CURRENT?old:whence==OFFSET_END?sizes[f->handle]:0)+offset;
    return old;
}
static QUAD __dos64_getpos(fcb *f) {
    if(metadata_original)assert(metadata_file_depth>0 && "seek position query retains I/O serialization");
    if(resize_active && resize_failure==1){native_error=777;return -1;}
    if(position_failure){native_error=ERROR_OBJECT_TOO_LARGE;return -1;}
    return positions[f->handle];
}
/* LSEEK */
/* LSEEK64 */
static QUAD __dos64_getsize(fcb *f) {
    assert(!table_depth && !atomic_load(&handle_closes[f->handle]));
    if(metadata_original)assert(metadata_file_depth>0);
    if(io_fail || (resize_active && resize_failure==2)){positions[f->handle]=999;native_error=777;return -1;}
    return sizes[f->handle];
}
static QUAD __dos64_setfilesize(fcb *f,QUAD length,LONG mode) {
    assert(!table_depth && mode==OFFSET_BEGINNING && length>=0);
    if(metadata_original)assert(metadata_file_depth>0);
    resize_calls++;
    if(resize_failure==3 || resize_failure==8){native_error=resize_failure==8?ERROR_OBJECT_TOO_LARGE:777;return -1;}
    sizes[f->handle]=length;return length;
}
static LONG FWrite(BPTR h,const void *buffer,LONG size,LONG count) {
    assert(!table_depth && !atomic_load(&handle_closes[h]) && size==1 && count>0);
    resize_writes++;
    if(resize_failure==5){native_error=0;return 0;}
    /* Force short transfers and check every exposed byte, including >4 KiB. */
    LONG written=count>137?137:count;
    if(resize_active){assert(positions[h]>=0 && positions[h]+written<=(QUAD)sizeof(resize_data));memcpy(resize_data+positions[h],buffer,written);}
    positions[h]+=written;return written;
}
/* RESIZE */
/* FTRUNCATE */
/* FTRUNCATE64 */
/* CLOSE */
/* DUP */
/* DUP2 */
struct directory { int fd; void *priv; };
#define DOS_FIB 1
static void FreeDosObject(int type,void *p) { assert(type==DOS_FIB);free(p); }
/* CLOSEDIR */
static void init_sem(struct SignalSemaphore *s) {
    pthread_mutexattr_t attr;assert(!pthread_mutexattr_init(&attr));
    assert(!pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE));
    assert(!pthread_mutex_init(&s->lock,&attr));assert(!pthread_mutexattr_destroy(&attr));
}
static fcb *install(int fd,BPTR handle,unsigned flags) {
    fcb *file=calloc(1,sizeof(*file));fdesc *d=__alloc_fdesc();assert(file&&d);
    init_sem(&file->io_lock);file->handle=handle;file->flags=O_RDWR;file->privflags=flags;file->opencount=1;
    file->reclock=(void *)(uintptr_t)99;d->fcb=file;
    __fdesc_lock();__setfdesc(fd,d);__fdesc_unlock();return file;
}
struct attempt { int write;ssize_t result;int error; };
static void *run_io(void *data) {
    struct attempt *a=data;char byte=0;
    a->result=a->write?ar_write(5,&byte,1):ar_read(5,&byte,1);a->error=errno;return NULL;
}
struct metadata_attempt { int operation,error; int64_t result; fcb *file; };
static void *run_metadata(void *data) {
    struct metadata_attempt *a=data;struct stat sb;struct stat64 wide;
    metadata_pause=metadata_cleanup=1;metadata_original=a->file;
    a->result=a->operation==0?ar_fstat(5,&sb):a->operation==1?ar_fstat64(5,&wide):a->operation==2?ar_fsync(5):a->operation==3?ar_lseek(5,123,SEEK_SET):a->operation==4?ar_lseek64(5,INT64_C(0x100000007),SEEK_SET):a->operation==5?ar_ftruncate(5,8):ar_ftruncate64(5,8);
    a->error=errno;
    if(!a->result && a->operation<2)assert((a->operation?wide.identity:sb.identity)==11);
    return NULL;
}
static void reset_counts(void) {
    for(int n=0;n<64;n++){atomic_store(&handle_closes[n],0);atomic_store(&handle_unlocks[n],0);}
    atomic_store(&block_frees,0);atomic_store(&record_puts,0);atomic_store(&record_closes,0);
}
int main(void) {
    init_sem(&table_guard);init_sem(&reclock_sem);metadata_table_guard=&table_guard;
    fcb retired={0};assert(!__fcb_retain(&retired));
    for(int write=0;write<2;write++)for(int failed=0;failed<2;failed++) {
        reset_counts();install(5,11,0);io_waiting=io_proceed=0;io_fail=failed;
        struct attempt a={.write=write};pthread_t thread;assert(!pthread_create(&thread,NULL,run_io,&a));
        assert(!pthread_mutex_lock(&io_gate));
        while(!io_waiting)assert(!pthread_cond_wait(&io_changed,&io_gate));
        assert(!ar_close(5));
        assert(!atomic_load(&handle_closes[11])&&!atomic_load(&block_frees)&&!atomic_load(&record_puts));
        install(5,22,0); /* The old operation must never resolve this replacement. */
        io_proceed=1;assert(!pthread_cond_broadcast(&io_changed));assert(!pthread_mutex_unlock(&io_gate));
        assert(!pthread_join(thread,NULL));assert(a.result==(failed?-1:11));if(failed)assert(a.error==EIO);
        assert(atomic_load(&handle_closes[11])==1&&atomic_load(&block_frees)==1&&atomic_load(&record_puts)==1);
        assert(!ar_close(5));assert(atomic_load(&handle_closes[22])==1&&atomic_load(&record_closes)==2);
    }
    for(int operation=0;operation<7;operation++)for(int failed=0;failed<2;failed++) {
        reset_counts();fcb *admitted=install(5,11,0);io_waiting=io_proceed=0;io_fail=failed;
        metadata_ports[11].handle=11;metadata_handles[11].fh_Type=&metadata_ports[11];
        struct metadata_attempt a={.operation=operation,.file=admitted};pthread_t thread;
        assert(!pthread_create(&thread,NULL,run_metadata,&a));
        assert(!pthread_mutex_lock(&io_gate));
        while(!io_waiting)assert(!pthread_cond_wait(&io_changed,&io_gate));
        assert(!ar_close(5));
        assert(!atomic_load(&handle_closes[11]) && "metadata operation owns the admitted file");
        install(5,22,0);io_proceed=1;
        assert(!pthread_cond_broadcast(&io_changed));assert(!pthread_mutex_unlock(&io_gate));
        assert(!pthread_join(thread,NULL));
        assert(a.result==(failed?-1:operation<3?0:operation==3?123:operation==4?INT64_C(0x100000007):0));if(failed)assert(a.error==EIO);
        assert(atomic_load(&handle_closes[11])==1 && atomic_load(&block_frees)==1);
        assert(!ar_close(5));
    }
    reset_counts();fcb *file=install(5,11,0);file->opencount=UINT_MAX;
    char byte=0;int before=atomic_load(&foreign_calls);
    assert(ar_read(5,&byte,1)==-1&&errno==EOVERFLOW);
    assert(ar_write(5,&byte,1)==-1&&errno==EOVERFLOW);
    struct stat sb;struct stat64 wide;
    assert(ar_fstat(5,&sb)==-1&&errno==EOVERFLOW);
    assert(ar_fstat64(5,&wide)==-1&&errno==EOVERFLOW);
    assert(ar_fsync(5)==-1&&errno==EOVERFLOW);
    assert(ar_lseek(5,0,SEEK_SET)==-1&&errno==EOVERFLOW);
    assert(ar_lseek64(5,0,SEEK_SET)==-1&&errno==EOVERFLOW);
    assert(ar_ftruncate(5,1)==-1&&errno==EOVERFLOW);
    assert(ar_ftruncate64(5,1)==-1&&errno==EOVERFLOW);
    assert(ar_fstat(-1,&sb)==-1&&errno==EBADF);
    assert(ar_fstat64(-1,&wide)==-1&&errno==EBADF);
    assert(ar_fsync(-1)==-1&&errno==EBADF);
    assert(ar_lseek(-1,0,SEEK_SET)==-1&&errno==EBADF);
    assert(ar_lseek64(-1,0,SEEK_SET)==-1&&errno==EBADF);
    assert(atomic_load(&foreign_calls)==before);file->opencount=1;assert(!ar_close(5));
    reset_counts();file=install(5,11,0);assert(__dupfd(5,6,FD_CLOEXEC)==6);
    assert(table[6]->fdflags==FD_CLOEXEC&&file->opencount==2);
    assert(ar_dup2(5,7)==7&&file->opencount==3);assert(!ar_close(6));assert(!ar_close(7));
    fail_slot=1;assert(__dupfd(5,6,0)==-1&&file->opencount==1);
    assert(ar_dup2(5,7)==-1&&file->opencount==1);fail_slot=0;
    file->opencount=UINT_MAX;assert(__dupfd(5,6,0)==-1&&errno==EOVERFLOW);
    assert(ar_dup2(5,7)==-1&&errno==EOVERFLOW);file->opencount=1;assert(!ar_close(5));
    reset_counts();file=install(5,11,_FCB_ISDIR);
    assert(ar_read(5,&byte,1)==-1&&errno==EISDIR&&file->opencount==1);
    struct directory *dir=malloc(sizeof(*dir));assert(dir);dir->fd=5;dir->priv=malloc(1);
    assert(!ar_closedir(dir));assert(atomic_load(&handle_unlocks[11])==1&&!atomic_load(&handle_closes[11]));
    reset_counts();install(5,11,_FCB_DONTCLOSE_FH);assert(!ar_close(5));
    assert(!atomic_load(&handle_closes[11])&&atomic_load(&block_frees)==1);
    assert(ar_read(5,&byte,1)==217);assert(ar_write(5,&byte,1)==217);
    assert(atomic_load(&foreign_calls)==before+2);
    reset_counts();file=install(5,11,_FCB_FLUSHONREAD);
    io_fail=position_failure=0;flush_calls=seek_calls=0;
    positions[11]=3;sizes[11]=16;
    assert(ar_lseek(5,100,SEEK_SET)==100 && sizes[11]==16);
    assert(flush_calls==1 && seek_calls==1 && !(file->privflags&_FCB_FLUSHONREAD));
    assert(ar_lseek(5,-7,SEEK_CUR)==93);
    assert(ar_lseek64(5,-5,SEEK_END)==11);
    int old_seeks=seek_calls;
    assert(ar_lseek(5,0,999)==-1&&errno==EINVAL&&seek_calls==old_seeks&&file->opencount==1);
    file->privflags|=_FCB_FLUSHONREAD;io_fail=1;
    assert(ar_lseek(5,0,SEEK_SET)==-1&&errno==EIO);
    assert((file->privflags&_FCB_FLUSHONREAD) && seek_calls==old_seeks);
    io_fail=0;assert(ar_lseek(5,0,SEEK_SET)==0 && !(file->privflags&_FCB_FLUSHONREAD));
    position_failure=1;assert(ar_lseek64(5,30,SEEK_SET)==-1&&errno==EOVERFLOW);
    position_failure=0;
    assert(ar_lseek64(5,INT64_C(0x100000007),SEEK_SET)==INT64_C(0x100000007)&&sizes[11]==16);
    file->privflags|=_FCB_ISDIR;assert(ar_lseek(5,0,SEEK_SET)==-1&&errno==EISDIR);
    file->privflags&=~_FCB_ISDIR;file->handle=0;
    assert(ar_lseek64(5,0,SEEK_SET)==-1&&errno==EBADF&&file->opencount==1);
    file->handle=11;assert(!ar_close(5));
    for(int width=0;width<2;width++) {
        for(int failure=0;failure<=8;failure++) {
            reset_counts();file=install(5,11,0);resize_active=1;resize_failure=failure;
            io_fail=position_failure=0;resize_calls=resize_writes=resize_seeks=0;
            positions[11]=17;sizes[11]=8;memset(resize_data,0xa5,sizeof(resize_data));
            int r=width?ar_ftruncate64(5,12000):ar_ftruncate(5,12000);
            assert(r==(failure?-1:0));if(failure)assert(errno==(failure==8?EFBIG:EIO));
            if(failure!=7 && failure!=8)assert(positions[11]==17);
            if(!failure){
                assert(sizes[11]==12000 && resize_writes>3);
                for(int i=0;i<8;i++)assert(resize_data[i]==0xa5);
                for(int i=8;i<12000;i++)assert(resize_data[i]==0);
            }
            if(failure<=2 && failure)assert(!resize_calls && !resize_writes);
            if(failure==5 || failure==6)assert(file->privflags&_FCB_FLUSHONREAD);
            assert(file->opencount==1);resize_active=0;resize_failure=0;assert(!ar_close(5));
        }
        reset_counts();file=install(5,11,0);positions[11]=100;sizes[11]=80;
        assert(!(width?ar_ftruncate64(5,3):ar_ftruncate(5,3)) && sizes[11]==3 && positions[11]==100);
        int calls=resize_calls;
        assert((width?ar_ftruncate64(5,-1):ar_ftruncate(5,-1))==-1 && errno==EINVAL && resize_calls==calls);
        file->privflags=_FCB_FLUSHONREAD;io_fail=1;
        assert((width?ar_ftruncate64(5,4):ar_ftruncate(5,4))==-1 && errno==EIO);
        assert(resize_calls==calls && positions[11]==100 && (file->privflags&_FCB_FLUSHONREAD));
        io_fail=0;file->privflags=0;sizes[11]=INT64_C(0x100000007);
        assert(!ar_ftruncate64(5,INT64_C(0x100000007)) && sizes[11]==INT64_C(0x100000007) && positions[11]==100);
        file->flags=0;assert((width?ar_ftruncate64(5,4):ar_ftruncate(5,4))==-1 && errno==EBADF);
        file->flags=O_RDWR;file->privflags|=_FCB_ISDIR;
        assert((width?ar_ftruncate64(5,4):ar_ftruncate(5,4))==-1 && errno==EISDIR);
        file->privflags=0;file->handle=0;
        assert((width?ar_ftruncate64(5,4):ar_ftruncate(5,4))==-1 && errno==EBADF);
        file->handle=11;assert(!ar_close(5));
        assert((width?ar_ftruncate64(-1,4):ar_ftruncate(-1,4))==-1 && errno==EBADF);
    }
    for(int n=0;n<SLOTS;n++)assert(!table[n]);
    assert(!pthread_mutex_destroy(&table_guard.lock));assert(!pthread_mutex_destroy(&reclock_sem.lock));
    puts("PASS: actual read/write/close under close/reuse, errors, dup/dup2 rollback, overflow, directory and borrowed handles");
    return 0;
}
