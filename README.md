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

Keep the official AROS remote fetch-only and review generic changes in this fork before proposing upstream contributions. A source pin identifies source inputs; build manifests and product runtime checks identify and qualify binaries separately. Changing a pin does not qualify an existing image.

## Generic diagnostics

- [Restricted AArch64 domain mechanism](developer/debug/test/kernel/restricted_domain_qemu/README.md): real-MMU QEMU checks with semantic negative controls; independent of product boot qualification.

- [Service coordinator ownership](developer/debug/test/kernel/service_host/README.md): sanitized HOST checks for copied requests, cancellation, generation reuse and domain retention.

- [AROS task service proof](developer/debug/test/kernel/service_vm/README.md): two clients, worker, native heartbeat, deterministic cancellation and joined teardown on a guest SDK.

- [Owned-buffer boundary](developer/debug/test/kernel/user_buffer_host/README.md) and [real-MMU service fixture](developer/debug/test/kernel/user_buffer_qemu/README.md): retained copying, permission/identity validation and cancellation without late writes.

- [Privileged-root snapshot](developer/debug/test/kernel/root_clone_host/README.md): bounded private table construction for restricted execution, with independent table ownership and permission checks.

## Licences

AROS is a collection of components under different licences. Preserve each file's notices and consult [LEGAL](LEGAL), [LICENSE](LICENSE), [LICENSE.GPL](LICENSE.GPL), [LICENSE.LGPL](LICENSE.LGPL) and component-specific licence files. This fork does not relicense the upstream code.
