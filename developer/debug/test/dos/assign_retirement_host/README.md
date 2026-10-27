<!-- Copyright (C) 2026 John KNIPPER. -->
<!-- SPDX-License-Identifier: APL-1.1 -->

# Assign retirement and path-copy bounds

Run from the core root with a new output directory:

```sh
python3 developer/debug/test/dos/assign_retirement_host/run.py --output /tmp/assign-retirement
```

The fixture executes the actual AssignLock, AssignPath and AssignLate bodies.
DOS namespace operations are modeled; releasing a filesystem lock asserts that
the global DOS write lock has already been released. Replacement must detach
and own the old entry before releasing its primary and multiassign locks.
Allocation failures and collisions with device entries exercise cleanup.
Exactly sized empty and nonempty source buffers expose path-copy overreads
under ASan/UBSan. Five mutations must fail with the corresponding assertion or
heap-buffer-overflow diagnostic, rather than merely returning an arbitrary error.

This is focused HOST evidence, not a concurrent-handler or guest test. It does
not implement assign snapshots, generation revalidation, deferred resolution
or a public context API. Executables are temporary; source hashes, extracted
bodies, compiler logs and runtime diagnostics remain in the output directory.
