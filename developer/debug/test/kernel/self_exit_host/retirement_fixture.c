/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define AROS_APPLE_SMP_MAX_CPUS 2
static uint32_t memory_dispatch_snapshot[2];
static void *memory_task_snapshot[2],*retiring_task,*current_task;
static unsigned cpu,online=3,ended,flushed,held;
static uint64_t token=17;
#define TLS_GET(field) current_task
static unsigned AROSCPUCurrentIndex(void) { return cpu; }
static uint32_t AROSAppleSMPOnlineMask(void) { return online; }
static uint64_t AROSApplePageMutationBegin(void) { assert(!held);held=token!=0;return token; }
static void AROSApplePageMutationEnd(uint64_t t) { assert(held && t==token);held=0;++ended; }
static void record_tlbi(void) { assert(held);++flushed; }
/* ACTUAL_RETIREMENT */
int main(void)
{
 int owner,caller;retiring_task=&owner;current_task=&caller;
 memory_task_snapshot[1]=&owner;
 assert(!retirement_fence() && ended==1 && !flushed && !held);
 memory_task_snapshot[1]=0;memory_dispatch_snapshot[1]=1;
 assert(!retirement_fence() && ended==2 && !flushed && !held);
 memory_dispatch_snapshot[1]=0;
 assert(retirement_fence() && ended==3 && flushed==1 && !held);
 current_task=&owner;
 assert(!retirement_fence() && ended==4 && flushed==1 && !held);
 current_task=&caller;token=0;
 assert(!retirement_fence() && ended==4 && flushed==1 && !held);
 token=19;online=1;memory_task_snapshot[1]=&owner;memory_dispatch_snapshot[1]=1;
 assert(retirement_fence() && ended==5 && flushed==2 && !held);
 puts("PASS actual retirement fence: live/dispatch/self/failed-barrier/offline controls");
}
