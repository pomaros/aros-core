/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Keep upstream private calls intact without cross-module ELF imports. */
#include "smp_api.h"
struct CallIPIEntry *core_ClaimCallIPI(int cpu) { return AROSAppleExecSMPAPIGet()->claim(cpu); }
void core_CommitCallIPI(struct CallIPIEntry *entry,int cpu,struct Hook *hook,int nargs,IPTR *args)
{ AROSAppleExecSMPAPIGet()->commit(entry,cpu,hook,nargs,args); }
void core_AbortCallIPI(struct CallIPIEntry *entry,int cpu) { AROSAppleExecSMPAPIGet()->abort(entry,cpu); }
void core_CancelCallIPIs(APTR hook,IPTR arg) { AROSAppleExecSMPAPIGet()->cancel(hook,arg); }
struct CallIPIEntry *core_TryClaimCallIPI(int cpu) { return AROSAppleExecSMPAPIGet()->try_claim(cpu); }
