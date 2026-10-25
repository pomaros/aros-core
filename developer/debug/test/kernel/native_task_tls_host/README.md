# AArch64 native task context checks

Run `python3 -B run.py --out /absolute/path/to/new-output`. Outputs must be in
a fresh directory. HOST Clang supplies ASan/UBSan; Homebrew LLVM assembles
freestanding AArch64 objects. The script does not build or mutate an SDK.

The fixture compiles the actual production STORE_TASKSTATE and
RESTORE_TASKSTATE macros from `arch/aarch64-native/kernel/task_context.h`.
Only Task/ETask field adapters and HOST register reads/writes are substituted.
It checks distinct full-width pointers, first-use zero, foreign non-pthread
tasks, simulated CPU migration, live lazy updates, GPR/SP/SIMD preservation,
and refusal to copy the vector frame's unused padding as a TP snapshot.
The same actual macros and private wrapper compile into an AArch64 object
with genuine MRS/MSR TPIDR_EL0. The complete `vectors.S`, and exact assembly
string tokens from the primary entry and SMP trampoline, assemble too.

## Source contract and limits

The native Raspberry Pi backend has its own scheduler, context allocator,
PrepareContext and CPU entry paths. Its task-owned context retains the public
280-byte prefix, with private TP at 280, SIMD at 288 and an 816-byte total.
Static assertions tie the owned allocation to the vector/SIMD constants.
AllocMem aligns to MEMCHUNK_TOTAL (at least AROS_WORSTALIGN, 16 on AArch64);
the creator asserts that contract before using a fixed SIMD offset.
KrnCreateContext explicitly initializes TP to zero; normal Exec tasks and SMP
bootstrap tasks both allocate through it. The kernel CPU anchor remains
TPIDR_EL1, independent of application TPIDR_EL0. Primary and secondary entry
clear the application register before C; an empty dispatcher clears it before
waiting for work. Real per-CPU idle tasks use ordinary zero-initialized task
contexts. Save occurs before Wait's lock release and ready-list publication,
so existing task ownership locks publish TP along with other saved registers.
Restore occurs with the incoming frame, before the CPU dispatcher's incoming
callbacks. No-switch vectors leave the live value untouched.

Native TLS is valid only in task execution context. The runtime contract
excludes interrupts, scheduler tc_Switch/tc_Launch hooks and a first allocating
TLS access under Disable. In particular, tc_Switch executes after the initial
snapshot and must not install or change a TCB. This restriction also applies
to the shared scheduler included by the Apple SMP backend and to the generic
non-SMP dispatcher. No generic scheduler callback ABI is changed here.

These checks establish source/layout/object properties, not actual native
task switching, SMP migration or Raspberry Pi hardware acceptance. Qualify a
separate coherent kernel generation with two real tasks (including a foreign
non-pthread task), forced Wait/Signal switches, zero inheritance, idle/wakeup,
actual CPU identities proving migration, and no-switch task-context library
callbacks. The Apple implementation needs its own native qualification too;
see [its fixture](../apple_task_tls_host/README.md). Hosted AArch64 retains a
different host thread-pointer contract.

Compiler native-TLS defaults, TLSDESC relocation/resolution, module identity,
TCB allocation/destructors/unload and task retirement remain separate work.
In particular, a cleanup callback cannot read et_RegFrame after RemTask has
freed it, nor run C++ destructors in a hidden foreign/scheduler context. No
native TLS compiler defaults or cleanup callbacks are installed by this patch.
