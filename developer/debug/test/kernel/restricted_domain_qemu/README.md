<!-- Copyright (C) 2026 John KNIPPER. AROS Public License (APL) Version 1.1. -->

# Restricted AArch64 domain mechanism diagnostic

This freestanding ELF exercises the shared AArch64 mapping/protection helpers
on QEMU's real guest MMU. It is not an AROS boot, scheduler implementation or
qualification of a product image. It introduces no kernel ABI.

Run from the common core checkout with Clang/LLD and QEMU installed:

```sh
python3 developer/debug/test/kernel/restricted_domain_qemu/run.py \
  --cc /path/to/clang --out /absolute/fresh/output/directory
```

The output directory must be outside the checkout. Dirty diagnostic inputs
require `--allow-dirty-diagnostic`; success then reports `DIAGNOSTIC_PASS`.
A committed-input run reports `PASS`. `result.json` retains input and ELF
SHA-256 identities, source revision, tool versions, build/run commands,
per-variant exit/output and failure classification. Each variant has build,
serial and QMP preflight logs. A timeout fails and captures registers through
QMP before terminating the guest. A compile failure or unrelated crash never
counts as detecting a mutation.

## Mechanism and checks

The fixture enters EL1 at 0x40080000 on `virt`, Cortex-A72, TCG, one CPU and
128 MiB RAM. A stopped-register QMP preflight verifies that entry PC before
execution. It uses 4 KiB pages, a 39-bit TTBR0 address range, no ASIDs, and
full TLB invalidation with barriers on each root switch. Caches are disabled.

Each of two roots maps RX user code at 0x10000000, distinct RW user backing
at 0x10001000 and a distinct RW stack at 0x10003000. The intervening page is
unmapped. These addresses are disjoint from the fixture's privileged UART
page at 0x09000000 and identity RAM block at 0x40000000. The privileged block
contains vectors, exception stack, tables and fixture data; it is EL0-inaccessible
but EL1 RWX. No production alias-wide W^X property is asserted.

Two EL0 contexts increment independent patterns over 2,000 alternating SVC
switches. The guest checks x18 and v8 sentinels; the handler saves/restores all
general and SIMD registers plus FPCR/FPSR. This does not test native TLS.
The fixture checks:

- Three denied raw SVCs per context, including the Supervisor number.
- A synchronous four-byte transform through a bounded, fixed owned-page
  buffer; rejection of a kernel pointer and a crossing-page buffer.
- Kernel-data read and write permission faults, an unmapped guard read and
  an instruction permission fault on attempted execution of the RW data page.
- A write fault after RO protection; exact fault PC/address/access is checked,
  the handler restores RW permission and retries that instruction.
- Final patterns and exact fault, service and denial counts at EL1.

The dispatcher is fixture-only. It does not invoke production AROS services or
prove that production Supervisor entry is restricted. The transform runs on
the exception stack, not a native worker. Known test faults are repaired or
skipped by the harness; there is no user fault-upcall/resume-token API.

## Deliberately broken variants

| Variant | Mutation | Required failure |
| --- | --- | --- |
| shared-backing | Both domains use domain A's data page | `FAIL guest-data` |
| admit-supervisor | Supervisor SVC reports success | `FAIL guest-svc` |
| skip-protect | Omit RO transition | `FAIL completion-counts` |
| expose-kernel | Make only the aligned secret-data page user-accessible | `FAIL guest-kernel-access` |

The final mutation must not expose the entire kernel block: making executable
kernel pages user-writable can trap EL1 instruction fetch before the test
starts, which would be an unrelated failure. The secret is page-aligned so the
mutation changes data permissions without exposing the exception code.

## Integration boundary

This is a mechanism regression for the existing generic helpers. Production
integration additionally needs kernel-owned task classification, audited
exception/scheduler mappings and root/TLS switching, restricted service
admission, owned request copies, an asynchronous native worker, callback
reentry, validated fault return, cancellation, drain and reclamation. SMP,
cache coherency, stale translations after reuse, native-task coexistence and
resource cleanup are outside this fixture. Static allocations live until QEMU
exits; a passing run does not establish leak-free domain teardown.


## Optional owned-buffer fixture

`--buffer-proof` links the actual generic buffer/service boundary via the shared
harness hooks. See [its contract and controls](../user_buffer_qemu/README.md).
Default invocation retains the original four controls and synchronous bounded
page transform; its result does not imply the additional buffer proof ran.


## Trusted-template root clone proof

`--root-clone-proof` constructs a trusted template with user-accessible normal
RAM and read-only shared kernel text, then derives two private roots using the
actual `AArch64ClonePrivilegedRoot` helper. Kernel text is EL1 RX; kernel data
is EL1 RW/NX. A fingerprint checks that root creation and private user mappings
did not change the source tables. The ordinary EL0 switch/fault/kernel-access
suite runs through these roots. A deliberately broken clone retaining user
access must fail `guest-kernel-access`.

This option can be combined with `--buffer-proof` to run the owned-buffer
baseline on cloned roots. In this mode the runner selects the root-clone
negative control; run `--buffer-proof` separately for all buffer controls.
The snapshot's kernel mappings are static in this fixture. Production module
mapping changes, exception-entry root/TLS transition and scheduler integration
are not qualified. The host root-clone suite also covers 16 KiB tables;
this runtime remains a 4 KiB, single-CPU, caches-disabled fixture.
