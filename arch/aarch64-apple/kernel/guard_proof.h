/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#ifndef AROS_APPLE_GUARD_PROOF_H
#define AROS_APPLE_GUARD_PROOF_H
struct AROSAppleRuntimeFrame;
int AROSAppleGuardProofFault(struct AROSAppleRuntimeFrame *);
int AROSAppleGuardProof(void);
#endif
