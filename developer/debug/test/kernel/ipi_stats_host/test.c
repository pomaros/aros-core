/* Copyright (C) 2026 John KNIPPER. AROS Public License (APL) Version 1.1. */
#include "smp.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t pending[AROS_APPLE_SMP_MAX_CPUS],notify[AROS_APPLE_SMP_MAX_CPUS];
volatile struct AROSIPIStats AROSAppleIPIStats[AROS_APPLE_SMP_MAX_CPUS];
static unsigned cpu,online,disabled,deliveries,last_mask;
static uint64_t AROSAppleRuntimeIRQSave(void) { assert(!disabled);disabled=1;return 7; }
static void AROSAppleRuntimeIRQRestore(uint64_t saved) { assert(disabled && saved==7);disabled=0; }
static unsigned AROSCPUCurrentIndex(void) { return cpu; }
uint32_t AROSAppleSMPOnlineMask(void) { return online; }
static uintptr_t notify_ipi(void) { assert(disabled);++deliveries;last_mask=notify[cpu];return 1; }
static uintptr_t AROSAppleRuntimeSupervisor(uintptr_t (*fn)(void)) { return fn(); }
#include "actual.inc"
int main(void)
{
    online=3;cpu=0;
    AROSAppleSMPSendIPI(AROS_APPLE_MEMORY_IPI,0,6);
    assert(deliveries==1 && last_mask==2 && pending[1]==AROS_APPLE_MEMORY_IPI && !pending[2]);
    assert(AROSAppleIPIStats[0].sent==1 && AROSAppleIPIStats[0].memory==1);
    AROSAppleSMPSendIPI(AROS_APPLE_MEMORY_IPI,0,2);
    assert(deliveries==2 && AROSAppleIPIStats[0].already_pending==1);
    AROSAppleSMPSendIPI(0,0,2);
    assert(deliveries==3 && AROSAppleIPIStats[0].zero==1 && pending[1]==AROS_APPLE_MEMORY_IPI);
    pending[1]=0;cpu=1;
    AROSAppleSMPSendIPI(AROS_APPLE_TIMER_IPI|8,0,1);
    assert(pending[0]==(AROS_APPLE_TIMER_IPI|8) && last_mask==1);
    assert(AROSAppleIPIStats[1].timer==1 && AROSAppleIPIStats[1].other==1 && AROSAppleIPIStats[1].sent==1);
    unsigned before=deliveries;cpu=AROS_APPLE_SMP_MAX_CPUS;
    AROSAppleSMPSendIPI(1,0,3);assert(deliveries==before && !disabled);
    cpu=0;AROSAppleSMPSendIPI(0xf0000000,0,2);
    assert(pending[1]==0 && AROSAppleIPIStats[0].zero==2 && deliveries==before+1);
    assert(sizeof(struct AROSIPIStats)==64);
    puts("PASS notification delivery unchanged and counter classes checked");
    return 0;
}
