# Executable segment ownership

`CreateSegList` must request executable pages on W^X platforms, preserve the
bootstrap pool path before kernel.resource exists, and fail closed when a
page allocation fails. `UnLoadSeg` distinguishes page mappings from ordinary
pool segments with `TypeOfMem`; a physical page borrowed from an Exec pool
cannot satisfy that distinction. The executable allocation selects a separate
code mapping.

Run `python3 run.py --output /absolute/new/directory` from this directory.
The HOST gate compiles the production creation function and DOS release
callback, using the production executable-memory flag. Its allocator model
covers initial pages, borrowed pool addresses, absent kernel.resource and
allocation failure, with W^X enabled and disabled. Actual host `mprotect`
prevents writes to populated code pages; this does not execute their machine
code or qualify a platform allocator.

A compiling negative control restores the untyped page request. It passes the
initial-arena case, then faults when the pool release callback writes to the
protected borrowed page. Both its wrong-owner witness and the actual signal
are required. This catches an interaction that allocation/free tests of either
allocator alone do not cover. Guest qualification must separately force arena
growth, execute and unload a generated segment, and retain the kernel trace.
