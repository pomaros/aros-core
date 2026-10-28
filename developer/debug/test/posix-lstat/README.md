<!-- Copyright (C) 2026 John KNIPPER. -->
<!-- Licensed under the AROS Public License (APL), Version 1.1. -->

# Native lstat metadata regression probe

Build `native-probe.c` with a matching AArch64 AROS C SDK. Run `LstatProbe
RAM:unique-new-root` with an unused directory name; the probe refuses an existing
root. It creates its own files, valid and dangling soft links, a raw Latin-1
link name, a directory and a multidirectory assign. It checks both lstat widths, contrasts stat behavior,
verifies referent contents and removes its test objects. Parent-link support is
reported separately. A failed cleanup is a test failure.

Require command return zero and the final `[LSTAT] result=PASS` with zero
failures. Require `parent-link result=PASS` for parent-link coverage; an
UNSUPPORTED line does not qualify that case. Record the SDK, resident POSIX
module and binary identities alongside output. Compilation alone does not
qualify filesystem behavior.

Require `multiassign result=PASS` for later-entry lookup, final-link metadata,
missing entries and exclusively locked files in both stat widths. The assign
name is reserved only for the duration of the probe; an existing assignment is
refused. This probe does not exercise concurrent namespace changes. A final-component lookup implementation must
also preserve DOS resolution through intermediate links and later assign
entries; locking only the first resolved parent is insufficient. No preceding
Git revision or unintegrated helper is required to build this probe.
