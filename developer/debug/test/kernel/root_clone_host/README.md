<!-- Copyright (C) 2026 John KNIPPER. AROS Public License (APL) Version 1.1. -->

# Privileged-root snapshot HOST regression

Tests the actual `AArch64ClonePrivilegedRoot` helper with ASan/UBSan for 4 KiB
and 16 KiB tables. It checks separate table ownership, unchanged source pages,
physical/cache/permission preservation, removed EL0 data access, UXN and explicit
PXN on formerly user-writable pages. Budget exhaustion, privileged RWX,
hierarchical restrictions and unused root entries are refused.

```sh
python3 developer/debug/test/kernel/root_clone_host/run.py \
  --out /absolute/fresh/output/directory
```

The six negative controls retain user access, omit UXN, omit explicit PXN,
ignore the table budget, admit an entry outside the configured root range,
and return an incorrect preflight count.
Each must reach its exact semantic assertion. Compiler failures, sanitizer
reports and timeouts fail the suite. Source and binary hashes and exact outputs
are recorded; dirty inputs require `--allow-dirty-diagnostic` and are not a
committed-source qualification.

This helper creates an unpublished snapshot. The allocator must ledger every
fresh table, including partially built trees on failure. The caller must freeze
and retain source tables/backing, publish with proper barriers, and arrange
coherency with subsequent trusted-root mapping changes. It is not a task/root
switch, live kernel mapping mirror, allocator teardown or production admission
mechanism. Its scope is TTBR0 only; TTBR1 and the complete register/privilege
configuration require separate platform validation.

`AArch64CountPrivilegedRoot` shares the clone traversal and descriptor checks.
The preflight performs no allocations or cache operations, counts repeated
source subtrees separately and preserves its output on refusal. It accepts a
resolve-only callback table. Both 4 KiB and 16 KiB paths are checked. A native
caller can use the count to prepare owned storage outside a coordinator, but
must revalidate after dropping the source lock; this helper does not implement
pool replenishment or authorize a fixed-capacity allocator.
