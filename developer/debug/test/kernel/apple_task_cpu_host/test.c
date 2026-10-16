/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
struct IntETask {uint64_t iet_private1,iet_private2,iet_LastBusy,iet_LastUsageStamp;uint32_t iet_CpuUsage;struct {uint64_t tv_sec,tv_nsec;}iet_CpuTime;};
#include "task_accounting.h"
static struct AppleTaskSample sample;
int main(void){struct IntETask a={0},b={0};const uint64_t hz=1000000;
 /* Tick zero is a legitimate active start, not an inactive sentinel. */
 AppleTaskDispatch(&a,0);AppleTaskSample(&a,0,hz,0,1,&sample);assert(!sample.usage&&!sample.seconds&&!sample.micros);
 AppleTaskSample(&a,250000,hz,250000,1,&sample);assert(sample.usage==UINT32_MAX&&sample.micros==250000&&a.iet_private2==0);
 /* Same window/read-only CPU time does not reset the percentage baseline. */
 AppleTaskSample(&a,250001,hz,250001,1,&sample);assert(sample.usage==UINT32_MAX&&sample.micros==250001&&a.iet_LastBusy==250000);
 AppleTaskSwitch(&a,500000,hz);assert(a.iet_private2==500000&&!a.iet_private1&&a.iet_CpuTime.tv_nsec==500000000);
 AppleTaskSample(&a,500000,hz,500000,0,&sample);assert(sample.usage==UINT32_MAX&&sample.micros==500000);
 /* Idle retains ThisTask in Exec; no active slice is charged. Sleep advances
  * raw ticks but not the awake denominator. A plateau preserves prior window. */
 AppleTaskSample(&a,100000000,hz,500000,0,&sample);assert(sample.micros==500000&&sample.usage==UINT32_MAX);
 AppleTaskSample(&a,100250000,hz,750000,0,&sample);assert(sample.micros==500000&&!sample.usage);
 /* New dispatch after idle/sleep starts a fresh interval. */
 AppleTaskDispatch(&a,100250000);AppleTaskSample(&a,100375000,hz,1000000,1,&sample);assert(sample.usage==UINT32_C(0x80000000)&&sample.micros==625000);
 AppleTaskSwitch(&a,100375000,hz);AppleTaskSwitch(&a,100400000,hz);assert(a.iet_private2==625000);
 /* Independent task, wrapping 56-bit hardware counter and normalized seconds. */
 AppleTaskDispatch(&b,APPLE_TASK_COUNTER_MASK-999999);AppleTaskSwitch(&b,1000000,hz);assert(b.iet_private2==2000000&&b.iet_CpuTime.tv_sec==2&&!b.iet_CpuTime.tv_nsec);
 AppleTaskSample(&b,1000000,hz,0,0,&sample);assert(sample.seconds==2&&!sample.micros&&!sample.usage);
 AppleTaskSample(&b,1000001,hz,250000,0,&sample);assert(!sample.usage);
 /* A reversed/reset awake source rebaselines; no unsigned fake busy spike. */
 AppleTaskSample(&b,1000002,hz,1,0,&sample);assert(!sample.usage&&b.iet_LastUsageStamp==2);
 AppleTaskSample(&b,1000003,0,1,0,&sample);assert(!sample.seconds&&!sample.micros&&!sample.usage);
 /* Long windows exercise overflow-free Q0.32 ratio, beyond a 32-bit us wrap. */
 struct IntETask c={0};AppleTaskSample(&c,0,hz,1,0,&sample);c.iet_private2=UINT64_C(1)<<42;
 AppleTaskSample(&c,0,hz,(UINT64_C(1)<<43)+1,0,&sample);assert(sample.usage==UINT32_C(0x80000000));
 puts("apple-task-cpu-host PASS");}
