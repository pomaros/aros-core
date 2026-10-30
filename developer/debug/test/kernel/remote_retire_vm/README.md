# Remote task retirement in AROS SMP

APL 1.1, Copyright (C) 2026 John KNIPPER.

Build RemoteRetire and RemoteRun with the matching SMP SDK; install both in
VMWORK0:System/C and run RemoteRun through the bounded QEMU supervisor with two
CPUs. The controller is pinned to CPU 0 and each victim to CPU 1. Eight compute,
eight voluntary-switch and eight waiting victims are removed, then their shared
counters must remain unchanged. The controller is also removed before the native
parent returns. Require the unique RETIRE PASS marker and observed-command-rc=0.
This is a native Exec lifetime prerequisite, not restricted execution or Wine.

The compute victim never yields voluntarily: stopping it requires remote
scheduling. The switching case exercises outgoing-context publication; source
inspection and HOST negative controls supplement it but do not prove that every
race timing occurred in one VM run. Preserve stopped-guest evidence on failure.
