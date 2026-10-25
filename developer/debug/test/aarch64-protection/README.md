# AArch64 protection engine and fault fixture

The production engine is `arch/aarch64-all/kernel/setprotection.c`, exposed by
`protection.h`. The Apple target's existing `setprotection.c` adapter compiles
and calls it; hosted AArch64 continues to use its host implementation. The
generic guarded-stack caller is `arch/aarch64-all/kernel/guarded_stack.c`.

## Reproduce

From the core checkout, with clang, ld.lld and qemu-system-aarch64 on PATH:

```sh
python3 developer/debug/test/aarch64-protection/run.py --output developer/debug/test/aarch64-protection/artifacts/4k
python3 developer/debug/test/aarch64-protection/run.py --page-bits 14 --output developer/debug/test/aarch64-protection/artifacts/16k
```

`CLANG`, `LD_LLD` and `QEMU` can override the executables. Output directories
retain the executable images, host/serial logs, QEMU trace and JSON verdict.
Host tests compile the actual engine with AddressSanitizer and UBSan. They
exercise 4KB/16KB, L0/L1/L2 roots, preserved neighbor mappings, guard restoration,
EL0/EL1 execute flags, W+X rejection, unsupported modes, invalid ranges,
allocation exhaustion, platform split restrictions, parent restrictions and
contiguous descriptors. Guarded-stack lifecycle tests include partial creation
failure, failed restore retention and successful cleanup retry.

The QEMU adapter boots one CPU at EL1 (Cortex-A72 for 4KB, `max` for 16KB),
checks granule support, programs real stage-1 tables,
and exercises the same engine. It first writes the unprotected guard address
as a negative control. It then catches the expected translation fault at that
address, destroys the guarded allocation and restores writes, converts a guard directly to executable read-only
code, executes it, catches a write permission fault, rejects W+X, and restores
writes again. The exception handler checks ESR's class, write bit and level,
and FAR before advancing the faulting store; an unexpected exception fails.

The allocation uses a 16KB guard followed by a 64KB stack region; on the 4KB
lane the guard spans four pages. The first stack word is independently written
to check that the adjacent stack remains usable.

Passing requires the exact marker, exit status zero, an empty guest-errors/
unimplemented trace, and no reserved failure token. A timeout fails even if a
PASS marker preceded it. The fixture explicitly enables PL011 before output.

## Platform integration requirements

This code implements baseline <=48-bit stage-1 TTBR0 descriptors, with 4KB or
16KB granules derived from TCR. It rejects 64KB, LPA2/DS, hardware AF/dirty
updates, contiguous mappings and restrictive parent-table attributes. Table
software bit 58 is reserved for reversible guards; existing owners must agree
to that allocation before adopting it. It preserves cache and shareability
attributes rather than interpreting platform-specific MAIR indices.

The caller must serialize **all** address-space mutation, and quiesce users
of affected mappings while break-before-make temporarily removes them. A
spinlock alone prevents competing editors but cannot stop another CPU fetching
from the invalid interval. The update code, stack, exception vectors and table
aliases must themselves remain mapped. The fixture deliberately locates its
guard in a separate block from that machinery. Allocation callbacks must not
sleep; failed allocations may leave equivalent table splits but no permission
change. One call changes one aligned page; no range-atomic promise is made.

The Apple adapter uses the existing 4KB, 48-bit identity map and EL0-to-EL1
supervisor callback. Stage two already pre-splits the mutable page arena;
ordinary arena leaf changes therefore allocate no tables, even under pressure.
Only MAIR index 0 (Device-nGnRnE) blocks may split live. Normal RAM blocks are
rejected because they may contain executing code, stack or table aliases.
Explicit shared-execution policy preserves AROS library text execution at both
EL0 and EL1 while keeping it read-only. The generic default grants execute only
at the requested privilege.

The adapter rejects interrupt callers and builds with `__AROSEXEC_SMP__` until
shared mapping locks and CPU quiescence exist. On this single-CPU target,
`Forbid` protects the operation record; SVC exception entry masks interrupts
during the actual walk. This is not a general SMP synchronization mechanism.
The upstream native mapper's smaller roots and EL1 policy still need their own
adapter; it is not silently replaced.

The native `AROSAppleGuardedStackCreate/Destroy` binding allocates from the
pre-split arena, guards 16KB, and retains backing on a failed restoration.
`KrnFreePages` likewise uses the status-bearing internal protection API and
does not free after a failed restore. These primitives are compiled into the
kernel but are not yet connected to every Exec/DOS stack creator.

## Remaining MP1 acceptance work

