<!-- Copyright (C) 2026 John KNIPPER <code@jkn.me> -->
<!-- Licensed under the AROS Public License (APL) Version 1.1. -->

# Dispatch cancellation fixture

Run `python3 run.py --out /absolute/fresh/path/outside/source`.
The runner extracts the actual CPU register/SIMD restore and root/TLS selection,
restricted stack/close check and native-frame restoration. Synthetic tasks,
contexts and coordinator functions exercise a domain closed while its thread is
not executing, an open domain, an invalid stack and an ordinary native task.
The baseline must return to the retained native frame on close, preserve its
SIMD and x18, restore native stack bounds, select native root/TLS and reapply
current interrupt masks. Four mutations must fail their exact intended checks.

The fixture does not execute the scheduler, task locks, MMU or exception-return
assembly. It establishes component behavior, not SMP cancellation qualification.
A coherent guest workload must separately close a domain while a member is
parked and prove that member resumes natively without executing user code.
