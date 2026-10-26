/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "self_exit.h"
static struct AppleSelfExit slots[2];
static int calls, base, detached;
static void publish(void *task, void *context)
{
    assert(detached && context == &base);
    ++calls;
    /* An intervening CPU may reclaim task before publication returns. */
    free(task);
    assert(!AppleSelfExitQueue(&slots[0], 0, &base, &base, publish));
    assert(!AppleSelfExitDetached(&slots[0], 0, task));
}
int main(void)
{
    void *task = malloc(64);
    assert(task);
    assert(!AppleSelfExitQueue(0, 0, task, &base, publish));
    assert(!AppleSelfExitQueue(&slots[0], 0, 0, &base, publish));
    assert(!AppleSelfExitQueue(&slots[0], 0, task, 0, publish));
    assert(!AppleSelfExitQueue(&slots[0], 0, task, &base, 0));
    assert(AppleSelfExitQueue(&slots[0], 0, task, &base, publish));
    assert(calls == 0); /* service interleaves here: no published ownership */
    assert(!AppleSelfExitQueue(&slots[0], 0, task, &base, publish));
    assert(!AppleSelfExitDetached(&slots[0], 1, task));
    assert(!AppleSelfExitDetached(&slots[0], 0, &base));
    assert(calls == 0);
    detached = 1;
    assert(AppleSelfExitDetached(&slots[0], 0, task)==APPLE_EXIT_PUBLISHED);
    assert(calls == 1);
    assert(AppleSelfExitDetached(&slots[0], 0, 0)==APPLE_EXIT_EMPTY);
    assert(calls == 1);
    assert(AppleSelfExitDetached(&slots[1], 1, 0)==APPLE_EXIT_EMPTY);
    task = malloc(64);
    assert(AppleSelfExitQueue(&slots[0], 0, task, &base, publish));
    assert(AppleSelfExitDetached(&slots[0], 0, task)==APPLE_EXIT_PUBLISHED);
    assert(calls == 2);
    puts("PASS real self-exit state machine");
}
