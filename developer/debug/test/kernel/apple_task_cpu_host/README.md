# Apple UP task CPU accounting

Run `python3 -B run.py --output /absolute/fresh/output` from this directory.
The harness compiles the production accounting header and freshly extracted
Apple query helper with ASan/UBSan. Four deliberately broken variants must fail.
It covers tick zero, current slices, idle with retained ThisTask, repeated small
windows, awake-time freeze, 56-bit wrap, normalization, long windows, and task
retirement during OpenDevice and CloseDevice. StartTime is copied before task
retirement; unsupported Apple register queries bypass generic task dereferences.

CPUTime is cumulative busy seconds/microseconds including a current running
slice. CPUUsage is unsigned Q0.32 (UINT32_MAX means 100%). The first query sets
a baseline; subsequent windows of at least 250000 awake microseconds update it.
Shorter queries retain the previous sample and baseline. GetUpTime supplies the
awake denominator. CNTVCT supplies busy intervals, stopped before entering idle.
Sampling state is shared per task, so multiple readers share a sampling window.

Qualified scope is Apple UP. SMP CPU fields remain zero/unavailable. This does
not connect the native SMP sweep or change other architectures or public ABI.
Callers retain the existing LockTaskList/NextTaskEntry contract. The UP helper
validates a raw address against live registry nodes while scheduling is disabled,
then copies values before re-enabling. It does not pin raw pointers across calls
or solve address reuse (ABA). No registry lock or device open occurs in FIQ.

The host mocks Exec scheduling, timer device and registry services; it proves
arithmetic and explicit lifetime interleavings, not actual guest scheduling,
hardware timing, interrupt behavior or SMP safety. Native compilation and an
independent guest workload are separate required validation lanes.
