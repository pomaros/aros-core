# Restricted AROS task demonstration

This experimental single-CPU workload invokes the native restricted control
API from two pthread tasks. Each payload runs at the same code/data/stack
addresses with separate backing, validates TLS/x18 across explicit yields,
and tries six forbidden SVCs. A fourth native worker receives only bounded
staging copies through the production W-BUF service boundary. Each payload
checks 100 outer replies and 100 callback replies, rejecting four invalid
input/output ranges and one invalid callback PC plus a nested callback installation; the worker
checks duplicate-completion refusal. One then faults on kernel memory; its native
wrapper receives a fault result while the other payload completes and a third
native task continues its heartbeat. The parent observes the command's exit.

Build RestrictedTasks.c with payload.S, linking the matching SDK's pthread
and standard runtime archives. Build RestrictedRun.c with the same SDK.
Install both under VMWORK0:System/C and launch RestrictedRun in a bounded,
locked QEMU profile with register access qualified before execution.

The experimental API admits one page of code, data and stack per invocation.
It restores the native wrapper after completion/fault and releases its owned
backing. It implements bounded worker services. It supports one callback frame and worker reentry, and owned-page fault resumption. It does not implement external task removal
or SMP reclamation. SMP admission is disabled.
The source is an integration candidate; a compile is not runtime acceptance.

The API version is 5. Service submit/collect use kernel-derived task identity;
collection yields when a response is unavailable. Close cancels and drains
worker leases from native context before reclaiming backing; the workload covers queued cancellation before worker creation, running
cancellation and termination while the worker retains a lease.

Callbacks target aligned instructions inside the owned immutable code page.
The kernel retains the full suspended context and restores it only after the
callback returns with no outstanding request. Callback code may issue a nested
worker operation, but nested callback frames are refused. Arbitrary callback
exception handling and malicious continuation tests remain separate gates.

The denied-call checks include the trusted worker take/complete interfaces and
a forged callback return without an active callback. Recursive callback setup
is refused once per domain while an ordinary nested worker call succeeds.

The first callback in each domain protects its owned data page read-only and
faults on a store. An executable owned handler receives the fault PC/address,
ESR and kernel-issued generation, restores write permission, calls the worker,
and resumes the original instruction using a kernel-retained frame. Resume
before permission repair, a forged PC and a forged token are rejected. The
outer callback and task continue; an unrelated kernel-memory fault terminates
only its own domain. This is a bounded resident-page fault-upcall contract,
not demand paging or general Windows exception support.

Before creating the worker, a separate restricted invocation submits and cancels
a queued request. Both concurrent payloads then cancel one held running request;
the faulting payload additionally exits through its fault while another held
request is live. The worker acknowledges cancellation and attempts duplicate
completion. The native result includes a data-page snapshot taken after drain
and before backing release: both cancelled output sentinels must stay unchanged,
all three domains drain, and close must wait for the faulting domain's worker.
External task removal remains a separate lifetime requirement.
