# AArch64 protection engine and fault fixture

The production engine is `arch/aarch64-all/kernel/setprotection.c`, exposed by
`protection.h`. Platform adapters supply mapping and synchronization policy.
The generic guarded-stack caller is `arch/aarch64-all/kernel/guarded_stack.c`.

## Reproduce

From the core checkout, with clang, ld.lld and qemu-system-aarch64 on PATH:

```sh
python3 developer/debug/test/aarch64-protection/run.py --output /absolute/new-output-4k
python3 developer/debug/test/aarch64-protection/run.py --page-bits 14 --output /absolute/new-output-16k
```

Output must be a new directory outside the source checkout. MetaMake scans
source subdirectories, including ignored build outputs.

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

## Product-controlled task proof

A product owns guest composition, boot policy, debugger setup and transcript
classification. Keep those drivers with the product, alongside its SDK and
binary identities. The common core supplies the controlled Exec-task workload
and the portable fault-matching checks; this directory's standalone EL1 run
qualifies its own setup.

The controlled workload requires an unguarded store, a fault attributed to the
registered task and exact PC/FAR/exception attributes, restored stack registers,
child continuation and parking, parent removal of the child, and successful
stack release. Subsequent loader/ABI and startup completion must also succeed.
Transcript checks require one loader boundary, exact single markers in order
within each stream, successful runner exit, an empty diagnostic trace and no
failure token after successful markers. A missing loader boundary is a failure;
the complete serial stream must remain available for diagnosis. Separate serial
and startup streams do not establish chronology between streams without an
independent observation record.

Every-task stack ownership and general overflow attribution/termination require
separate acceptance. A caller-owned arena stack must not be added to an ordinary
AllocMem/FreeEntry ownership list. SMP acceptance also requires the mapping
synchronization and quiescence contract above. The standalone fixture emits no
`[MP1] PASS` claim.

## Provenance

Implementation is original APL 1.1 code. Architectural facts: Arm DDI 0487,
VMSAv8-64 translation table descriptors and break-before-make requirements;
[Arm's Memory Management guide](https://developer.arm.com/documentation/101811/latest).
No Linux or other GPL implementation was copied. The fixture's UART address
is QEMU virt's PL011 device, not a production hardware discovery mechanism.
