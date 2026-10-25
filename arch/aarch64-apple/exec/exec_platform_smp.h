/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * SMP adapter for the Apple guest scheduler. */
#ifndef APPLE_EXEC_PLATFORM_SMP_H
#define APPLE_EXEC_PLATFORM_SMP_H
#ifndef __AROSEXEC_SMP__
#error "The Apple SMP Exec adapter requires an SMP Exec build"
#endif
#include <aros/apple/runtime.h>
#include "tls.h"
#include "smp_api.h"
#include "../../aarch64-native/exec/exec_platform.h"
/* VM IPIs arrive as IRQs, native timer ticks as FIQs. Exclude both locally.
 * No blocking or scheduler syscall may occur before the matching restore. */
#undef EXEC_FIQ_DISABLE
#undef EXEC_FIQ_RESTORE
#undef EXEC_IRQFIQ_DISABLE
#undef EXEC_IRQFIQ_RESTORE
#define EXEC_FIQ_DISABLE() AROSAppleExecSMPAPIGet()->irq_save()
#define EXEC_FIQ_RESTORE(mask) AROSAppleExecSMPAPIGet()->irq_restore(mask)
#define EXEC_IRQFIQ_DISABLE() AROSAppleExecSMPAPIGet()->irq_save()
#define EXEC_IRQFIQ_RESTORE(mask) AROSAppleExecSMPAPIGet()->irq_restore(mask)
#undef EXEC_SPINLOCK_INIT
#undef EXEC_SPINLOCK_LOCK
#undef EXEC_SPINLOCK_UNLOCK
#define EXEC_SPINLOCK_INIT(lock) AROSAppleExecSMPAPIGet()->spin_init(lock)
#define EXEC_SPINLOCK_LOCK(lock,hook,mode) AROSAppleExecSMPAPIGet()->spin_lock(lock,hook,mode)
#define EXEC_SPINLOCK_UNLOCK(lock) AROSAppleExecSMPAPIGet()->spin_unlock(lock)
/* A message port's lock, taken with IRQ and FIQ masked (PutMsg, ReplyMsg
 * from the timer's FIQ, GetMsg): bounded, see smp_bridge.c. */
struct MsgPort;
void AROSAppleExecPortLock(struct MsgPort *port);
#define EXEC_PORT_LOCK(port) AROSAppleExecPortLock(port)
#endif
