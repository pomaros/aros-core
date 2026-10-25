# CPU21 implementation and pre-guest validation

Apple UP runtime is frozen at `c3cbe0a55e`, following initial implementation
`9df5e7c520`, power-base merge `ad0d85ffdf`, snapshot correction `8bfdf21a56`
and SMP-conditional regression `b04d84ef7c`. No public ABI or ETask layout changed.

Native `kernel-task` and `kernel-kernel` completed sequentially with exit 0.
The linked task.resource and kernel.resource hashes, build logs and host result
are recorded in [NATIVE-CPU-2026-10-16.json](NATIVE-CPU-2026-10-16.json).
The first task build failed because the resource referenced the kernel-private
AROSAppleRuntimeLocal symbol. The final implementation uses the switch-cleared
active marker; the failed log is retained. Final logs retain existing generated
Hook declaration and kernel format warnings.

`aros-build-cpu-accounting/host-003` passes ASan/UBSan for production arithmetic,
actual query-helper lifetime interleavings, copied StartTime after target exit,
and SMP unavailable behavior. Four deliberate regressions are rejected: retained
idle interval, omitted live slice, wall-clock denominator and premature target
dereference. The host service model does not prove guest scheduling or hardware.

Independent read-only review confirmed that normal switch paths clear the active
marker before idle; direct RemTask dispatch discards the exiting context. The
UP query copies supported fields before Enable/CloseDevice and skips unsupported
register queries, so mixed CPU/StartTime queries do not retain live task pointers.

Actual guest busy/idle/current-slice/exit-churn proof now passes, together with
a real producer-disabled negative control that correctly retains a feature FAIL.
See [the guest report](../../exec/task_cpu_accounting_vm/RESULT-2026-10-16.md)
for exact metrics, immutable artifacts and independent replay. The native manifest
above remains the historical pre-guest record. No actual sleep-cycle CPU,
SMP accounting or Apple hardware acceptance is claimed. See [README.md](README.md)
for sampling units, shared window semantics, lifetime contract and limitations.
