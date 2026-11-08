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

## Utility identifiers

`utility.library` version51 provides [GetUniqueID64](rom/utility/getuniqueid64.c),
a boot-wide identifier allocator serialized across processors. Values begin
above the legacy32-bit range. Exhaustion returns zero permanently, without
wrapping or reusing identifiers; callers use the API from task context.

## Generic diagnostics

- [FAT transfer cache borrows](developer/debug/test/filesys/fat/cache_pins_host/README.md): actual transfer-body HOST checks and a Native DOS many-open-files regression; guest execution requires its own receipt.

- [Restricted AArch64 domain mechanism](developer/debug/test/kernel/restricted_domain_qemu/README.md): real-MMU QEMU checks with semantic negative controls; independent of product boot qualification.

- [Service coordinator ownership](developer/debug/test/kernel/service_host/README.md): sanitized HOST checks for copied requests, cancellation, generation reuse and domain retention.

- [AROS task service proof](developer/debug/test/kernel/service_vm/README.md): two clients, worker, native heartbeat, deterministic cancellation and joined teardown on a guest SDK.

- [Owned-buffer boundary](developer/debug/test/kernel/user_buffer_host/README.md) and [real-MMU service fixture](developer/debug/test/kernel/user_buffer_qemu/README.md): retained copying, permission/identity validation and cancellation without late writes.

- [Privileged-root snapshot](developer/debug/test/kernel/root_clone_host/README.md): bounded private table construction for restricted execution, with independent table ownership and permission checks.

- [Restricted space/channel ownership](developer/debug/test/kernel/restricted_space_host/README.md): actual adapter with synthetic PTE memory and semantic close/generation controls.

- [Synchronous exception delivery](developer/debug/test/kernel/restricted_exception_host/README.md): actual record writer, handler registration and continuation admission with a modeled fault stack; the restricted VM workload takes a real data abort and BRK through the handler on QEMU.

- [Native metadata allocation](developer/debug/test/kernel/restricted_allocation_host/README.md): extracted allocation wrappers with injected failures and ownership rollback.

- [Orphan cleanup admission](developer/debug/test/kernel/restricted_reap_hint_host/README.md): production retirement/pending predicates with retained leases and semantic negative controls; Native/SMP execution is separate.

- [FAT directory metadata](developer/debug/test/filesys/fat_examine_host/README.md): enumerate entries held open exclusively without granting or releasing their data locks; cursor, error and cleanup controls.

## Licences

AROS is a collection of components under different licences. Preserve each file's notices and consult [LEGAL](LEGAL), [LICENSE](LICENSE), [LICENSE.GPL](LICENSE.GPL), [LICENSE.LGPL](LICENSE.LGPL) and component-specific licence files. This fork does not relicense the upstream code.


## Managed-domain service work

Restricted API32 adds take_domain(domain,work) for Native controllers that own
one live managed domain. It leaves other domains' queued requests untouched and
retains worker-bound completion. Invalid, foreign, unmanaged and closing domains
are refused before service selection. Legacy take() keeps global-worker semantics.
The [protected admission HOST gate](developer/debug/test/kernel/restricted_domain_work_host/run.py)
extracts the production SVC entry filter and owner checks, with semantic negative
controls. The [space/channel gate](developer/debug/test/kernel/restricted_space_host/run.py)
checks selection between live spaces with identical virtual buffer addresses.
HOST evidence does not qualify a compiled kernel or Native execution.

Restricted API37 adds controller-owned privatization of a single-page read-only
shared view. It copies the backing into an independently owned writable page,
keeps the virtual reservation, and renews only pending write-fault tickets that
match the replaced identity. Shared peers retain the original object. The
space/channel and context-restore HOST diagnostics cover the backing transaction
and ticket selection; Native execution has a separate acceptance scope.
