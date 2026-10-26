<!-- Copyright (C) 2026 John KNIPPER. -->
<!-- Licensed under the AROS Public License (APL), version 1.1. -->

# Native filesystem semantics probe

Build `native-probe.c` as `FilesProbe` with a matching AROS C SDK. Run
`FilesProbe volume:unused-directory nanoseconds` on a disposable volume that
provides nanosecond timestamps, or select `ticks` for 50 Hz modification times.
Both profiles require object identity across rename and hard links. They do not
accept pathname hashes as stable inode identities.

The probe refuses an existing root before touching its contents. It checks
distinct identities, precise modification times, both link forms, a 520-byte
dangling target, exact readlink truncation without a terminator, atomic rename
results, open-file metadata and invalid directory descriptors. It removes its
objects and treats failed cleanup as failure. The checks do not establish crash
durability, rename atomicity under concurrent observation or power loss.

Require exit zero and `[FILES] result=PASS` with zero failures. Record the
selected filesystem, timestamp profile, resident CRT and SDK/binary identities.
The probe deliberately fails missing features; compilation is not acceptance.
It uses public APIs and requires no provisional filesystem packet definitions.

A host build under ASan/UBSan checks the probe's own operations and cleanup
against the host filesystem. That result does not qualify AROS code. Existing
root refusal must leave a sentinel file intact. This probe complements the
[final-component probe](../posix-lstat/README.md); neither covers
multidirectory assign resolution or concurrent namespace changes.
