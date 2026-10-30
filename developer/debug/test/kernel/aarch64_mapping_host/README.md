<!-- Copyright (C) 2026 John KNIPPER <code@jkn.me> -->
<!-- Licensed under the AROS Public License (APL) Version 1.1. -->

# Generic AArch64 mapping HOST checks

Run `python3 run.py --out /ABS/NEW-OUTPUT` from any directory. The runner uses
`/usr/bin/clang` by default (`--cc` overrides it), runs each compiler/test in
sequence, and refuses output reuse. It refuses dirty input unless `--allow-dirty-diagnostic` is specified; that
mode records DIAGNOSTIC_PASS. Final receipts bind the clean source commit.
It compiles the actual generic mapping.c
and current setprotection.c, with C11, strict warnings, ASan and UBSan. There
is no copied walker or host-pointer-as-physical-address shortcut.

The fixture records synthetic physical table addresses separately from their
HOST allocations. It uses independent byte/descriptor expectations and observes
cache-publication callbacks, readable-NX state during executable cache sync,
exact PA ownership, output sentinels and table reachability. It covers 4K/16K,
25/30/32/39/42/48-bit VA geometry, empty-hole creation, neighboring leaves,
retained guards, exact-owner remap/unmap, existing block/restricted-table
refusal, resident block/page offsets, invalid geometry/addresses/descriptors,
root ASID/CnP versus misalignment, missing callbacks and allocation failures.

Every returned allocation stays in the fixture ledger. Failure after publishing
an intermediate table retains that reachable table; a malformed/unresolvable
allocation remains explicitly caller-owned and unpublished. All HOST allocations
are freed when the synthetic address space is destroyed. Published tables are
not automatically reclaimed by leaf unmap. A real platform must bound/account
that address-space ownership, or detach/prune with the required synchronization.
It must not equate the number of allocate calls with successfully linked pages.

Five separately compiled semantic mutants must fail C assertions: wrong unmap
owner, accepting the JIT bit, dropping byte offsets, ignoring physical width and
accepting tagged geometry. Compilation failure or an unrelated sanitizer crash
is not an accepted negative control. Inputs are hashed before/after replay;
commands, identities, stdout/stderr and true return codes are recorded.

The current HOST sanitizer does not support LeakSanitizer. The runner explicitly
disables that unsupported option while retaining AddressSanitizer and UBSan;
fixture-ledger teardown is explicit. This is not an LSan result.

The helpers intentionally refuse JIT32 and W+X even though the shared protection
engine has a separately admitted platform JIT mode. Ordinary RX helper tests
verify descriptor/cache mechanics only. A platform must separately enforce
permissions across every alias of a physical page before exposing executable
anonymous mappings. The Remap helper verifies the expected PA and only changes
permissions on the existing leaf; it never changes backing or splits blocks.

HOST callback traces do not execute a real MMU, TLB, cache, scheduler or SMP
rendezvous. No native Apple backend, context, DOS API, build registration, SDK,
QEMU or M1 path is activated by these tests. Those require separate source and
runtime qualification.

The inactive-root user-page admission query checks EL0 permissions, access flag,
normal-memory index, valid leaf and parent descriptors without mutating tables
or switching roots. Both page sizes are exercised; deliberate removal of EL0
and write-permission checks must fail their corresponding assertions. This
query does not prove backing ownership, lifetime or native hardware behavior.
