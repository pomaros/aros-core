# Native AArch64 task pointer checks

Run `python3 -B run.py --out /absolute/path/to/new-output` from this directory.
The output directory must not exist. The script uses HOST Clang with ASan and
UBSan, and Homebrew LLVM for freestanding AArch64 objects. Neither an SDK nor
a kernel build directory is used.

The HOST fixture includes the production task-context helpers and public
ExceptionContext, adapting scalar Exec types and hardware register reads and
writes. It checks zero anchor contents, preservation of the public prefix and
SIMD storage, distinct stable anchor addresses, a non-pthread task, runtime
anchor updates, and migration between two simulated CPU registers. Source
checks cover register save before scheduler publication, dispatch restore, idle
clearing, context creation, crash saves, and the shared PSCI secondary entry. Actual
AArch64 register operations and the complete secondary entry assembly are
assembled and inspected. These are source/layout/assembly checks, not native
scheduling or SMP acceptance.

## Kernel contract

`TPIDR_EL0` holds the address of a task-owned runtime anchor. The context uses the
eight alignment bytes following the public 280-byte ExceptionContext. The
private prefix is 288 bytes, SIMD starts at 288, and the allocation stays 816
bytes. No public ExceptionContext or vector-frame field is added. The native
TLS runtime reads the anchor through the register, never a private context offset.
TPIDRRO_EL0 remains
the kernel's CPU anchor. x18 remains ordinary saved task state.

KrnCreateContext initializes every task's anchor contents to zero. cpu_SaveContext
preserves register and SIMD state without copying the anchor or a live TP value;
cpu_Dispatch installs the address of the selected task's anchor. That address
stays constant across runtime updates and CPU migration. The existing scheduler
ownership and publication locks carry saved registers across CPUs. Exception returns which
do not switch leave the live register alone. Idle and initial CPU entry clear
it. Stack swapping and setjmp/longjmp within one task preserve task identity
and do not change the register.

This fixture covers the Apple scheduler. The separate Raspberry Pi/native
backend owns its own STORE_TASKSTATE/RESTORE_TASKSTATE, allocator and entry
paths, checked by [its fixture](../native_task_tls_host/README.md). Both need
actual native switching and SMP qualification before generic native-TLS
compiler admission. Hosted AArch64 uses the host's thread-pointer contract
and requires separate analysis. Native TLS is unavailable in interrupts,
scheduler tc_Switch/tc_Launch hooks or a first allocating access under Disable;
these hooks must not allocate or change runtime TLS state.

This change does not allocate or release TCBs, install a TLS resolver, enable
native TLS in a compiler, or accept arbitrary local-exec modules. Runtime
module identity, allocation, task-end cleanup, and destructor ownership must
be qualified before native TLS applications are enabled. In particular,
RemTask currently deletes et_RegFrame before CleanupETask, and runs under an
outer Forbid: a cleanup callback must not read that freed frame or assume
schedulable destructor execution. A loader/runtime change needs an explicit
safe retirement contract, including foreign tasks and failed task creation.

## Native qualification for a separate kernel generation

Build and boot a coherent kernel generation containing this source; retain the
old SDK/kernel cohort. Use two actual Exec tasks, including a task created
without pthreads. Each verifies zero-initialized anchor contents and a distinct nonzero anchor
address, writes a distinct full-width sentinel into its anchor, repeatedly
Wait/Signal switches, and verifies both address and contents after every resume.
A same-task callback must change the sentinel and verify that a no-switch
SVC/interrupt return preserves both address and new contents. A third fresh task
must have its own zeroed anchor after its creator has installed nonzero contents. Include
idle/wakeup and task exit without enabling an allocating TLS runtime.

For SMP, force each task to resume on a different admitted CPU, record actual
CPU identities and values on both sides, and require a migration to have
occurred. A single-CPU or simulated-register pass cannot establish SMP
correctness. Follow with actual lazy TLSDESC modules, foreign callbacks,
retirement and unload/destructor tests when that runtime exists. QEMU can
qualify scheduling contracts; Apple hardware qualification is separate.
