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

## Actual GNU Make timestamp comparison

Build `make-rebuild-probe.c` as `MakeRebuildProbe` with a matching AROS SDK.
Run `MakeRebuildProbe volume:unused-directory path:to/make`. The executable
must be GNU Make with its `file` function. The probe refuses an existing root,
sets source/output timestamps to the same second but 100 ms apart, checks
stat/lstat/fstat/fstatat, then executes the actual make program through DOS
RunCommand. GNU Make's own file function writes the target, so no recipe shell
is needed. Require exit zero and `[FILES] PASS actual-make-same-second`.

The 100/200 ms timestamps are representable by 50 Hz filesystems; this tests
subsecond dependency decisions without claiming nanosecond filesystem precision.
The probe restores CurrentDir, closes its handles, removes its three files and
root, and treats cleanup failure as failure. Record the make executable's build
configuration and identity, selected filesystem and coherent CRT/SDK identities.
Compilation alone is not a guest result. Real write-clock quality needs its own
clock-resolution contract: nanosecond fields do not guarantee unique timestamps
for successive writes.

## Native hard-link admission and rollback

Run `python3 link-host.py --output /absolute/path/outside/source` for the actual
production link body with modeled DOS calls and path scope. ASan/UBSan cases
cover conversion, source lookup/examine, directory refusal, destination lookup,
cross-volume refusal, native creation failure and error preservation when cleanup
changes errno/IoErr. Two compiling controls remove cross-volume admission or
error restoration and must fail. No provisional filesystem wire is used.

This checks the wrapper, not real mounted-handler hard links, assign resolution,
concurrent namespace changes or the path-scope implementation. FilesProbe's
native identity/link checks remain necessary for product acceptance. The soft-
link target encoding/resolution contract is separate from hard-link creation.
