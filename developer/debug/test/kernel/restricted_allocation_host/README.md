<!-- Copyright (C) 2026 John KNIPPER. AROS Public License (APL) Version 1.1. -->

# Native metadata allocation rollback

The runner extracts the actual native reservation/shared allocation wrappers
and record declarations from restricted.c. It supplies a page allocator with
injected failures, native exclusion counters and publication callbacks, then
runs ASan/UBSan. Fatal `wfe` becomes `abort`; the table-transfer SVC is replaced by a modeled
publication callback. All allocation, rollback and table-chain release logic
is extracted unchanged.

```sh
python3 developer/debug/test/kernel/restricted_allocation_host/run.py --out /ABS/FRESH
```

Every allocation attempt in an eight-page shared object is failed in turn,
including its metadata. Publication rejection must free all pages. Success
transfers ownership until explicit release. Reservation allocation/publication
failure and a larger metadata record are also checked. The allocator asserts
exclusion and page-aligned lengths; the final ledger must be empty.

This is HOST evidence of wrapper rollback and ownership, not native allocator,
SMP, remote retirement or AROS execution. Production metadata uses the page
arena, avoiding Exec allocation callbacks that can Wait during Forbid.

Table supply checks metadata failure, page allocation failure, publication
refusal and successful ownership transfer for a 2048-page chunk. Teardown
reclaims a chain containing fully and partially used chunks with exact lengths.
No table count cap is inferred from fixture allocation tracking capacity.

Reservation retirement also extracts the production allocation link/unlink
helpers. Two rounds remove tail, middle and head records out of insertion
order, reject duplicate retirement, verify each survivor and reuse records.

API39 slice storage uses separate linked-view accounting and atomic native-free
references. The fixture tests all36 combinations of three-view detach and free
orders, including the root view disappearing first and a split from a non-root
view. Original chunk metadata remains available until the last native free;
each physical page and descriptor is released once and accounting reaches zero.
Allocation and publication refusal of the cut wrapper also reclaim its temporary
descriptor. This does not execute the privileged cut dispatcher or prove SMP
interleavings; the coordinator and actual guest path require runtime checks.
