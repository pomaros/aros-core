<!-- Copyright (C) 2026 John KNIPPER <code@jkn.me>. -->
<!-- Licensed under the AROS Public License (APL), Version 1.1. -->

# Apple code window on the host

`run.py --out DIR` compiles `arch/aarch64-apple/kernel/code_window.c`
unchanged and runs `test.c` under ASan and UBSan, over a simulated Exec that
gives 2 MiB blocks and a simulated mapping (no page tables):

- every page of a request is backed by its own physical page, from a block
  Exec gave, and cleared before use;
- frees of a whole run, its head or its tail give each page back once, and
  a second free gives nothing;
- a failure while backing (Exec has no block, a table cannot be allocated)
  gives back what was taken and the reservation;
- a fixed address gets exactly its run; requests made one after the other
  are adjacent, as a segment's hunks must be;
- a block goes back to Exec only when none of its pages is mapped, and once
  everything is freed only a reserve of four empty blocks is kept, with
  their records;
- a reservation (MEMF_RESERVE) maps nothing until a range of it is committed,
  a commit backs only what is not backed yet and clears it, a decommit gives
  the pages back, neither applies outside a reservation or partly outside
  one, a commit that runs out of memory is refused, and freeing the
  reservation gives every committed page back;
- when Exec runs out, requests fail cleanly and everything comes back;
- the statistics count pages in use, allocations, frees, and executable
  pages unmapped by a free;
- 100,000 random allocations and frees end with every page returned.
