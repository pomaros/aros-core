# v8.library provenance

The library glue (`v8.conf`, the `v8*.c` functions, `v8_init.c`,
`v8_platform.c`, `v8_simple_eval.c`, `v8_bridge.cc`, `std_fix.cpp`,
`v8_allocator.*`, `include/libraries/v8.h`) and the engine embedding in
`engine/` come from Terminills' aros-web, `libs/v8`, commit `0631e9e`,
headed "Copyright (C) 2025, The AROS Development Team". Its author licenses it
like the AROS tree (APL 1.1) or compatibly (John Knipper, 2026-10-24).
The x86_64 hosted shims of that directory (`emutls-aros.c`,
`glibc-stubs-aros.c`, `pthread-abi-aros.c`) are not imported: this V8 is built
against the AROS SDK, not a glibc ABI.

`v8_aarch64.c` (the AArch64 JIT and timer hooks) and `mmakefile.src` are
original, APL 1.1. `aros-relocatable.lds` is the section script of our
Chromium toolchain patch queue.

The V8 monolith linked in is V8 (BSD-3-Clause) with LLVM's libc++
(Apache-2.0 WITH LLVM-exception); a distribution of v8.library carries both
license texts.
