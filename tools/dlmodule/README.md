<!-- Copyright (C) 2026 John KNIPPER <code@jkn.me>. -->
<!-- Licensed under the AROS Public License (APL), Version 1.1. -->

# dlopen modules

A module that `dlopen()` loads is an AROS ELF object linked like a program,
so that `LoadSeg()` loads and relocates it, with `dlmodule-startup.o` in place
of the program startup and a generated table of the symbols it exports.
`compiler/include/aros/dlmodule.h` describes the format: the entry at the
start of the code is marked (`AROS_DLMODULE_MAGIC` and the version, behind a
branch), and returns a descriptor whose `Init` opens the module's automatic
libraries and runs its constructors, as genmodule's InitLib does for a
library, and whose `Expunge` undoes them.

```sh
dlmodule-link --cc CLANG --cc-arg=--target=aarch64-unknown-aros \
    --cc-arg=--sysroot=SDK --cc-arg=-BTOOLS \
    --startup SDK/lib/dlmodule-startup.o --nm llvm-nm \
    -o libexample.so example.o [--export NAME | --export-prefix PREFIX]...
```

Without `--export` or `--export-prefix`, the module exports every global
symbol its own objects define, and none from archives; names that start with
`__` are never exported unless named. The tool links twice: once to list what
the module defines, then with the table. It refuses a module with an
undefined symbol.

`dlopen()` (posixc.library) looks for a bare name in `PROGDIR:` and then
`LIBS:dl/`, as given and without the version numbers after `.so`
(`libvulkan.so.1` finds `libvulkan.so`). A module runs its initialisation in
the task that loads it first and uses that task's C library bases, so a
program loads modules from its main task. Modules do not bind to each other's
symbols, and `RTLD_GLOBAL` is refused. Modules still loaded when the program
ends are unloaded after its exit functions.

The tests are in `developer/debug/test/dlopen/`.
