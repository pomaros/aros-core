/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Licensed under the AROS Public License (APL), Version 1.1. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

/* Modeled AROS types and DOS. The production sources are included below
   with their own #include lines removed. */
typedef long LONG;
typedef unsigned long ULONG;
typedef unsigned char *STRPTR;
typedef const unsigned char *CONST_STRPTR;
typedef short BOOL;
typedef void *BPTR;
#define BNULL ((BPTR)0)
#define DOSFALSE 0
#define TICKS_PER_SECOND 50
#define SHARED_LOCK -2
#define MEMF_ANY 0
#define ERROR_OBJECT_IN_USE 202
#define ERROR_OBJECT_NOT_FOUND 205
#define ERROR_LINE_TOO_LONG 120
#define ERROR_WRITE_PROTECTED 214
#define AT_FDCWD -100
#define AT_SYMLINK_NOFOLLOW 0x02
#define UTIME_NOW ((1L << 30) - 1L)
#define UTIME_OMIT ((1L << 30) - 2L)
#define OFFSET_FROM_1970 2922*24*60*60
#define _FCB_ISDIR 1U
struct DateStamp { LONG ds_Days, ds_Minute, ds_Tick; };

typedef struct { int handle_is_lock; void *handle; unsigned privflags; int locked; } fcb;
typedef struct { fcb *fcb; } fdesc;

static int ioerror, gmtoffset, allocs, frees, locks, unlocks, fcb_depth;
static int lock_fails, lock_error, set_fails, set_error, at_fails, at_entered, at_left;
static const char *link_name, *link_target;
static size_t name_length = 12;
static char last_set[70000];
static struct DateStamp last_date, now_date = {17000, 600, 25};
static int sets;
static char lock_token;
static fcb file_fcb, dir_fcb;
static fdesc descriptors[2] = {{&file_fcb}, {&dir_fcb}};

static LONG IoErr(void) { return ioerror; }
static int __stdc_ioerr2errno(LONG error) { return error == ERROR_OBJECT_NOT_FOUND ? ENOENT : error == ERROR_WRITE_PROTECTED ? EROFS : EIO; }
static int __stdc_gmtoffset(void) { return gmtoffset; }
static void DateStamp(struct DateStamp *ds) { *ds = now_date; }
static void *AllocVec(ULONG size, int flags) { (void)flags; allocs++; return malloc(size); }
static void FreeVec(void *p) { if (p) frees++; free(p); }
static BPTR Lock(CONST_STRPTR name, LONG mode)
{
    assert(mode == SHARED_LOCK);
    (void)name;
    if (lock_fails) { ioerror = lock_error; return BNULL; }
    locks++;
    return &lock_token;
}
static void UnLock(BPTR lock) { assert(lock == &lock_token); unlocks++; }
/* The resolved name: the link target, or a name name_length bytes long. */
static BOOL resolved_name(STRPTR buffer, LONG size, const char *source)
{
    char name[70000];
    if (link_name && strcmp(source, link_name) == 0)
        snprintf(name, sizeof(name), "%s", link_target);
    else
    {
        memset(name, 'n', name_length);
        memcpy(name, "WORK:", 5);
        name[name_length] = '\0';
    }
    if (strlen(name) + 1 > (size_t)size) { ioerror = ERROR_LINE_TOO_LONG; return DOSFALSE; }
    strcpy((char *)buffer, name);
    return 1;
}
static const char *last_lock_name;
static BOOL NameFromLock(BPTR lock, STRPTR buffer, LONG size)
{
    if (lock == (BPTR)&dir_fcb) return resolved_name(buffer, size, "dir");
    assert(lock == &lock_token);
    return resolved_name(buffer, size, last_lock_name);
}
static BOOL NameFromFH(BPTR fh, STRPTR buffer, LONG size)
{
    assert(fh == (BPTR)&file_fcb);
    assert(fcb_depth == 1);
    return resolved_name(buffer, size, "file");
}
static BOOL SetFileDate(CONST_STRPTR name, struct DateStamp *ds)
{
    sets++;
    if (set_fails) { ioerror = set_error; return DOSFALSE; }
    snprintf(last_set, sizeof(last_set), "%s", (const char *)name);
    last_date = *ds;
    return 1;
}
static fdesc *__getfdesc(int fd) { return fd == 3 ? &descriptors[0] : fd == 4 ? &descriptors[1] : NULL; }
static void __fcb_lock(fcb *f) { assert(f); fcb_depth++; }
static void __fcb_unlock(fcb *f) { assert(f); fcb_depth--; }
static int __at_isabsolute(const char *path) { return strchr(path, ':') != NULL || path[0] == '/'; }
static int __at_enter(int dirfd, BPTR *oldcd, BPTR *dirlock)
{
    (void)oldcd; (void)dirlock;
    if (at_fails) { errno = EBADF; return -1; }
    assert(dirfd == AT_FDCWD || dirfd == 4);
    at_entered++;
    return 0;
}
static void __at_leave(BPTR oldcd, BPTR dirlock) { (void)oldcd; (void)dirlock; at_left++; }
static const char *__path_u2a(const char *path) { last_lock_name = path; return path; }

