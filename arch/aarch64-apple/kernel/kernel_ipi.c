/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include "smp.h"
#define CORE_IPI_MAX_CPUS AROS_APPLE_SMP_MAX_CPUS
#define CORE_IPI_ONLINE_MASK() AROSAppleSMPOnlineMask()
#define CORE_IPI_SEND(msg, data, mask) AROSAppleSMPSendIPI(msg, data, mask)
#define CORE_IPI_AVAILABLE() 1
/* VM GIC transport uses IRQ; native Apple delivery also uses FIQ. */
#define CORE_IPI_MASKED(mask) (((mask) & UINT64_C(0xc0)) != 0)
#include "../../aarch64-native/kernel/kernel_ipi.c"
