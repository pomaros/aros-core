# AROS AArch64 VM platform

Copyright (C) 2026 John KNIPPER. Licensed under the AROS Public License 1.1.

This directory provides the UEFI-loaded, single-CPU VM platform shared by
PomAROS VM and QEMU. The historical `apple-aarch64` target and directory names
identify build paths; this public target contains no native Apple device drivers.
Generic AArch64 code lives in `arch/aarch64-all`.

The guest uses GICv2/GICv3 interrupts, the ARM architectural virtual timer,
firmware FDT discovery, PCI ECAM and coherent virtio DMA. Its x18 register is
ordinary task state. Hosted macOS builds reserve x18 separately.

`apple-boot-payloads` builds the kernel, Exec, the RAM-backed boot-image device
and the position-independent loader. `apple-wanderer-payloads` adds the AROS
desktop closure. The kernel and loader independently validate the exact ordered
resident set for the headless or desktop VM profile before startup.

Native Apple hardware implementations are maintained outside this repository.
SMP is not enabled by this VM target; other upstream architecture targets retain
their own SMP support. Product build, packaging and runtime qualification belong
to the product repositories.
