#ifndef ___FDESC_H
#define ___FDESC_H

/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Copyright (C) 2026 John KNIPPER
    $Id$

    Desc: file descriptors handling internals - header file
    Lang: english
*/

#include <dos/dos.h>
#include <exec/semaphores.h>

/* file control block - one per file handle */
struct reclock_file;

typedef struct _fcb
{
    BPTR handle;// if privflags has flag _FCB_ISDIR: BCPL pointer to struct FileLock
                // otherwise: BCPL pointer to struct FileHandle
    int  flags;
    /* Descriptor references plus retained in-flight operations. */
    unsigned int opencount;
    unsigned int privflags;
    /* Serialises DOS operations on the shared handle. A single fcb (one DOS
       FileHandle) is shared between descriptors that are dup'd, inherited
       across vfork()/exec*(), or reached from pthread workers through the
       creator's descriptor table. DOS FileHandles carry buffer/position
       state and are not safe for concurrent use, so read/write serialise
       on this. Initialised at fcb creation. */
    struct SignalSemaphore io_lock;
    /* The file record locks know this handle by (see __reclock.c), once
       a lock operation has named it. */
    struct reclock_file *reclock;
} fcb;

#include <proto/exec.h>
/* Serialise a whole POSIX/stdio operation against concurrent use of the
   same shared DOS handle. Recursive (SignalSemaphore), so a syscall may
   hold it across a sequence that itself calls the __dos64_* primitives
   (which also lock). A NULL/absent handle needs no serialisation. */
static inline void __fcb_lock(fcb *f)   { if (f) ObtainSemaphore(&f->io_lock); }
static inline void __fcb_unlock(fcb *f) { if (f) ReleaseSemaphore(&f->io_lock); }

/* The caller must already own a reference or hold the descriptor-table guard
   protecting one. Never revive a retired block or wrap its reference count. */
static inline BOOL __fcb_retain(fcb *file)
{
    unsigned int count = __atomic_load_n(&file->opencount, __ATOMIC_RELAXED);
    do
    {
        if (!count || count == (unsigned int)-1)
            return FALSE;
    } while (!__atomic_compare_exchange_n(&file->opencount, &count, count + 1,
                FALSE, __ATOMIC_RELAXED, __ATOMIC_RELAXED));
    return TRUE;
}

/* Retain under the table guard; error is zero for an absent/foreign fd, or
   EOVERFLOW when a local file cannot admit another operation. */
fcb *__holdfcb(int fd, int *error);
void __releasefcb(fcb *file);

/* privflags */
#define _FCB_ISDIR        ((unsigned int)1<<0)
#define _FCB_DONTCLOSE_FH ((unsigned int)1<<1)
#define _FCB_FLUSHONREAD  ((unsigned int)1<<2)
#define _FCB_CONSOLERAW   ((unsigned int)1<<3)
#define _FCB_FH64         ((unsigned int)1<<14)
#define _FCB_FS64         ((unsigned int)1<<15)

#define FLUSHONREADCHECK                                                \
    if (__builtin_expect(fdesc->fcb->privflags & _FCB_FLUSHONREAD, 0))  \
    {                                                                   \
        fdesc->fcb->privflags &= ~_FCB_FLUSHONREAD;                     \
        __fcb_lock(fdesc->fcb);                                         \
        Flush(fdesc->fcb->handle);                                      \
        __fcb_unlock(fdesc->fcb);                                       \
    }

/* file descriptor structure - one per descriptor */
typedef struct _fdesc
{
    fcb  *fcb;
    int  fdflags;
} fdesc;

struct PosixCIntBase;
struct __fd_namespace;
/* Borrowed only while the calling runtime remains open. */
struct __fd_namespace *__fd_namespace_current(void);
struct __fd_namespace *__fd_namespace_capture(void);
void __fd_namespace_drop(struct __fd_namespace *space);
void __fdesc_lock(void);
void __fdesc_unlock(void);
int __getfdslots(void);
fdesc *__getfdesc(register int fd);
void __setfdesc(register int fd, fdesc *fdesc);
int __getfdslot(int wanted_fd);
int __dupfd(int oldfd, int minimum, int flags);
/* wanted_fd for __getfdslot()/__open(): the lowest free fd, picked and
   reserved under the table lock so two Tasks cannot get the same one */
#define __FD_FIRSTFREE (-2)
int __getfirstfd(register int startfd);
/* The lowest free fd not below minimum, picked and reserved in one step */
int __getfreefdslot(int minimum);
int __open(int wanted_fd, const char *pathname, int flags, int mode);
void __updatestdio(void);
LONG __oflags2amode(int flags);
fdesc *__alloc_fdesc(void);
void __free_fdesc(fdesc *fdesc);
void __close_on_exec_fdescs(void);

/* Resolve the operation hooks for a descriptor owned by another subsystem
   (e.g. a bsdsocket.library socket) through fd.library.  Returns the hooks
   and, via datap, the owner data to act on; returns NULL for a plain posixc
   file descriptor (handled locally) or when fd.library is unavailable. */
struct fd_hooks;
const struct fd_hooks *__getfdhooks(int fd, APTR *datap);

/* Coherent foreign-owner call admission. Every call retains fd.library and
 * its hook binding until completion; providers must also retire their objects. */
#define __FDH_READ 1
#define __FDH_WRITE 2
#define __FDH_CLOSE 3
#define __FDH_IOCTL 4
#define __FDH_FCNTL 5
#define __FDH_DUP 6
SIPTR __fdhook(int fd, int operation, IPTR a, IPTR b, IPTR c, LONG *error);

#endif /* ___FDESC_H */
