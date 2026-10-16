/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#ifndef APPLE_TASK_ACCOUNTING_H
#define APPLE_TASK_ACCOUNTING_H
#include <stdint.h>
/* ARM guarantees at least 56 counter bits. All intervals must be <2^56 ticks.
 * Start+1 reserves zero for inactive, including a genuine dispatch at tick 0. */
#define APPLE_TASK_COUNTER_MASK ((UINT64_C(1)<<56)-1)
#define APPLE_TASK_USAGE_WINDOW UINT64_C(250000)
static inline uint64_t AppleTaskCounter(void)
{ uint64_t value;__asm__ volatile("isb\n\tmrs %0,cntvct_el0":"=r"(value));return value&APPLE_TASK_COUNTER_MASK; }
static inline uint64_t AppleTaskFrequency(void)
{ uint64_t value;__asm__ volatile("mrs %0,cntfrq_el0":"=r"(value));return value; }
static inline uint64_t AppleTaskDelta(uint64_t now,uint64_t start)
{ return (now-start)&APPLE_TASK_COUNTER_MASK; }
static inline void AppleTaskDispatch(struct IntETask *et,uint64_t now)
{ et->iet_private1=(now&APPLE_TASK_COUNTER_MASK)+1; }
static inline void AppleTaskSwitch(struct IntETask *et,uint64_t now,uint64_t frequency)
{
    if(et->iet_private1) {
        et->iet_private2+=AppleTaskDelta(now,et->iet_private1-1);
        et->iet_private1=0;
    }
    if(frequency) {
        et->iet_CpuTime.tv_sec=et->iet_private2/frequency;
        et->iet_CpuTime.tv_nsec=(et->iet_private2%frequency)*UINT64_C(1000000000)/frequency;
    }
}
struct AppleTaskSample {uint64_t seconds;uint32_t micros,usage;};
static inline void AppleTaskSample(struct IntETask *et,uint64_t now,uint64_t frequency,uint64_t awake,int active,struct AppleTaskSample *out)
{
    uint64_t busy=et->iet_private2;
    if(active&&et->iet_private1)busy+=AppleTaskDelta(now,et->iet_private1-1);
    out->seconds=out->micros=out->usage=0;
    if(!frequency)return;
    out->seconds=busy/frequency;out->micros=(busy%frequency)*UINT64_C(1000000)/frequency;
    /* Keep sub-window readers from replacing the baseline or publishing noisy
     * percentages. A stopped awake clock leaves the previous window unchanged. */
    uint64_t elapsed=et->iet_LastUsageStamp?awake-(et->iet_LastUsageStamp-1):0;
    if(!et->iet_LastUsageStamp || awake<et->iet_LastUsageStamp-1) {
        et->iet_LastBusy=busy;et->iet_LastUsageStamp=awake+1;et->iet_CpuUsage=0;
    } else if(elapsed>=APPLE_TASK_USAGE_WINDOW) {
        uint64_t ticks=busy-et->iet_LastBusy;
        uint64_t micros=ticks/frequency*UINT64_C(1000000)+(ticks%frequency)*UINT64_C(1000000)/frequency;
        /* Divide first when the window is large to avoid shifting a 64-bit
         * microsecond count by 32. Long-double is forbidden in kernel code. */
        uint32_t fraction=0;
        if(micros>=elapsed)fraction=UINT32_MAX;
        else {uint64_t rem=micros;for(unsigned bit=0;bit<32;++bit){fraction<<=1;if(rem>=elapsed-rem){rem-=elapsed-rem;fraction|=1;}else rem+=rem;}}
        et->iet_CpuUsage=fraction;et->iet_LastBusy=busy;et->iet_LastUsageStamp=awake+1;
    }
    out->usage=et->iet_CpuUsage;
}
#endif
