# Restricted AROS task demonstration

This experimental single-CPU workload invokes the native restricted control
API from two pthread tasks. Each payload runs at the same code/data/stack
addresses with separate backing, validates TLS/x18 across explicit yields,
and tries three forbidden SVCs. A fourth native worker receives only bounded
staging copies through the production W-BUF service boundary. Each payload
checks 100 outer replies and 100 callback replies, rejecting four invalid
input/output ranges and one invalid callback PC; the worker
checks duplicate-completion refusal. One then faults on kernel memory; its native
wrapper receives a fault result while the other payload completes and a third
native task continues its heartbeat. The parent observes the command's exit.

Build RestrictedTasks.c with payload.S, linking the matching SDK's pthread
and standard runtime archives. Build RestrictedRun.c with the same SDK.
Install both under VMWORK0:System/C and launch RestrictedRun in a bounded,
locked QEMU profile with register access qualified before execution.

The experimental API admits one page of code, data and stack per invocation.
It restores the native wrapper after completion/fault and releases its owned
backing. It implements bounded worker services. It supports one callback frame and worker reentry, but does not implement resumable fault
upcalls, external task removal or SMP reclamation. SMP admission is disabled.
The source is an integration candidate; a compile is not runtime acceptance.

The API version is 3. Service submit/collect use kernel-derived task identity;
collection yields when a response is unavailable. Close cancels and drains
worker leases from native context before reclaiming backing; adversarial
cancellation/close timing requires separate runtime qualification.

Callbacks target aligned instructions inside the owned immutable code page.
The kernel retains the full suspended context and restores it only after the
callback returns with no outstanding request. Callback code may issue a nested
worker operation, but nested callback frames are refused. Arbitrary callback
exception handling and malicious continuation tests remain separate gates.
