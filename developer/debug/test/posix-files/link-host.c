/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */

#include <stdio.h>
#include <stdint.h>
typedef long LONG;
typedef intptr_t SIPTR;
typedef void *BPTR;
typedef int BOOL;
typedef unsigned char *STRPTR;
typedef const unsigned char *CONST_STRPTR;
#define BNULL NULL
#define BADDR(x) (x)
#define FALSE 0
#define SHARED_LOCK -2
#define ERROR_OBJECT_NOT_FOUND 205
#define POSIXC_CWD_SCOPE_OR_RETURN(n) do { (void)(n); } while (0)
#include <assert.h>
#include <errno.h>
#include <string.h>

int aros_link(const char *, const char *);

struct MsgPort { int unused; };
struct FileLock { struct MsgPort *fl_Task; };
struct DevProc { struct MsgPort *dvp_Port; int unused; };
struct FileInfoBlock { int fib_DirEntryType; };
static struct MsgPort ports[2];
static struct FileLock source = {&ports[0]};
static struct DevProc destination = {&ports[0], 0};
static char converted[64];
static LONG error;
static unsigned mode, locks, procs, makes;



const char *__path_u2a(const char *path)
{
    if ((mode == 5 && !strcmp(path,"old")) || (mode == 6 && !strcmp(path,"new"))) {
        errno = EILSEQ; return NULL;
    }
    strcpy(converted, !strcmp(path,"old") ? "VOL:old" : "VOL:new");
    return converted;
}
BPTR Lock(CONST_STRPTR path, LONG access)
{
    assert(!strcmp((const char *)path,"VOL:old") && access == SHARED_LOCK);
    if (mode == 7) { error = ERROR_OBJECT_NOT_FOUND; return NULL; }
    ++locks; return &source;
}
void UnLock(BPTR lock) { assert(lock == &source && locks); --locks; error = 999; errno = EIO; }
BOOL Examine(BPTR lock, struct FileInfoBlock *info)
{
    assert(lock == &source);
    if (mode == 8) { error = 101; return 0; }
    info->fib_DirEntryType = mode == 4 ? 1 : -3; return 1;
}
struct DevProc *GetDeviceProc(CONST_STRPTR path, struct DevProc *previous)
{
    assert(!previous && !strcmp((const char *)path,"VOL:new"));
    destination.dvp_Port = &ports[mode == 1 ? 1 : 0];
    if (mode == 3) { error = ERROR_OBJECT_NOT_FOUND; return NULL; }
    ++procs; return &destination;
}
void FreeDeviceProc(struct DevProc *proc) { assert(proc == &destination && procs); --procs; error = 998; errno = EIO; }
BOOL MakeLink(STRPTR path, SIPTR target, LONG soft)
{
    assert(!strcmp((const char *)path,"VOL:new") && !soft);
    assert((BPTR)target == &source);
    ++makes;
    if (mode == 2) { error = 100; return 0; }
    return 1;
}
LONG IoErr(void) { return error; }
int __posixc_dos_error(int value)
{
    if (value == ERROR_OBJECT_NOT_FOUND) return ENOENT;
    return value == 100 ? EEXIST : EIO;
}
#include "link-production.c"
int main(void)
{
    const int expected[] = {0,EXDEV,EEXIST,ENOENT,EPERM,EILSEQ,EILSEQ,ENOENT,EIO};
    for (mode=0; mode<9; ++mode) {
        int result;
        makes=0; errno=0; result=aros_link("old","new");
        assert(result == (mode ? -1 : 0));
        assert(!mode || errno == expected[mode]);
        assert(locks==0 && procs==0);
        assert(makes == (mode==0 || mode==2 ? 1u : 0u));
    }
    errno=0; assert(aros_link(NULL,"new")==-1 && errno==EFAULT);
    errno=0; assert(aros_link("old",NULL)==-1 && errno==EFAULT);
    errno=0; assert(aros_link("","new")==-1 && errno==ENOENT);
    errno=0;assert(aros_link("old","")==-1 && errno==ENOENT);
    assert(locks==0 && procs==0);
    puts("PASS native hard-link failure, cross-volume refusal and cleanup error preservation");
    return 0;
}
