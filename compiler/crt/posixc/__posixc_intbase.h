/*
    Copyright (C) 2012-2026, The AROS Development Team. All rights reserved.
    Copyright (C) 2026 John KNIPPER

    This file defines the private part of PosixCBase.
    This should only be used internally in posixc.library code so
    changes can be made to this structure without breaking backwards
    compatibility.
*/
#ifndef __POSIXC_INTBASE_H
#define __POSIXC_INTBASE_H

#include <libraries/posixc.h>
#include <exec/lists.h>
#include <exec/semaphores.h>
#include <dos/dos.h>
#include <devices/timer.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <aros/types/sigset_t.h>
#include <pwd.h>

/* Some private structs */
struct __random_state;
struct __env_item;
struct _fdesc;
struct vfork_data;

struct PosixCIntBase
{
    struct PosixCBase PosixCBase;

    /* optional libs */
    struct Library           *PosixCUserGroupBase;
    struct Library           *PosixCEntropyBase;    /* entropy.resource, for getentropy() */
    struct Library           *PosixCDOS64Base;      /* dos64.library, for 64-bit file I/O */

    /* common */
    APTR internalpool;
    int32_t flags;

    struct Device *timerBase;
    struct timerequest timerReq;
    struct MsgPort timerPort;

    /* random.c */
    struct __random_state *rs;

    /* getpwuid.c */
    struct passwd pwd;

    /* getpass.c */
    char passbuffer[PASS_MAX];

    /* sigprocmask.c */
    sigset_t   sigmask;

    /* __exec.c */
    BPTR exec_seglist;
    BPTR exec_cwd, exec_progdir; /* owned locks until RunCommand returns */
    struct _fcb *exec_files[3]; /* retained through native stdio restoration */
    char *exec_args;
    char *exec_taskname;
    APTR exec_pool;
    char **exec_tmparray;
    BPTR exec_oldin, exec_oldout, exec_olderr;
    struct StdCBase *exec_oldstdcbase;

    /* __fdesc.c */
    /* Synchronous vfork override and the existing module-owner path. The
       descriptor table itself is retained independently in fd_namespace. */
    struct PosixCIntBase *fd_owner;
    struct __fd_namespace *fd_namespace;
    struct __cwd_binding *cwd_runtime;

    /* __upath.c */
    char *upathbuf;  /* Buffer that holds intermediate converted paths */
    int doupath;   /* BOOL - does the conversion need to be done?  */
    int parent_does_upath; /* BOOL - parent does upath conversion */

    /* __vfork.c */
    struct vfork_data *vfork_data;

    /* chdir.c/fchdir.c */
    int cd_changed;
    BPTR cd_lock;

    /* flock.c */
    struct MinList _file_locks, *file_locks;

    /* umask */
    mode_t umask;

    /* __stdio.c */
    struct MinList stdio_files;

    /* setuid.c/getuid.c */
    uid_t uid; /* Real user id of process */
    uid_t euid; /* Effective user id of process */
    /* set(e)gid.c/get(e)gid.c */
    gid_t gid; /* Real group id of process */
    gid_t egid; /* Effective group id of process */

    /* __upath.c, at the end to keep the offsets above: a program linked
       against the global PosixCBase shares it with its pthreads; those Tasks
       convert into their own buffers (upath_others), upathbuf stays with the
       Task that opened the base. */
    struct Task *upath_task;
    struct MinList upath_others;
    struct SignalSemaphore upath_sem;

    /* __dl.c, at the end likewise. The modules dlopen() loaded are listed
       in the owner base (see fd_owner), so that a Process and its pthread
       workers share them; dl_release is the exit-list node that unloads
       them, queued once. dl_errors holds one dlerror() record per task
       that had an error, also in the owner base. */
    struct MinList dl_modules;
    struct SignalSemaphore dl_sem;
    APTR dl_release;
    struct MinList dl_errors;
};

/* Select the calling Task's live runtime binding, including nested opens. */
struct PosixCIntBase *__cwd_runtime_base(void);

/* flags; values of flags are power of two so they can be ORed together */

/* When a program is started with the exec functions and from vfork,
   this is indicated in the flags of the library.
   This way the child can use the parent posixc.library during its
   initialization phase */
#define EXEC_PARENT 0x00000001
#define VFORK_PARENT 0x00000002

/* This flag is set by vfork() to correctly report child process ID during
   execution of child code, even though that it's actually executed by parent
   process until execve() is called. */
#define PRETEND_CHILD 0x00000004

/* __upath.c: frees the path buffers of other Tasks (upath_others) */
void __upath_free_others(struct PosixCIntBase *PosixCBase);

#endif //__POSIXC_INTBASE_H
