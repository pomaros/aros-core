# Restricted AROS task demonstration

This experimental UP/SMP workload invokes the native restricted control
API from two pthread tasks. Each payload runs at the same code/data/stack
addresses with separate backing, validates TLS/x18 across explicit yields,
and tries sixteen forbidden SVCs per task. A fourth native worker receives only bounded
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
backing. It implements bounded worker services. It supports one callback frame and worker reentry, and owned-page fault resumption. External worker/owner removal and SMP reclamation are exercised explicitly.
The source is an integration candidate; a compile is not runtime acceptance.

The API version is 9. Service submit/collect use kernel-derived task identity;
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
External task removal is exercised after the two-client workload.

A final restricted client submits a request to a raw Exec worker. The worker
claims its lease, signals its parent and parks. The parent verifies TS_WAIT
and calls RemTask; the architecture context-deletion hook cancels and retires
that lease before freeing the worker context. The client drains without output
publication and returns a checked snapshot. This tests parked-worker removal; the following owner-removal workload tests
deferred domain reclamation.

External owner retirement uses eight remove/complete cycles, exceeding the four
domain slots. Each raw Exec owner is removed while the native parent holds a
request: the domain closes but retains 1,028 pages until acknowledgement, then
all slots/backing drain and stale completions fail. The removed native caller's
result storage stays untouched. Kernel control slots serialize publication and exclusive reaping through the
coordinator. Allocation/release execute outside that lock on native stacks,
with Forbid protecting unpublished native backing against terminal freeze.
In SMP builds the heartbeat forces both clients across both CPUs. CPU 0 removes
the CPU-1 worker and eight CPU-1 owners while a second CPU-1 reaper polls; all
backing must drain. Three invalid-stack cases exercise YIELD, COLLECT and STATUS.

The payload checks full-width v0, v8 and v31 sentinels as well as x18/user TLS.
Task, callback and fault-handler vectors deliberately differ. Checks after
yields, nested services, fault resume and callback return require restoration
of the correct context, including the upper halves of the vector registers.
Compilation alone is not runtime qualification.

The production adapter separates address-space ownership from per-thread service
channels. Native worker handles identify generation-tagged channels independently
of domain handles. This workload still creates one domain per run invocation;
multi-thread execution in one root needs its separate integration workload.

Fault delivery switches to a separate zeroed RW/NX page with unmapped guards on
both sides. Native Task stack bounds follow the active handler and revert on
resume; the original SP stays in the kernel-retained continuation. The payload
checks the alternate SP and retained stack words across worker scheduling.
Handler replacement/unregistration is refused while a fault is active; zero
unregisters an idle handler. Code/backing remain immutable and retained until
channel drain. A nested fault terminates the invocation instead of recursively
consuming its alternate stack. Native control API v10 identifies this layout.
