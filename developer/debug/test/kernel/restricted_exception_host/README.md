<!-- Copyright (C) 2026 John KNIPPER <code@jkn.me> -->
<!-- Licensed under the AROS Public License (APL) Version 1.1. -->

# Exception delivery fixture

The runner extracts the production code-identity helpers, the deliverable
exception classes, the fault-stack record writer and the EXCEPTION_HANDLER,
RESTORE_CONTEXT and FAULT_HANDLER cases with the admission rule that applies
while a delivered exception is active. A synthetic owned page, fault stack and
restore result replace the MMU, scheduler and exception return.

Run `python3 run.py --out /absolute/fresh/path/outside/source`.
The sanitized baseline must copy the complete faulting context, ESR, FAR and
generation into the record at the top of the fault stack, enter the handler
with the documented registers, count the delivery, refuse nesting, repair,
callback, wait, service, stale-handler, small-stack and non-guest exception
classes, refuse registration during a repair, deny every SVC but EXIT and
RESTORE_CONTEXT while active, and clear the exception state only on a
successful restore. Eight separately compiled semantic mutations must fail
the intended assertion.

Guest acceptance must separately take a real data abort through the handler
and a restore into Windows-style dispatch code on QEMU. This fixture is not
evidence of those runtime paths.
