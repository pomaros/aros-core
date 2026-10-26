# Early flattened-device-tree access

This allocator-free component reads an immutable, caller-owned FDT before Exec
or `openfirmware.resource` exists. The caller supplies a readable buffer length;
validation clears the context on failure. The format is bounded to 2 MiB, 4096
nodes and depth 32. It contains no Apple, Raspberry Pi, macOS or MMIO addresses.

`libfdt/` is the reference implementation from dtc **v1.7.2**, commit
`2d10aa2afe35527728db30b35ec491ecb6959e5c`, retrieved from
https://github.com/dgibson/dtc. These files are used under **BSD-2-Clause**;
the original notices and `LICENSE.BSD-2-Clause` are retained. The sole imported
source change is the `AROS_FDT_FREESTANDING` include branch in `libfdt_env.h`.
It selects the local byte operations without requiring a hosted C runtime.
The AROS adapter and byte operations are APL 1.1.

The adapter exposes iteration, raw properties, complete paths, compatible
matching, inherited enabled state, chosen console aliases, scalar `reg`
entries and recursive `ranges` translation. Missing `ranges` refuses translation;
empty `ranges` is identity. A whole register window must fit exactly one range.
One/two address cells and zero/one/two size cells cover CPUs and scalar MMIO.
PCI three-cell addresses require a PCI binding and are explicitly refused.

Interrupt lookup resolves `interrupts-extended` or inherited `interrupt-parent`
to a controller and its original cells. It does not assume GIC numbering.
Interrupt nexuses, nonzero PPI partitions and more than four interrupt cells
need their own binding. No callback reads a device register during discovery.

After startup the same retained blob is available through the platform's
existing boot handoff. This library does not replace the runtime
`openfirmware.resource` interface or construct a second mutable device tree.

The shared GIC bootstrap in `../../irq/` consumes these descriptors. The boot
CPU calls `AROSGICInit` once for shared distributor setup, with IRQs masked.
Secondary CPUs use `AROSGICInitCPU`, which preserves the distributor and prepares
only the executing CPU. Callers retain the described mappings and serialize
shared initialization. Component host tests and guest qualification are separate;
see the public `pomaros/pomaros-vm` product's qualification status for guest
results. A component probe is not physical-board acceptance.

Without an explicit inherited `interrupt-parent`, interrupt lookup uses the
first ancestor declaring `interrupt-controller`; interrupt nexuses remain
unsupported rather than being silently bypassed.
