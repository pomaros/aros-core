/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
typedef uint32_t ULONG, Tag;
typedef uintptr_t IPTR;
struct TagItem { Tag ti_Tag; IPTR ti_Data; };
/* ACTUAL_CONSTANTS */
/* ACTUAL_UTILITIES */
#define FindTagItem LibFindTagItem
#define GetTagData LibGetTagData
static void ApplyTagChanges(struct TagItem *list, struct TagItem *changelist)
/* ACTUAL_APPLY */

static IPTR policy(int isAsynch, IPTR parent, const struct TagItem *tags)
{
    struct { IPTR pr_WindowPtr; } process = {parent}, *me = &process;
    struct TagItem proctags[] = {
        { NP_WindowPtr, /* ACTUAL_WINDOW_VALUE */ },
        { TAG_MORE, (IPTR)tags }
    };
    /* CreateNewProc inherits only the parent's explicit suppression. */
    struct TagItem defaults[] = {
        { NP_WindowPtr, parent == (IPTR)-1 ? parent : 0 }, { TAG_END, 0 }
    };
    ApplyTagChanges(defaults, proctags);
    return defaults[0].ti_Data;
}
int main(void)
{
    struct TagItem none[] = {{TAG_END, 0}};
    struct TagItem suppress[] = {{NP_WindowPtr, (IPTR)-1}, {TAG_END, 0}};
    struct TagItem window[] = {{NP_WindowPtr, 0x1234}, {TAG_END, 0}};
    struct TagItem allow[] = {{NP_WindowPtr, 0}, {TAG_END, 0}};
    struct TagItem chained[] = {{TAG_MORE, (IPTR)suppress}};
    struct TagItem skipped[] = {{TAG_SKIP, 1}, {NP_WindowPtr, 0},
        {TAG_IGNORE, 0}, {TAG_MORE, (IPTR)suppress}};
    struct TagItem duplicate[] = {{NP_WindowPtr, 0x4567},
        {NP_WindowPtr, (IPTR)-1}, {TAG_END, 0}};
    /* Defaults stay the same, including async's default-public-screen policy. */
    assert(policy(1, (IPTR)-1, NULL) == 0);
    assert(policy(1, 0x88, none) == 0);
    assert(policy(0, 0x88, none) == 0x88);
    assert(policy(0, (IPTR)-1, NULL) == (IPTR)-1);
    /* An explicit no-requester policy must reach the spawned process. */
    assert(policy(1, 0x88, suppress) == (IPTR)-1);
    assert(policy(0, 0x88, suppress) == (IPTR)-1);
    assert(policy(1, 0, window) == 0x1234);
    assert(policy(0, (IPTR)-1, allow) == 0);
    assert(policy(1, 0, chained) == (IPTR)-1);
    assert(policy(1, 0, skipped) == (IPTR)-1);
    assert(policy(1, 0, duplicate) == 0x4567);
    /* pthread_create omits NP_WindowPtr; CreateNewProc inherits -1. */
    IPTR child = policy(1, 0, suppress);
    IPTR inherited = child == (IPTR)-1 ? child : 0;
    assert(inherited == (IPTR)-1);
    puts("PASS actual requester tag expression and utility traversal (12 checks)");
    return 0;
}
