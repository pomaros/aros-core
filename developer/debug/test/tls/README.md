<!-- Copyright (C) 2026 John KNIPPER <code@jkn.me>. -->
<!-- Licensed under the AROS Public License (APL), Version 1.1. -->

# Thread-local storage tests

Native TLS on AArch64 (integration repository D-137): every TLS variable is
reached through a TLS descriptor, which `LoadSeg()` builds when it loads the
segment (`rom/dos/internalloadseg_elf.c`); `rom/dos/elf_tls.c` holds the
resolver and the segments' slots. `TPIDR_EL0` points to the running task's
control block, kept per task by exec.

`TLSDescTest` is built from `TLSDescTest.c` and `tlsdesc-access.S`:

    clang --target=aarch64-unknown-aros --sysroot=<SDK>/Developer -B<tools> \
        -mcmodel=large -fno-pic -O2 TLSDescTest.c tlsdesc-access.S -lpthread \
        -o TLSDescTest

The access sequences are written in assembly, in the AAELF64 form
(`adrp`/`ldr`/`add` with `:tlsdesc:` and a `.tlsdesccall` before the
`blr`), so that the loader and the runtime can be qualified without a
compiler that emits them. The test checks that the first access installs a
control block, that the resolver's slow and fast paths preserve x2-x18 and
v0-v31, the `.tdata` initial values including a pointer the loader relocated
in the image, a zeroed and 64-byte aligned `.tbss`, stable addresses across
accesses, that the thread pointer does not change on the first access
(TPIDR_EL0 is the address of the task's TLS anchor, set by exec for the
task's whole life), and that a second task gets a block of its own.
`LEAKCHECK` also runs 50 short-lived tasks with TLS and requires that they
cost no more memory than 50 tasks without: a task's TLS memory is on its
`tc_MemEntry` list, so exec frees it with the task.

`TLSNative` (`TLSNative.c`, `tlsnative-other.c`) is the same contract written
in C with `_Thread_local`, for a compiler that emits TLS descriptors for AROS
instead of emulating TLS: an initialised and a zeroed variable, a pointer
initialiser the loader relocates, and an extern variable defined in another
translation unit, and a second task with its own copy.

`TLSGrow DIR` (`TLSGrow.c`, module `tlsgrow-module.c` linked with
`tools/dlmodule/dlmodule-link` as `DIR/tlsgrow.so`) reads two TLS variables
across the first accesses of nine dlopen()ed TLS modules, which grow the
task's slot table past eight slots; the compiler keeps the thread pointer it
read before the call (one MRS), so the read after it is right only if the
thread pointer never changes.

`TLSBench` measures one thread-local access, emulated (`pthread_getspecific`,
with 2 and 40 threads, from the main thread and from the last thread created)
against the native sequence. It needs nothing but a running system.
