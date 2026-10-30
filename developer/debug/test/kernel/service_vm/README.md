<!-- Copyright (C) 2026 John KNIPPER. AROS Public License (APL) Version 1.1. -->

# Service coordinator AROS task proof

`ServiceCoordinator.c` links the actual portable `service.c` and `domain.c`
into an AROS executable. Two client pthreads exchange 200 checked byte requests
with a worker pthread while a heartbeat pthread uses the native timer. A start
barrier checks four distinct Exec Task identities. All coordinator calls share
one pthread mutex; conditions provide deterministic worker/cancellation barriers.

Each client also cancels one request before worker admission and one after the
worker takes a lease. Stale tokens are refused after slot reuse; the heartbeat
must advance while a worker lease is held. Cancellation retains the queue until
acknowledgement and cannot publish a reply. Shutdown requires exact counts,
empty queue, reclaimable domain, four joined threads and destroyed mutex/cond.
`ServiceRun.c` observes the command's actual return code from `SystemTags`.
A host supervisor must enforce a deadline and capture a failed guest through an
independent debugger; a hung guest is not allowed to pass from partial markers.

Build against one coherent AArch64 AROS guest SDK using its startup, pthread,
POSIX, stdcio/stdc and Exec/DOS link libraries. `ServiceCoordinator.c` requires
`rom/kernel/mapping` on the include path and links `service.c` and `domain.c`.
`ServiceRun.c` is a separate executable. Install both under `System/C` on a
fresh disposable WORK image and run `VMWORK0:System/C/ServiceRun` from startup.
Record exact source, SDK, compiler, executable and image hashes with the verdict.
The driver must require the PASS counts, observed return 0 and shell completion,
reject failure/trap output and retain the independent preflight receipt.

This is a trusted user-space OS integration proof, not a registered kernel
service, protected process boundary or public ABI. The single-CPU test does not
qualify SMP. Native tasks share address space; user-memory validation, domain
handle lookup, callbacks and production dispatch/root/TLS integration remain
separate requirements. Guest service teardown does not establish filesystem
quiescence or a safe persistent whole-machine shutdown; use disposable disks.
