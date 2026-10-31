<!-- Copyright (C) 2026 John KNIPPER. AROS Public License (APL) Version 1.1. -->

# Native metadata allocation rollback

The runner extracts the actual native reservation/shared allocation wrappers
and record declarations from restricted.c. It supplies a page allocator with
injected failures, native exclusion counters and publication callbacks, then
runs ASan/UBSan. The only instruction substitution is fatal `wfe` to `abort`.

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
