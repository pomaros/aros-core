/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int BPTR;
typedef struct { int io_lock, handle, flags, opencount; } fcb;
typedef struct { int fdflags; fcb *fcb; } fdesc;
#define BNULL 0
#define MEMF_ANY 0
#define MEMF_CLEAR 0
#define MODE_NEWFILE 1
#define MODE_OLDFILE 2
#define O_RDONLY 1
#define O_WRONLY 2
#define POSIXC_PIPE_NAME_SIZE 64
static int allocations, handles, reservations, alloc_calls, open_calls, slot_calls;
static int fail_alloc, fail_open, fail_slot, dos_error;
static fdesc *published[2];
static void *AllocVec(size_t size, int flags)
{
    (void)flags;
    if (++alloc_calls == fail_alloc) return NULL;
    void *p = calloc(1, size); assert(p); allocations++; return p;
}
static void FreeVec(void *p)
{ if (p) { allocations--; free(p); } errno = EIO; }
static fdesc *__alloc_fdesc(void) { return AllocVec(sizeof(fdesc), 0); }
static void __free_fdesc(fdesc *p) { FreeVec(p); }
static int getpid(void) { return 42; }
static void __posixc_pipe_name(char *p, unsigned long pid)
{ (void)pid; strcpy(p, "PIPE:fixture"); }
static BPTR Open(const char *name, int mode)
{
    (void)name; (void)mode;
    if (++open_calls == fail_open) { dos_error = ENOENT; return 0; }
    handles++; return open_calls;
}
static int IoErr(void) { return dos_error; }
static int __stdc_ioerr2errno(int e) { return e; }
static void DeleteFile(const char *name) { (void)name; dos_error = EIO; errno = EIO; }
static void Close(BPTR h) { assert(h && handles); handles--; dos_error = EIO; errno = EIO; }
static void InitSemaphore(int *p) { *p = 1; }
static int __getfreefdslot(int minimum)
{
    (void)minimum;
    if (++slot_calls == fail_slot) { errno = EMFILE; return -1; }
    reservations++; return slot_calls - 1;
}
static void __setfdesc(int fd, fdesc *p)
{
    assert(fd >= 0 && fd < 2);
    if (!p) { assert(reservations); reservations--; errno = EIO; }
    else { assert(!published[fd]); published[fd] = p; }
}
#include "pipe-production.c"
static void reset(void)
{
    assert(!allocations && !handles && !reservations);
    alloc_calls = open_calls = slot_calls = 0;
    fail_alloc = fail_open = fail_slot = 0;
    published[0] = published[1] = NULL;
    errno = dos_error = 0;
}
static void rejected(int expected)
{
    int output[2] = { 71, 72 };
    assert(fixture_pipe(output) == -1);
    assert(errno == expected);
    assert(!allocations && !handles && !reservations);
    assert(output[0] == 71 && output[1] == 72);
    assert(!published[0] && !published[1]);
}
int main(void)
{
    reset(); assert(fixture_pipe(NULL) == -1 && errno == EFAULT);
    for (int n = 1; n <= 4; n++) { reset(); fail_alloc = n; rejected(ENOMEM); }
    for (int n = 1; n <= 2; n++) { reset(); fail_open = n; rejected(ENOENT); }
    for (int n = 1; n <= 2; n++) { reset(); fail_slot = n; rejected(EMFILE); }
    reset(); int output[2] = {-1, -1}; assert(fixture_pipe(output) == 0);
    assert(output[0] == 0 && output[1] == 1 && published[0] && published[1]);
    assert(handles == 2 && reservations == 2 && allocations == 4);
    assert(published[0]->fcb->flags == O_RDONLY && published[1]->fcb->flags == O_WRONLY);
    for (int n = 0; n < 2; n++) {
        fdesc *p = published[n]; assert(p->fcb->opencount == 1 && p->fcb->io_lock == 1);
        Close(p->fcb->handle); FreeVec(p->fcb); __free_fdesc(p); __setfdesc(n, NULL);
    }
    reset(); puts("PASS pipe allocation/open/reservation rollback, error preservation and publication");
}
