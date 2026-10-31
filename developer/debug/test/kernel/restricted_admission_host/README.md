<!-- Copyright (C) 2026 John KNIPPER <code@jkn.me> -->
<!-- Licensed under the AROS Public License (APL) Version 1.1. -->

# Native trap admission fixture

Run `python3 run.py --out /absolute/fresh/path/outside/source`.
The runner extracts the actual outer restricted trap entry and its private
service constants. Inner operation dispatch and scheduler calls are stubs.
Every native service, including wake, state, suspend and resume, must reach
inner dispatch from a native user task. Unrelated services, missing contexts,
wrong vectors and privileged origins must be refused. Restricted faults/scalars
still reach their separate inner policy; a scheduling operation releases the
coordinator before entering the scheduler.

Three mutations must fail the intended assertion: restoring the old admission
range, accepting unrelated native services and admitting a privileged origin.
`--baseline-only` reproduces a defective source without constructing controls.
This does not test the inner service policy or actual exception assembly. It
closes a gap left by fixtures that entered service handlers directly.
