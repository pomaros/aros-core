<!-- Copyright (C) 2026 John KNIPPER <code@jkn.me>. -->
<!-- Licensed under the AROS Public License (APL), Version 1.1. -->

# A thread's crash report names its program

`thread_crash.c` builds `ThreadCrashProbe`: a pthread writes through NULL.
A thread has no CLI and no segment list of its own, so the kernel's crash
report walks the creator chain (`et_Parent`, at most eight levels) and places
the faulting address in the first creator's segments. The report must read
`crash elr=... ThreadCrashProbe seg0+0x...` and not `?`.

Offsets are segment-relative: `seg0+N` is the ELF `.text` offset `N` minus
the loader's segment header (0x14 for this probe).
