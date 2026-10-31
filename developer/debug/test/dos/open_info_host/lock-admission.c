/* Copyright (C) 2026 John KNIPPER
 * Distributed under the AROS Public License 1.1. */
#include <assert.h>
#include <stdio.h>
typedef long LONG;
typedef unsigned long ULONG;
#define SHARED_LOCK (-2)
#define EXCLUSIVE_LOCK (-1)
#define ERROR_OBJECT_IN_USE 202
#define D(x)
struct GlobalLock { struct GlobalLock *next; ULONG dir_cluster, dir_entry; LONG access; };
struct Info { struct GlobalLock *locks; };
struct Super { struct Info *info; };
struct Globals { struct Super *sb; };
struct ExtFileLock { int unused; };
struct DirHandle { int unused; };
struct DirEntry { int unused; };
#define ForeachNode(list, node) for ((node)=*(list); (node); (node)=(node)->next)
#include "lock-admission-actual.c"
int main(void)
{
    struct GlobalLock other={NULL,4,2,EXCLUSIVE_LOCK};
    struct GlobalLock held={&other,3,2,SHARED_LOCK};
    struct Info info={&held}; struct Super sb={&info}; struct Globals glob={&sb};
    struct ExtFileLock sentinel, *output=&sentinel;
    assert(LockFile(3,2,SHARED_LOCK,&output,&glob)==0);
    assert(LockFile(3,2,EXCLUSIVE_LOCK,&output,&glob)==ERROR_OBJECT_IN_USE);
    held.access=EXCLUSIVE_LOCK;
    assert(LockFile(3,2,SHARED_LOCK,&output,&glob)==ERROR_OBJECT_IN_USE);
    assert(LockFile(3,2,EXCLUSIVE_LOCK,&output,&glob)==ERROR_OBJECT_IN_USE);
    assert(LockFile(3,9,SHARED_LOCK,&output,&glob)==0);
    assert(LockFile(9,2,EXCLUSIVE_LOCK,&output,&glob)==0);
    info.locks=NULL;
    assert(LockFile(3,2,SHARED_LOCK,&output,&glob)==0);
    assert(LockFile(3,2,EXCLUSIVE_LOCK,&output,&glob)==0);
    assert(output==&sentinel);
    puts("PASS FAT lock admission: shared/shared, both exclusive conflicts, unrelated and absent locks");
    return 0;
}
