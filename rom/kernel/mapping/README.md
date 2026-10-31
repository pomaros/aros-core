<!-- Copyright (C) 2026 John KNIPPER <code@jkn.me> -->
<!-- Licensed under the AROS Public License (APL) Version 1.1. -->

# Internal mapping components

These components provide independently testable mechanisms for virtual memory.
They are not registered in a platform build and do not export a DOS or POSIX
mapping API. Native fault delivery, filesystem operations, task ownership and
cross-CPU synchronization belong to their platform coordinator.

- `mapping.c` manages reservations, page-state generations, population, range
  retirement and ordered writeback batches under one caller-held mapping lock.
  It never allocates, blocks or performs filesystem I/O. Translation callbacks
  must acknowledge the required CPU quiescence and TLB invalidation before
  returning. Every alias participating in shared writeback must be included by
  the coordinator; the component cannot discover aliases.
  `KrnMappingReserveAt` selects an exact address without displacing any existing
  reservation; first-fit allocation remains available through `KrnMappingReserve`.
  Both use caller-owned unpublished metadata and publish under the mapping lock.
- `pager.c` fills a private, unpublished page in task context outside that lock.
  It supports zero backing and positioned reads on a retained private handle,
  detects short reads before the expected EOF, and zeroes the last page's tail.
  Cancellation does not reclaim callback context or interrupt a blocked read.
  The caller rechecks the request generation before installing returned backing.
- `domain.c` retains execution members, mappings and in-flight I/O separately.
  A creator's departure does not remove another worker's membership. Closing
  admission does not stop CPUs or establish that their memory accesses ended.
  Reclamation also requires withdrawing every external reference.

- `service.c` owns a bounded copied-byte request queue under the same caller-held
  domain lock. Owner-bound generation tokens reject stale request reuse; each
  live request retains a domain member. Cancellation of a running worker waits
  for its completion acknowledgement before releasing retention, and discards
  the reply. Queued/ready requests retire immediately on close. Queue drain
  does not imply domain reclamation or CPU quiescence. Buffers are trusted
  staging storage: domain-handle lookup, user-memory validation, scheduling,
  callback delivery and locking are integration responsibilities. See
  `service.h` for the complete lifetime and buffer contract.

- `user_buffer.c` selects kernel-owned domain identities through generation
  handles and retains anonymous resident pages through the existing mapping
  `in_flight` counts and domain membership. Every range is preflighted before
  copying, and every copy rechecks ledger ownership/permissions/generations plus
  a mandatory platform translation/access callback. Close invalidates copying
  immediately; backing cannot be released until leases drain. Shared/file-backed
  pages and faulting admission are deliberately refused.
- `user_service.c` connects those leases to the copied-byte service queue.
  Domain memory authority and thread request ownership are separate kernel-derived
  identities: sharing an address space does not grant cancellation/collection
  authority over another thread's request. Input
  is staged before submission; output remains pinned until cancellation is
  acknowledged or completion is collected. Ready responses are discarded if
  publication is no longer valid. Workers present captured tokens so record
  reuse cannot redirect stale completions, even when separate queues reuse the
  same queue token. Request-record generations survive retirement and never wrap. No public pointer/handle ABI or
  production exception/scheduler integration is supplied by these components.

The coordinator must register fault waiters before releasing its lock, suspend
and resume them through the scheduler, retain all backing and metadata through
completion, and provide actual filesystem coherence and persistence. Waiting on
an exception stack is not a supported integration. Executable mappings also need
an alias-wide permission policy; these components do not establish it.

Generic AArch64 leaf helpers live in `arch/aarch64-all/kernel/mapping.c` under
the synchronization contract in that directory's `protection.h`. Their callback
traces exercise descriptor mechanics, not real MMU/cache/TLB behavior.

The host regressions and their limits are described in
`developer/debug/test/kernel/user_buffer_host/README.md`,
`developer/debug/test/kernel/service_host/README.md`,
`developer/debug/test/kernel/mapping_host/README.md` and
`developer/debug/test/kernel/aarch64_mapping_host/README.md`.

The real-MMU buffer fixture and its platform-access oracle are described in
`developer/debug/test/kernel/user_buffer_qemu/README.md`.

Anonymous virtual reservations use `KrnMappingReserveVirtual`, distinct from
already committed demand-paged mappings. Their RESERVED pages fault without
queueing I/O, and cannot be populated or protected into commitment implicitly.
`KrnMappingCommitRange` atomically enables absent zero-backed pages; it does not
allocate or change existing committed pages. `KrnMappingDecommitPage` requires
pins drained and acknowledged translation removal before returning physical
backing, while preserving the virtual reservation. The coordinator supplies
zero backing through the pager, owns physical allocation/free and transactional
range handling, and must not treat component success as hardware qualification.