#include "production-utimens.c"
#include "production-utimensat.c"
#include "production-futimens.c"
#include "production-utimes.c"
#include "stat-expressions.h"

/* __stat()'s reading of a DOS date, taken from the production source. */
static long long stat_seconds(const struct DateStamp *d)
{
    struct { struct DateStamp fib_Date; } fibstore = {*d}, *fib = &fibstore;
    return STAT_MTIME_EXPRESSION;
}
static long stat_nsec(const struct DateStamp *d)
{
    struct { struct DateStamp fib_Date; } fibstore = {*d}, *fib = &fibstore;
    return STAT_NSEC_EXPRESSION;
}

static void reset(void)
{
    ioerror = 0; lock_fails = set_fails = at_fails = 0; sets = 0;
    link_name = link_target = NULL; name_length = 12; last_set[0] = '\0';
    memset(&last_date, 0, sizeof(last_date)); errno = 0;
}

static void check_round_trip(long long seconds, long nsec, int offset)
{
    struct timespec ts[2] = {{0, UTIME_OMIT}, {(time_t)seconds, nsec}};
    reset();
    gmtoffset = offset;
    assert(aros_test_utimensat(AT_FDCWD, "WORK:f", ts, 0) == 0);
    assert(sets == 1);
    assert(stat_seconds(&last_date) == seconds);
    assert(stat_nsec(&last_date) == nsec / 20000000L * 20000000L);
    assert(last_date.ds_Minute >= 0 && last_date.ds_Minute < 1440);
    assert(last_date.ds_Tick >= 0 && last_date.ds_Tick < 60 * TICKS_PER_SECOND);
}

