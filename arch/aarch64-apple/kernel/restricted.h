/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef AROS_KERNEL_RESTRICTED_H
#define AROS_KERNEL_RESTRICTED_H
#include <aros/apple/restricted.h>
#include <aros/apple/runtime.h>
extern const struct AROSRestrictedAPI AROSRestrictedAPI;
struct Task;
void AROSAppleRestrictedCheckStack(struct AROSAppleRuntimeFrame *, struct Task *);
int AROSAppleRestrictedTrap(struct AROSAppleRuntimeFrame *);
/* Trusted EL1 only, with PageMapLock and completed remote rendezvous held.
 * Propagate arena alias policy without allocation; failure retains backing. */
int AROSAppleRestrictedProtectAliases(uintptr_t address,uint64_t tcr,unsigned permissions);
/* Same contract, with the restricted coordinator already held. */
int AROSAppleRestrictedProtectAliasesLocked(uintptr_t address,uint64_t tcr,unsigned permissions);
void AROSAppleRestrictedContextRetired(void *context);
#endif
