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
- `pager.c` fills a private, unpublished page in task context outside that lock.
  It supports zero backing and positioned reads on a retained private handle,
  detects short reads before the expected EOF, and zeroes the last page's tail.
  Cancellation does not reclaim callback context or interrupt a blocked read.
  The caller rechecks the request generation before installing returned backing.
- `domain.c` retains execution members, mappings and in-flight I/O separately.
  A creator's departure does not remove another worker's membership. Closing
  admission does not stop CPUs or establish that their memory accesses ended.
  Reclamation also requires withdrawing every external reference.

The coordinator must register fault waiters before releasing its lock, suspend
and resume them through the scheduler, retain all backing and metadata through
completion, and provide actual filesystem coherence and persistence. Waiting on
an exception stack is not a supported integration. Executable mappings also need
an alias-wide permission policy; these components do not establish it.

Generic AArch64 leaf helpers live in `arch/aarch64-all/kernel/mapping.c` under
the synchronization contract in that directory's `protection.h`. Their callback
traces exercise descriptor mechanics, not real MMU/cache/TLB behavior.

The host regressions and their limits are described in
`developer/debug/test/kernel/mapping_host/README.md` and
`developer/debug/test/kernel/aarch64_mapping_host/README.md`.
