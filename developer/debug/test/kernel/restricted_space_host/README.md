<!-- Copyright (C) 2026 John KNIPPER. AROS Public License (APL) Version 1.1. -->

# Restricted space/channel ownership checks

Compile the actual production restricted_service.c and generic AArch64 mapping
helpers with ASan/UBSan. The fixture supplies aligned host memory for page tables
and callbacks for cache/TLB operations; it does not execute privileged CPU
instructions or qualify concurrent AROS execution.

```sh
python3 developer/debug/test/kernel/restricted_space_host/run.py --out /ABS/FRESH
```

Two channels share one space. Closing a channel with a worker lease must retain
its resources, avoid late writes, and leave the other channel operational.
Reusing channel storage must advance the independent channel generation even
when the request token repeats. Space close refuses mapping retirement until
all channels detach, then unmaps and drains its domain membership.

Three channel semantic mutations respectively stop the whole domain on channel close,
ignore channel generation, or remove live mappings before channels drain. Each
must reach its named assertion; timeout, compiler failure and sanitizer failures
are not accepted as semantic detection. Receipts bind all input sources and
compiled binaries; clean source reports PASS, dirty diagnostic input is explicit.

Private regions reserve exact addresses in the dynamic arena, outside the
per-thread stack/TLS range. The caller retains zeroed metadata until successful
detach and supplies fresh zero-filled backing; commit failure retains caller
ownership. Decommit returns backing only after acknowledged unmap. A failed
detach can have removed some PTEs, but transfers no ownership: retry while
retaining every page and record. Space close refuses attached regions.

The fixture exercises independent roots at the same VA, duplicate physical-page
refusal, eager writable commit, committed no-access guards, generation exhaustion,
PTE conflicts, cross-page buffer copying, pinned decommit refusal and partial
unmap/retry. Recommit uses the actual anonymous zero pager on reused backing.
Eight additional semantic controls cover duplicate ownership, private aliases,
unowned installation, pinned backing, attached-region destruction, eager write
permissions, guard-to-write permission and skipped zeroing (eleven total).

This adapter accepts private read/write/no-access pages only. Executable pages
and shared aliases require a separate backing/publication contract. No guest
allocation syscall is supplied by this component test.
