# Rust target for native AArch64 AROS

`aarch64-unknown-aros.json` is the target of every native AArch64 AROS build:
Apple Silicon, the PomAROS VM and QEMU guests, and later Raspberry Pi. It is
the PomAROS Hosted target (`pomaros-hosted/hosted/rust/`) without
`+reserve-x18`: native AROS treats x18 as an ordinary register, and only
hosted AROS, which calls macOS directly, reserves it.

A stock nightly builds for it with `-Zbuild-std=core,alloc` (kernel-side
code) or with the AROS `std` of `jonx/rust-aros`. Code is static, uses the
large code model and no thread-local storage, aborts on panic, and produces
GOT-free relocations that `LoadSeg` and the module loader accept.

`rusttest/` is the reference kernel-side module: a genmodule C shell whose
LVOs call a `no_std` staticlib built by its mmakefile (`make
kernel-rusttest`), with `test/RustTest` checking every LVO. A Rust module
copies its layout.
