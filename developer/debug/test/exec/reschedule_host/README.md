# Explicit-yield regression

`python3 developer/debug/test/exec/reschedule_host/run.py` compiles the actual
`Reschedule`, `core_Schedule`, `core_Switch`, and `core_Dispatch` sources with the
normal UP scheduling flag definitions. The fixture supplies list and ABI glue;
it does not implement scheduling policy. No VBlank is injected: an explicit
yield must relinquish the remaining quantum without waiting for a timer tick.

The fixed source alternates 10,000 explicit yields exactly 5,000:5,000.
`--baseline` removes the quantum-expiry statement and checks that the original
failure returns (10,000:0). Both variants check higher/lower priorities,
Forbid/Permit-style deferral, Disable/Enable-style deferral, repeated pending
requests, and an empty ready queue. This is host logic coverage, not execution
of a real interrupt, context switch, or guest.

Build `test-exec-public` in a **uniprocessor** AROS build and run
`Tests/exec/reschedule_fairness` (or its staged equivalent) for the target test.
It runs two 10-second timer windows: equal-priority private counters with
`Reschedule()` on every iteration, then priority 1 against priority 0. The
controller has priority 20 and sleeps on timer.device; the workers do not sleep.
Both equal counters must be nonzero with ratio 0.9 through 1.1. The control
requires positive high-priority work and zero low-priority work. Task-allocation,
timer and SMP-configuration failures return `RETURN_FAIL`. Successful output
contains the SMP0 fairness and priority-control lines and exit status zero.

The aggregate ratio alone cannot prove that explicit yields work: a functioning
VBlank can time-slice the old code fairly over ten seconds. The deterministic
host regression distinguishes that defect without a timer. A guest pass must
still be retained separately with the platform harness's strict exit/error
checks. No guest or hardware result is claimed by these host tests.

## Cause and scope

`Reschedule()` promises to give up CPU time but originally set only scheduling
attention. `core_Schedule()` retains the current task when the ready head is
not higher priority and the quantum has not expired. Marking the current
quantum expired before scheduling fixes explicit yielding; the priority-sorted
ready queue still selects the highest-priority runnable task. Dispatch clears
the flag and reloads the quantum. Deferred requests retain both flags until
scheduling is permitted. Internal `Signal` and task-launch calls already check
higher priority; exception paths already require dispatch. This does not change
the separate scheduler policy for timer interrupts.

The September 5 native observation is consistent with this path, but the
original target run has not been reproduced here. The upstream VBlank expiry
repair (`8d6e27551f`) is already present in this tree; it does not make an
explicit yield expire an otherwise unexpired quantum. This patch makes no
claim about the separate Darwin hardened-runtime signal-context issue.
