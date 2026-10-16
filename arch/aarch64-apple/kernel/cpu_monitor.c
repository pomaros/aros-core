/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <aros/config.h>
#include "cpu_monitor.h"
#include "../exec/tls.h"
#include <aros/apple/runtime_local.h>
static struct AROSCPUTopology topology;
/* The anchor belongs to a CPU, never to a task context. EL1 installs it;
 * EL0 may read it without reserving x18 or accessing TPIDR_EL1. */
struct CPURecord {
    _Alignas(64) struct AROSAppleRuntimeLocal runtime;
    _Alignas(64) struct AROSCPUAccounting accounting;
    unsigned index;
    tls_t exec;
};
_Static_assert(offsetof(struct CPURecord, runtime)==0, "anchor starts with assembly runtime fields");
static struct CPURecord records[AROS_CPU_MAX];
static struct AROSAppleRuntimeLocal bootstrap_runtime;
static unsigned initialized;
static int firmware_valid;
static struct AROSAppleCPUStats stats;
_Static_assert(AROS_CPU_MAX <= AROS_APPLE_CPU_STATS_MAX, "CPU inventory fits descriptor");
const struct AROSAppleCPUStats *AROSAppleCPUMonitorStats(void)
{
    return __atomic_load_n(&initialized, __ATOMIC_ACQUIRE) ? &stats : 0;
}
static uint64_t counter(void)
{
    uint64_t v;__asm__ volatile("isb\n\tmrs %0, cntvct_el0":"=r"(v));return v;
}
static uint64_t affinity(void)
{
    uint64_t v;__asm__ volatile("mrs %0, mpidr_el1":"=r"(v));return v&AROS_CPU_AFFINITY_MASK;
}
void AROSCPUInitMonitor(const void *blob, uint64_t bytes)
{
    struct AROSEarlyFDT fdt;
    uint64_t boot=affinity();
    firmware_valid=AROSEarlyFDTOpen(&fdt,blob,bytes) && AROSCPUDiscover(&fdt,boot,&topology);
    if (!firmware_valid) {
        /* Report the observed boot core, with unknown firmware inventory. */
        topology.count=1;topology.boot_cpu=0;topology.psci_available=0;
        topology.cpu[0].affinity=boot;topology.cpu[0].enabled=1;
        topology.cpu[0].method=AROS_CPU_START_UNKNOWN;topology.cpu[0].release_address=0;
    }
    for (unsigned i=0;i<topology.count;i++) records[i].index=i;
    records[topology.boot_cpu].runtime=bootstrap_runtime;
    for (unsigned i=0;i<topology.count;i++) {
        records[i].runtime.exec_tls=&records[i].exec;
        records[i].exec.CPUNumber=i;
        records[i].exec.IDNestCnt=-1;
        records[i].exec.TDNestCnt=-1;
        records[i].exec.Quantum=4;
    }
    if (!AROSCPUBindCurrent()) return;
    stats.version = AROS_APPLE_CPU_STATS_VERSION;
    stats.size = sizeof(stats);
    stats.boot_index = topology.boot_cpu;
    stats.cpu_count = topology.count;
    for (unsigned i = 0; i < topology.count; ++i)
        stats.accounting[i] = &records[i].accounting;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(stats.frequency));
    __atomic_store_n(&initialized,1,__ATOMIC_RELEASE);
    AROSCPUCurrentState(AROS_CPU_BUSY);
}
int AROSCPUBindCurrent(void)
{
    __asm__ volatile("msr tpidrro_el0,xzr\n\tisb" ::: "memory");
    uint64_t id=affinity();
    for (unsigned i=0;i<topology.count;i++)
        if (topology.cpu[i].affinity==id && topology.cpu[i].enabled) {
            __asm__ volatile("msr tpidrro_el0,%0\n\tisb"::"r"(&records[i]):"memory");
            return 1;
        }
    return 0;
}
static struct CPURecord *current(void)
{
    struct CPURecord *record;
    if (!__atomic_load_n(&initialized,__ATOMIC_ACQUIRE)) return 0;
    __asm__ volatile("mrs %0,tpidrro_el0":"=r"(record));
    return record;
}
struct AROSAppleRuntimeLocal *AROSAppleRuntimeLocal(void)
{
    struct CPURecord *record=current();
    return record ? &record->runtime : &bootstrap_runtime;
}
unsigned AROSCPUCurrentExecIndex(void)
{
    return current() ? TLS_GET(CPUNumber) : AROS_CPU_MAX;
}
unsigned AROSCPUCurrentIndex(void)
{
    struct CPURecord *record=current();
    return record ? record->index : AROS_CPU_MAX;
}
void AROSCPUCurrentState(uint32_t state)
{
    struct CPURecord *record=current();
    if (record) AROSCPUAccount(&record->accounting,state,counter());
}
const struct AROSCPUTopology *AROSCPUMonitorTopology(void) { return &topology; }
int AROSCPUMonitorFirmwareValid(void) { return firmware_valid; }
int AROSCPUMonitorSample(unsigned cpu, struct AROSCPUSample *sample)
{
    return cpu<topology.count && sample && AROSCPUSnapshot(&records[cpu].accounting,counter(),sample);
}

void AROSCPUAttachExec(void *sysbase, void *kernelbase)
{
    for(unsigned i=0;i<topology.count;i++) {
        records[i].exec.SysBase=sysbase;
        records[i].exec.KernelBase=(void **)kernelbase;
    }
}
