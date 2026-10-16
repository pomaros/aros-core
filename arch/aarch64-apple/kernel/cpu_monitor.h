/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef AROS_APPLE_CPU_MONITOR_H
#define AROS_APPLE_CPU_MONITOR_H
#include "cpu_topology.h"
#include "cpu_accounting.h"
void AROSCPUAttachExec(void *sysbase, void *kernelbase);
void AROSCPUInitMonitor(const void *fdt, uint64_t bytes);
/* EL1 only, local interrupts masked. Only the executing CPU becomes online. */
void AROSCPUCurrentState(uint32_t state);
int AROSCPUBindCurrent(void); /* EL1, before secondary uses any local state. */
unsigned AROSCPUCurrentIndex(void);
unsigned AROSCPUCurrentExecIndex(void); /* EL0-readable, AROS_CPU_MAX if unbound. */
const struct AROSCPUTopology *AROSCPUMonitorTopology(void);
int AROSCPUMonitorSample(unsigned cpu, struct AROSCPUSample *sample);
int AROSCPUMonitorFirmwareValid(void);
/* Opt-in isolated CPU execution probe; never advertises Exec SMP readiness. */
uintptr_t AROSCPUProbeStart(void);
int AROSCPUProbeDrain(void);
uintptr_t AROSCPUProbeStop(void);
int AROSCPUProbeCPU(unsigned cpu);
void AROSCPUProbeTick(uint64_t seconds);
int AROSCPUProbeHandleIPI(uint32_t id);
int AROSCPUProbeIPIPassed(void);
void AROSCPUProbeIPICounters(uint32_t *sent,uint32_t *handled,uint32_t *acked);
const struct AROSAppleCPUStats *AROSAppleCPUMonitorStats(void);
#endif