The spec's 16KB guard span is implemented over the current 4KB granule. Task
stack allocation/free must adopt the new guarded-stack primitive, with bounds
metadata for fault attribution. SMP activation requires the synchronization
and quiescence mechanism described above.
The opt-in native fixture is selected by the decimal `/chosen` property
`aros,mp1-proof=1`. It creates an actual Exec task with a caller-owned 64KB
stack and a 16KB guard, then uses the same assembly store for an unguarded
control and the guarded probe. Recovery accepts only the registered task,
exact probe PC/FAR, EL0 synchronous L3 write translation fault and expected
stack registers. The fixed continuation restores the original SP before C.
The child signals its parent and parks; the parent checks `TS_WAIT`, removes
it, and only then releases its stack. Ordinary faults retain their existing
handling. Host tests reject wrong task, PC, FAR and exception attributes.

The native fixture passed a booted AROS run on 2026-10-15: no-guard control,
exact task guard hit, resumed/parked child removal and parent continuation,
then B2A W^X/unload and B2Q startup completion. QEMU exited zero and its
`guest_errors,unimp` trace was empty. Its `[MP1] PASS` marker is conditional
on both tasks and successful cleanup; it does not claim every task is
protected. The run required the upstream-merge ELF loader correction that
removes the obsolete final all-hunks RX pass (`ba3a9b0937`, equivalent to
`9173c12a1a`). Without that fix the controlled task passed, but later DOS
startup stalled: that earlier whole-lane verdict was a failure.

The base profile logs deferred library opens and unavailable deadline timer
before the proof, then passes its final startup checks. This evidence proves
controlled task progress, not timer-driven fairness or hardware support.

Every-task integration needs matching allocation and release changes in
`NewCreateTaskA` and DOS `CreateNewProc`: their `tc_MemEntry` records currently
own ordinary `AllocMem` stacks. An arena-backed pointer must not be placed in
those records, because `FreeEntry` would call the wrong allocator. A separate
stack ownership record and cleanup path, plus general fault attribution and
termination, are still required.

The standalone fixture proves descriptor/TLB/cache behavior and recoverable
EL1 faults for its own setup. It does **not** meet MP1's every-task-stack,
task-attribution or continued-AROS-operation requirements, and makes no Apple
hardware or SMP validation claim. No `[MP1] PASS` line is emitted.

## Reproduce the native controlled-task lane

Use an **external** UP Apple build directory. MetaMake scans the source tree,
including ignored directories, so placing build output under `artifacts/`
causes generated makefiles to be mistaken for source. Initialize the pinned
`rom/dos/catalogs` submodule before the build; `locale.library` needs its
`catalog_version.h`. The AROS compiler prefix must have verified compiler-rt
builtins; this runner does not install or modify that toolchain.

After configuring `--target=apple-aarch64 --with-toolchain=llvm
--with-aros-toolchain=yes --with-aros-toolchain-install=PREFIX --without-x`,
run `gmake -j2 apple-boot-payloads` in that external directory. Then:

```sh
python3 developer/debug/test/aarch64-protection/native.py \
  --build /absolute/path/to/apple-build \
  --integration /absolute/path/to/aros-apple-silicon \
  --toolchain /absolute/path/to/aros-crosstools \
  --output developer/debug/test/aarch64-protection/artifacts/native
```

The runner needs QEMU, `dtc`'s `fdtput`, GNU make and the integration loader's
LLVM/LLD tools. `--firmware` overrides the Homebrew EDK2 default. It compiles
the integration UEFI loader into its own output, builds the CLI ABI probe
against this native SDK, packages the same 20 components as the integration
base recipe, and injects the opt-in property into a private DTB. It never
rebuilds or writes the integration checkout. The ABI producer is native;
this does not claim parity with the separately locked hosted ABI corpus.

`result.json`, `inputs.json`, `prepare.log`, `runner.log` and `run/` retain
commands, component/firmware hashes and serial/semihost/QEMU evidence. A short
transient directory avoids the UNIX socket path limit; evidence is copied
back before cleanup. Acceptance requires each control/guard/continuation,
B2A and B2Q marker exactly once, in order, exit zero and an empty QEMU trace.
Every-task allocation, general overflow termination and SMP remain open as
listed above.

## Provenance

Implementation is original APL 1.1 code. Architectural facts: Arm DDI 0487,
VMSAv8-64 translation table descriptors and break-before-make requirements;
[Arm's Memory Management guide](https://developer.arm.com/documentation/101811/latest).
No Linux or other GPL implementation was copied. The fixture's UART address
is QEMU virt's PL011 device, not a production hardware discovery mechanism.