int main(void)
{
    struct timespec ts[2];
    struct timeval tv[2];
    int offsets[] = {0, -120, 60, 300, -600};
    long long base = 252460800LL; /* 1978-01-01 UTC */

    file_fcb.handle = &file_fcb; file_fcb.privflags = 0;
    dir_fcb.handle = &dir_fcb; dir_fcb.privflags = _FCB_ISDIR;

    /* Round trip through stat() for many instants and time zones. */
    for (size_t o = 0; o < sizeof(offsets) / sizeof(offsets[0]); o++)
    {
        for (long long s = base + 3600 * 12; s < base + 3600LL * 24 * 365 * 60; s += 7777777)
        {
            check_round_trip(s, 0, offsets[o]);
            check_round_trip(s + 59, 999999999, offsets[o]);
            check_round_trip(s + 1, 20000000, offsets[o]);
        }
    }

    /* Before the DOS epoch is refused, never wrapped. */
    reset(); gmtoffset = 0;
    ts[0].tv_sec = 0; ts[0].tv_nsec = UTIME_OMIT; ts[1].tv_sec = base - 1; ts[1].tv_nsec = 0;
    assert(aros_test_utimensat(AT_FDCWD, "WORK:f", ts, 0) == -1 && errno == EINVAL && sets == 0);
    /* Invalid nanoseconds, in either slot. */
    reset(); ts[1].tv_sec = base + 100; ts[1].tv_nsec = 1000000000L;
    assert(aros_test_utimensat(AT_FDCWD, "WORK:f", ts, 0) == -1 && errno == EINVAL && sets == 0);
    reset(); ts[1].tv_nsec = 0; ts[0].tv_nsec = -1;
    assert(aros_test_futimens(3, ts) == -1 && errno == EINVAL && sets == 0);
    /* UTIME_OMIT for the modification time changes nothing. */
    reset(); ts[0].tv_nsec = UTIME_NOW; ts[1].tv_nsec = UTIME_OMIT;
    assert(aros_test_utimensat(AT_FDCWD, "WORK:f", ts, 0) == 0 && sets == 0);
    /* NULL and UTIME_NOW use the current DOS date. */
    reset();
    assert(aros_test_utimensat(AT_FDCWD, "WORK:f", NULL, 0) == 0 && sets == 1);
    assert(memcmp(&last_date, &now_date, sizeof(now_date)) == 0);
    reset(); ts[0].tv_nsec = UTIME_OMIT; ts[1].tv_nsec = UTIME_NOW;
    assert(aros_test_futimens(3, ts) == 0 && memcmp(&last_date, &now_date, sizeof(now_date)) == 0);

    /* Arguments. */
    reset(); assert(aros_test_utimensat(AT_FDCWD, NULL, NULL, 0) == -1 && errno == EFAULT);
    reset(); assert(aros_test_utimensat(AT_FDCWD, "", NULL, 0) == -1 && errno == ENOENT);
    reset(); assert(aros_test_utimensat(AT_FDCWD, "WORK:f", NULL, 0x100) == -1 && errno == EINVAL);
    reset(); assert(aros_test_futimens(9, NULL) == -1 && errno == EBADF && sets == 0);

    /* A soft link is followed unless AT_SYMLINK_NOFOLLOW is given. */
    reset(); link_name = "WORK:link"; link_target = "WORK:target";
    assert(aros_test_utimensat(AT_FDCWD, "WORK:link", NULL, 0) == 0 && strcmp(last_set, "WORK:target") == 0);
    reset(); link_name = "WORK:link"; link_target = "WORK:target";
    assert(aros_test_utimensat(AT_FDCWD, "WORK:link", NULL, AT_SYMLINK_NOFOLLOW) == 0 && strcmp(last_set, "WORK:link") == 0);
    /* An exclusively locked file is set by its own name. */
    reset(); lock_fails = 1; lock_error = ERROR_OBJECT_IN_USE;
    assert(aros_test_utimensat(AT_FDCWD, "WORK:busy", NULL, 0) == 0 && strcmp(last_set, "WORK:busy") == 0);
    /* A missing file reports the DOS error. */
    reset(); lock_fails = 1; lock_error = ERROR_OBJECT_NOT_FOUND;
    assert(aros_test_utimensat(AT_FDCWD, "WORK:none", NULL, 0) == -1 && errno == ENOENT && sets == 0);
    reset(); set_fails = 1; set_error = ERROR_WRITE_PROTECTED;
    assert(aros_test_utimensat(AT_FDCWD, "WORK:ro", NULL, AT_SYMLINK_NOFOLLOW) == -1 && errno == EROFS);
    /* Names longer than the first buffer grow it; absurd ones stop. */
    reset(); name_length = 5000;
    assert(aros_test_utimensat(AT_FDCWD, "WORK:long", NULL, 0) == 0 && strlen(last_set) == 5000);
    reset(); name_length = 69000;
    assert(aros_test_utimensat(AT_FDCWD, "WORK:huge", NULL, 0) == -1 && errno == EIO && sets == 0);

    /* Relative names enter and leave dirfd; absolute names do not. */
    reset(); at_entered = at_left = 0;
    assert(aros_test_utimensat(4, "rel", NULL, 0) == 0 && at_entered == 1 && at_left == 1);
    reset(); at_entered = at_left = 0;
    assert(aros_test_utimensat(4, "WORK:abs", NULL, 0) == 0 && at_entered == 0 && at_left == 0);
    reset(); at_fails = 1;
    assert(aros_test_utimensat(4, "rel", NULL, 0) == -1 && errno == EBADF && sets == 0);

    /* futimens names a file through NameFromFH under its fcb lock, a
       directory through NameFromLock. */
    reset(); assert(aros_test_futimens(3, NULL) == 0 && sets == 1 && fcb_depth == 0);
    reset(); assert(aros_test_futimens(4, NULL) == 0 && sets == 1);
    reset(); name_length = 3000; assert(aros_test_futimens(3, NULL) == 0 && strlen(last_set) == 3000);

    /* utimes keeps its microseconds as ticks and follows links. */
    reset(); gmtoffset = -60;
    tv[0].tv_sec = 0; tv[0].tv_usec = 0; tv[1].tv_sec = (time_t)(base + 86400 * 400 + 37); tv[1].tv_usec = 640000;
    assert(aros_test_utimes("WORK:f", tv) == 0);
    assert(stat_seconds(&last_date) == base + 86400 * 400 + 37 && stat_nsec(&last_date) == 640000000L);
    reset(); tv[1].tv_usec = 1000000;
    assert(aros_test_utimes("WORK:f", tv) == -1 && errno == EINVAL && sets == 0);
    reset(); assert(aros_test_utimes(NULL, NULL) == -1 && errno == EFAULT);

    assert(locks == unlocks);
    assert(allocs == frees);
    puts("PASS futimens/utimensat/utimes host fixture");
    return 0;
}
