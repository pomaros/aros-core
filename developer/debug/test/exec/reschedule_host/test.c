/* SPDX-License-Identifier: APL-1.1
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 */
static struct Task a, b;
static void KrnSchedule(void)
{
    ++schedules;
    if (core_Schedule()) {
        core_Switch();
        core_Dispatch();
    }
}
static void reset(int priority)
{
    memset(&base, 0, sizeof(base));
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    base.TDNestCnt = base.IDNestCnt = -1;
    base.Quantum = base.Elapsed = 4;
    a.tc_State = TS_RUN;
    b.tc_State = TS_READY;
    a.tc_SPReg = b.tc_SPReg = 8;
    a.tc_SPUpper = b.tc_SPUpper = 16;
    a.tc_IDNestCnt = b.tc_IDNestCnt = -1;
    a.tc_Node.ln_Pri = priority;
    base.ThisTask = &a;
    Enqueue(&base.TaskReady, &b.tc_Node);
    schedules = softints = 0;
}
int main(void)
{
    unsigned counts[2] = {0, 0};
    reset(0);
    for (unsigned n = 0; n < 10000; ++n) {
        ++counts[base.ThisTask == &b];
        Reschedule();
    }
#if BASELINE
    assert(counts[0] == 10000 && counts[1] == 0);
    puts("reschedule-host baseline: equal-priority non-yield reproduced (10000:0)");
#else
    assert(counts[0] == 5000 && counts[1] == 5000);
    assert(!(base.SysFlags & SFF_QuantumOver));
    puts("reschedule-host fixed: equal-priority explicit yields alternate (5000:5000)");
#endif
    reset(1);
    for (unsigned n = 0; n < 10000; ++n) {
        assert(base.ThisTask == &a);
        Reschedule();
    }
    reset(-1);
    Reschedule();
    assert(base.ThisTask == &b);
    reset(0);
    base.TDNestCnt = 0;
    Reschedule();
    assert(schedules == 0 && softints == 0);
    assert(base.SysFlags & SFF_AttnSwitch);
    base.TDNestCnt = -1;
    KrnSchedule();
    assert(base.ThisTask == (BASELINE ? &a : &b));
    reset(0);
    base.IDNestCnt = 0;
    Reschedule();
    Reschedule();
    assert(schedules == 0 && softints == 1);
    base.IDNestCnt = -1;
    KrnSchedule();
    assert(base.ThisTask == (BASELINE ? &a : &b));
    reset(0);
    base.TaskReady.head = NULL;
    Reschedule();
    assert(base.ThisTask == &a);
    puts("reschedule-host: priority, Forbid, Disable, pending-switch and empty-queue controls passed");
    return 0;
}
