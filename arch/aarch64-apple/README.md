# Shared AArch64 guest and kernel foundation

Copyright (C) 2026 John Knipper. Licensed under the AROS Public License 1.1.

This directory supplies the shared AArch64 boot, kernel and virtual-machine
foundation. The historical `apple-aarch64` configure name is used by the public
VM and QEMU products; it does not mean this tree contains the native Apple
peripheral drivers. See the [core build targets](../../README.md#build-targets).

The shared implementation builds on `arch/aarch64-all/`. It owns the bounded
BootInfo handoff, module relocation, memory ownership and reclamation, retained
payload identity, architectural CPU context, scheduler integration and virtual
platform support. Shared DOS, Exec and C-runtime code remains in its ordinary
component directories.

Platform-specific implementations enter through explicit boundaries:

- [stage2_platform.h](bootstrap/stage2_platform.h) selects the second-stage
  provider. A native build requires a provider; absent observations cannot
  count as successful hardware evidence.
- [coldstart_platform.h](kernel/coldstart_platform.h) defines the startup and
  desktop service boundary.
- [platform_debug.h](kernel/platform_debug.h) separates shared diagnostics and
  VM console output from native debug transports.

Native Apple drivers, hardware providers, firmware and product integration are
maintained separately. Do not add a second generic implementation to that
platform layer or copy its private sources into this public repository.
Existing `AROSApple` identifiers describe shared handoff/API names; they are not
proof of Apple hardware support.

Build through the relevant product repository, using its pinned core revision,
SDK and test procedure. Hosted reserves x18 for macOS; VM and native AROS permit
x18 task state. Keep their ABIs and build directories separate. A generic QEMU
result qualifies the code it exercises, not Apple devices, native startup or
physical storage behavior.
