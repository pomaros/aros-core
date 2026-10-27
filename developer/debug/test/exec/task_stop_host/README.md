# Detached-task observer regression

Run `python3 run.py --output /absolute/new/directory` for ASan/UBSan,
or add `--sanitizer thread` for ThreadSanitizer. The default SMP model
executes the actual registration/notification bodies and lock helper with
host list, CPU and interrupt adapters. It checks caller/state/version guards,
duplicate registration, removal joining an in-flight callback, and 2,000
concurrent registration/removal/free cycles. Unsupported platforms refuse
registration. Eleven compiled runtime mutations must fail. `--variant up`
is a synthetic serialization model, not a claim of UP platform support.
The deliberately stalled callback tests the join; production callbacks must
never wait, allocate, call Exec, take another lock or modify the observer list.

Run `python3 service.py --output /absolute/new/directory` to execute the
actual ServiceTask body with modeled queue/allocation operations. It checks
notification before freeing a detached task, resource version and platform
capability guards, and exclusion of REMOVED/TOMBSTONED fallthrough. Two
compiled mutations must fail. These fixtures do not execute the real scheduler,
interrupt controller, generated module linkage or guest workload.

The platform declares EXEC_SELF_EXIT_HANDOFF only when its producer retains
self-removing Task storage until execution has left its stack and TLS state.
The shared SMP adapter pairs this capability with EXEC_DEFER_SELF_EXIT and
its version-4 kernel bridge. The existing kernel/self_exit_host fixture covers
that producer separately. Other adapters refuse observer registration.
Rebuild Exec 51.12, task.resource 2.0 and consumers as one cohort.

Observers own their Hook and resident code/data until successful removal.
They may compare the Task address with independent records and publish atomic
state; they must not dereference its destroyed execution context. This event
proves neither remote RemTask quiescence nor completion by another actor still
holding an operation. Those require separate lifetime barriers.
