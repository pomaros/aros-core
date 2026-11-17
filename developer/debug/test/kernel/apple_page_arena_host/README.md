<!-- Copyright (C) 2026 John KNIPPER <code@jkn.me>. -->
<!-- Licensed under the AROS Public License (APL), Version 1.1. -->

# Apple page arena allocator on the host

`run.py --out DIR [--compare REV]` compiles `arch/aarch64-apple/kernel/page_allocator.c`
unchanged against stub exec headers and runs `test.c` under ASan and UBSan:

- metadata-only allocations above 4 GiB and at the capacity boundary of
  stage two's part (64 MiB), with over-boundary allocation and
  initialization refusals;
- 200,000 random allocations and frees, page tables among them: every block
  handed out is free, ordinary allocations match an independent first-fit
  model, and a request fails only when no free run is long enough;
- placement: a page table takes the arena's last page, other requests
  are first fit from the bottom, so that the hunks of one segment stay
  adjacent;
- a fixed-address request gets that exact run or nothing;
- a full arena can reuse holes below the occupied prefix, including after a
  larger request fails and after a fixed-address allocation fills the last hole;
- a session that fills the arena with a large program's hunks, page tables
  and small long-lived blocks, then frees most hunks, and asks for forty
  2 MiB blocks (V8's JIT regions);
- growth through fake operations: a full arena grows ahead of need after a
  retry the attach asked for, a chunk is arena only once attach publishes it,
  its own records are never handed out, a page table goes to a chunk's top,
  a large request takes a larger chunk with the records attach carved for
  restricted domains counted as allocations and freeable, a failed attach
  gives its chunk back, and a chunk goes back to Exec only when nothing in it
  is used and enough stays free without it, or stays arena when its blocks
  cannot be restored.

With `--compare REV` the session also runs against the allocator at that git
revision; the current one must satisfy more of the 2 MiB requests.

`mapping.py --out DIR --reject REV` separately compiles the actual native
attach, restore and detach bodies from `page_arena_kernel.c`. Architecture
operations are substituted: invalidating a block applies host `mprotect` with
no access, and publishing its replacement restores access. Metadata resides
inside the first or last block; another block already has page leaves. Attach,
retry, failed attachment rollback and detach must preserve metadata and restore
the original descriptors. The old revision must fault on inaccessible metadata.
This is HOST mapping-lifetime evidence, not hardware cache/TLB qualification.

`native_probe.c` is an AROS command for the compiled kernel. Each of three
cycles allocates 128 MiB from the physical page arena (larger than its initial
64 MiB maximum), checks cleared bytes, writes and verifies both ends of every
page, then frees the allocation. Require its final PASS and kernel growth/trim
records to distinguish successful page access from actual chunk restitution.
Compile against the selected product SDK; retain binary, kernel and source
identities with the run. A HOST pass does not substitute for this execution.
