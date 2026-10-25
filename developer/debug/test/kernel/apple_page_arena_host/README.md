<!-- Copyright (C) 2026 John KNIPPER <code@jkn.me>. -->
<!-- Licensed under the AROS Public License (APL), Version 1.1. -->

# Apple code arena allocator on the host

`run.py --out DIR [--compare REV]` compiles `arch/aarch64-apple/kernel/page_allocator.c`
unchanged against stub exec headers and runs `test.c` under ASan and UBSan:

- 200,000 random allocations and frees, page tables among them: every block
  handed out is free, and a request fails only when no free run is long
  enough;
- placement: a page table takes the arena's last page, other requests
  are first fit from the bottom, so that the hunks of one segment stay
  adjacent;
- a fixed-address request gets that exact run or nothing;
- a session that fills the arena with a large program's hunks, page tables
  and small long-lived blocks, then frees most hunks, and asks for forty
  2 MiB blocks (V8's JIT regions).

With `--compare REV` the session also runs against the allocator at that git
revision; the current one must satisfy more of the 2 MiB requests.
