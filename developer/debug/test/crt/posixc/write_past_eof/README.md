<!-- Copyright (C) 2026 John KNIPPER <code@jkn.me>. -->
<!-- Licensed under the AROS Public License (APL), Version 1.1. -->

# Writes beyond the end of a file

`WritePastEOF DIR` writes beyond the end of a file in `DIR` with `pwrite`
and with `lseek` then `write`, as Chromium's disk cache does. The gap must read
as zeros, the file must grow to the written end, and a failure must set a
POSIX `errno`. posixc no longer extends files while seeking (core
`f83bef2514`), so each handler provides the beyond-EOF position itself. RAM:,
FAT12/16 and FAT32 volumes pass; it prints `WPE result=PASS`.
