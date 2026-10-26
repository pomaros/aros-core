/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "self_exit.h"
#define AROS_APPLE_SMP_MAX_CPUS 2
#define TS_INVALID 0
#define TS_RUN 2
struct Task { int tc_State; };
static struct { struct Task *ThisTask; int IDNestCnt; } tls;
#define TLS_GET(field) tls.field
static unsigned cpu;
static unsigned AROSCPUCurrentIndex(void) { return cpu; }
/* ACTUAL_ADAPTER */
static int calls, base;
static void publish(void *task, void *context) { assert(context==&base); free(task); ++calls; }
int main(void)
{
    struct Task *task=malloc(sizeof(*task)); assert(task);
    task->tc_State=TS_INVALID; tls.ThisTask=task; tls.IDNestCnt=0;
    cpu=2; assert(!api_self_exit_queue(task,&base,publish));
    cpu=0; assert(!api_self_exit_queue(0,&base,publish));
    tls.ThisTask=0; assert(!api_self_exit_queue(task,&base,publish)); tls.ThisTask=task;
    tls.IDNestCnt=-1; assert(!api_self_exit_queue(task,&base,publish)); tls.IDNestCnt=0;
    task->tc_State=TS_RUN; assert(!api_self_exit_queue(task,&base,publish)); task->tc_State=TS_INVALID;
    assert(api_self_exit_queue(task,&base,publish));
    assert(calls==0);
    tls.ThisTask=0;
    assert(AROSAppleSelfExitDetached(task));
    assert(calls==1);
    puts("PASS actual adapter admission");
}
