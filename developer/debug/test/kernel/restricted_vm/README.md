# Restricted AROS task demonstration

This experimental single-CPU workload invokes the native restricted control
API from two pthread tasks. Each payload runs at the same code/data/stack
addresses with separate backing, validates TLS/x18 across explicit yields,
and tries three forbidden SVCs. A fourth native worker receives only bounded
staging copies through the production W-BUF service boundary. Each payload
checks 100 replies and rejects four invalid input/output ranges; the worker
checks duplicate-completion refusal. One then faults on kernel memory; its native
wrapper receives a fault result while the other payload completes and a third
native task continues its heartbeat. The parent observes the command's exit.

Build RestrictedTasks.c with payload.S, linking the matching SDK's pthread
and standard runtime archives. Build RestrictedRun.c with the same SDK.
Install both under VMWORK0:System/C and launch RestrictedRun in a bounded,
locked QEMU profile with register access qualified before execution.

The experimental API admits one page of code, data and stack per invocation.
It restores the native wrapper after completion/fault and releases its owned
backing. It implements bounded worker services. It does not implement callbacks, resumable fault
upcalls, external task removal or SMP reclamation. SMP admission is disabled.
The source is an integration candidate; a compile is not runtime acceptance.

The API version is 2. Service submit/collect use kernel-derived task identity;
collection yields when a response is unavailable. Close cancels and drains
worker leases from native context before reclaiming backing; adversarial
cancellation/close timing requires separate runtime qualification.
