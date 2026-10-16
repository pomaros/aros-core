# PomAROS shared AROS core

Common operating-system sources for [PomAROS Hosted](https://github.com/pomaros/pomaros-hosted), [PomAROS VM](https://github.com/pomaros/pomaros-vm) and [PomAROS QEMU](https://github.com/pomaros/pomaros-qemu). This repository is a maintained fork of [official AROS](https://github.com/aros-development-team/AROS), retaining its Git ancestry and existing component licences.

## Build targets

| Product | Core configuration | AArch64 x18 | Host integration |
| --- | --- | --- | --- |
| Hosted | `darwin-aarch64` | Reserved for macOS | macOS process, native bridges and host drivers |
| VM | `apple-aarch64 --enable-vm-guest` | Available to AROS | Independent guest under Virtualization.framework |
| QEMU | `apple-aarch64 --enable-vm-guest` | Available to AROS | Same guest sources under QEMU |

The guest target retains its existing `apple-aarch64` build name. It supplies the single-CPU guest configuration used by both public VM products. It contains virtual-machine support and shared AArch64 contracts; it does not supply native Apple peripheral drivers. Native Apple hardware development is maintained separately. Generic CPU and kernel work is intended for reuse by other AArch64 platforms, including Raspberry Pi; that does not imply those platforms are qualified by Mac VM tests.

The three product repositories own applications, launchers, disk images, host bridges and packaging. Each `CORE.lock` pins the same reviewed source revision. Hosted and guest kernels are separate target builds; VM and QEMU use the same guest kernel build.

## Building

Start with the relevant product's instructions:

- [Hosted setup and toolchain](https://github.com/pomaros/pomaros-hosted/blob/main/GETTING-STARTED.md)
- [VM build instructions](https://github.com/pomaros/pomaros-vm#readme)
- [QEMU build instructions](https://github.com/pomaros/pomaros-qemu#readme)

Use separate source and build directories from private native development. Build toolchain sources and patches live under `tools/crosstools/`; external prerequisites are downloaded from their original public sources. Apple signing credentials are not build prerequisites.

[Repository ownership and upstream maintenance](docs/pomaros/UPSTREAM.md) explains synchronization and contribution flow. [Source and binary provenance](docs/pomaros/SOURCES.md) explains what a source revision identifies.

## Licences

AROS is a collection of components under different licences. Preserve each file's notices and consult [LEGAL](LEGAL), [LICENSE](LICENSE), [LICENSE.GPL](LICENSE.GPL), [LICENSE.LGPL](LICENSE.LGPL) and component-specific licence files. This fork does not relicense the upstream code.
