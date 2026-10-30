<!-- Copyright (C) 2026 John KNIPPER. AROS Public License (APL) Version 1.1. -->

# Real-MMU owned-buffer/service proof

Links the actual portable buffer/service code into the existing restricted-domain
QEMU harness through its optional fixture hooks. Run with Clang/LLD and QEMU:

```sh
python3 developer/debug/test/kernel/restricted_domain_qemu/run.py \
  --buffer-proof --cc /path/to/clang --out /absolute/fresh/output/directory
```

Two EL0 contexts use distinct backing at the same VA and alternate 2,000 times.
Their SVC byte transformation goes through the buffer registry, copy leases and
actual service coordinator. Caller identity comes from the fixture's current
context, not a caller-supplied principal. No guest address reaches the worker's
byte transform. The transform is synchronous fixture work at EL1; native AROS
worker scheduling is a separate test, not claimed here.

The platform callback uses `AT S1E0R` / `AT S1E0W` and `PAR_EL1` with each root
to check real EL0 permissions and expected physical backing. Only known owned
anonymous allocations can be returned as trusted aliases. Root selection,
barriers and full TLB invalidation are serialized on one CPU; the prior root is
restored. The test's page size is 4 KiB and caches are disabled.

Each domain also checks real cross-page copies; foreign/stale identities;
overflow; guard/unmapped coverage; independent ledger and hardware read-only
refusals; and permission changes between pin and copy without partial writes.
A queued request is cancelled, its record reused, and a delayed worker token
refused. Running cancellation holds backing until acknowledgement. Pages are
unmapped and reassigned only after drain; stale completion/collection cannot
write the reassigned backing. Completion-before-close also refuses publication.
All request/lease/mapping/domain ownership drains for both domains.

The baseline must print exact buffer counts plus the original domain proof
counts. Six broken variants must fail at their expected assertions: foreign
domain, stale domain, foreign page, omitted ledger permission, early lease
release on cancellation and stale worker completion. QMP stopped-register
preflight occurs before execution; timeouts capture registers and fail. Inputs,
ELFs, compiler/QEMU versions and commands are recorded in the result receipt.
Dirty input success is diagnostic-only.

This freestanding proof is not an AROS boot or production restricted scheduler.
It does not qualify SMP, faulting/shared/file-backed buffers, user callbacks,
cache coherency or production alias-wide W^X. Kernel identity RAM remains EL1
RWX. The original kernel read/write, guard and NX fault checks remain active.
