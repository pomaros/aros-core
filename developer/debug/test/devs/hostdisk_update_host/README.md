<!-- Copyright (C) 2026 John KNIPPER. -->
<!-- SPDX-License-Identifier: APL-1.1 -->

# Queued hostdisk update barrier

Run from the core checkout, choosing a new output directory for each run:

```sh
python3 developer/debug/test/devs/hostdisk_update_host/run.py --output /tmp/hostdisk-asan
python3 developer/debug/test/devs/hostdisk_update_host/run.py --tsan --output /tmp/hostdisk-tsan
```

The fixture extracts the actual BeginIO, unit dispatch, read/write loops and
Unix Host_Open/Close/Read/Write/Seek/Update bodies. Exec messages and hostlib
locking use pthread adapters; reads, writes and fsync use a real temporary file.
The Darwin and Linux geometry bodies use modeled ioctl responses. This is not
an AROS guest test, a Linux ioctl ABI test or a physical-media durability test.

The 34 cases cover ordered write/barrier/write/barrier completion, no premature
reply, command advertisement, absent backend/function, invalid descriptors,
read-only files, interrupted synchronization, partial and zero writes, failure
propagation, calling-thread errno, read-only open fallback for EROFS, EBUSY,
EACCES and EPERM, plus geometry error propagation. Ten mutations must compile
and fail at runtime; the missing dispatch and zero-progress write controls are
bounded by the process timeout. Inspect each failure log: an unrelated host
failure does not qualify a mutation. The result records source hashes and each
compile/runtime outcome. Compiler and runtime calls have finite deadlines.

The device completes CMD_UPDATE on its existing unit queue after host fsync.
Backends without a synchronization callback omit it from NSCMD_DEVICEQUERY and
return IOERR_NOCMD. Host fsync is the stated contract; host drive-cache policies
and power-loss guarantees are outside this fixture. Unit startup, abort/close,
forced-stop behavior and other backend implementations require separate tests.
