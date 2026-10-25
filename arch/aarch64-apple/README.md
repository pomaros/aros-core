# AROS Apple Silicon target

Copyright (C) 2026 John Knipper. Licensed under the AROS Public License 1.1.

This directory is the self-contained platform layer for native AROS on Apple
Silicon. It builds on `arch/aarch64-all/`; platform-independent AArch64 work
belongs there, while Apple boot, FDT, interrupt, power and device code belongs
here.

The pre-hardware target is a real load boundary rather than a bootable kernel.
It verifies the `apple-aarch64` configure identity and freestanding AROS
AArch64 compiler, builds `kernel.resource`, `exec.library`, `utility.library`
and `task.resource` as AROS ELF64 AArch64 relocatable modules, and builds an
APL-owned position-independent second stage which relocates those modules after
the producer-neutral BootInfo v4 loader handoff.
The generic regression constructs the first memory header, scans residents,
prepares ExecBase and completes the single-task `kernel.resource` resident. It
then imports every eligible conventional range and reclaims every loader extent
whose BootInfo v4 lifetime ends after stage two. Before that reclamation, the
exact verified bundle size/SHA-256 and slot/direct selection state enter a
queryable kernel-owned retained copy. It enters EL0, rotates the boot
task plus two allocated tasks through the real Exec scheduler with owned integer
and FP/SIMD contexts, then runs the kernel/Exec/utility/task core cold-start
layer and the complete AROS/iffparse/locale/stdc/stdcio/posixc closure after DOS
takeover. The retained read-only FAT image supplies `SYS:`, its standard assigns
and portable CLI/TLS probes. It does not yet boot a normal desktop kickstart or
claim that Apple hardware is enabled.

The pre-hardware A6 foundation now lives inside `kernel.resource`. A bounded
final-FDT parser accepts current AIC1/AIC2 bindings, resolves the controller's
PMGR power-domain phandle and forbids MMIO unless that domain is always-on or
explicitly acquired. Stable source IDs use twelve IRQ bits plus two die bits;
the Apple private `kernel_arch.h` expands the ordinary AROS IRQ table to 16,384
entries and routes `KrnAddIRQHandler()`/`KrnRemIRQHandler()` through the AIC
adapter. Initialization masks all implemented sources, runtime external-IRQ
vectors dispatch through `krnRunIRQHandlers()`, and event-read auto-ack is
followed by software-latch clear and conditional mask-clear EOI. Scheduler exit
is considered only for an IRQ returning to AArch64 EL0, never for a nested EL1
IRQ. The adapter is inactive until an explicit final-FDT activation call, so
the existing pre-AIC quarantine remains the production behavior. FIQ timer
delivery, IPIs, AIC3, affinity and SMP are not claimed.

The pre-hardware A6b layer is a separate `pmgr.resource`. It consumes the same
bounded final FDT plus its explicit handoff size, inventories direct Apple PMGR
power-state children and validates every dependency before exposing atomic
multi-root acquire/release calls. Parents acquire before children; release is
child-first to the FDT minimum state; always-on domains are pinned, auto-managed
registers are not overridden, polls are finite and failed transactions roll
back or fault closed. Discovery and initialization never write MMIO. The
resident is deliberately priority 104 with `RTF_COLDSTART`: AROS `genmodule`
would derive `RTF_SINGLETASK` at 105, before the complete memory map is imported.
The separate diagnostic links a transport-neutral evidence encoder and an
original bounded DockChannel candidate writer. The encoder emits the same
transaction grammar used for UART-owned phases
and can encode the silent generation-4 release only after validating the
runtime channel ID, bundle/FDT digests, zero initial references, child-first
transition order and final register/counter report. The identity is no longer a
free-form caller input: a bounded parser requires exactly one final-FDT node
carrying both `aros,pmgr-evidence-channel` and a canonical
`aros,evidence-channel-id`, validates its Apple compatibility, one exact MMIO
extent and complete always-on PMGR ancestry, and rejects malformed or trailing
FDT bytes. The runtime entry point obtains that exact FDT and its explicit size
from `KrnGetBootInfo()`, obtains the verified bundle digest from the kernel's
retained BootInfo-v4 payload identity, and hashes the FDT with the same original
APL SHA-256 implementation used by the A4 observer. Only the exact
`apple,dockchannel-uart` authority returned by that parser can initialize the
writer; initialization performs no I/O. After interrupts are disabled and the
fourth UART-domain release has been validated, the writer polls TX-free at
offset `0x4014` and writes one byte at `0x4004`, with a total 1,048,576-poll
budget, a 32-KiB transcript limit and permanent failure after any ambiguous
access. It accepts no address, channel ID or digest override. The locked J313
FDT does not yet describe DockChannel, so a reviewed physical final-FDT
augmentation and native M1/M5 qualification remain mandatory; no DockChannel
or PMGR hardware behavior is claimed.
The resource also exposes a generation-token reset hold for one active domain
with exactly one reference: begin disables the device, asserts reset and blocks
all acquire/release calls; end validates the exact token, deasserts reset, then
reenables the device. Any ambiguous readback faults PMGR permanently. This is
the lower-level authority used by RTKit recovery, not a general client-reset
escape hatch. No hardware behavior is claimed until a reviewed M1 client chain
passes.

The pre-hardware R0/R1 substrate is one `rtkit.resource`, shared by every Apple
ASC coprocessor client. Its bounded final-FDT parser accepts only complete ASC
v4 mailbox bindings and resolves their interrupt and PMGR providers without
MMIO. The injected transport enforces the 64-bit-message/32-bit-endpoint FIFO
order, barriers, finite polls and permanent faulting after timeout or corrupt
endpoint data. The protocol state machine negotiates RTKit v11-v12, discovers
and starts endpoints, sequences IOP/AP power, manages bounded shared-buffer
callbacks and handles syslog, ioreport, debug, application and crash events.
Shutdown requires separate AP and IOP quiesce acknowledgements before power can
be released. A faulted instance has a separate fail-closed recovery path: the
final FDT must bind the mailbox through a consumer's exact `mboxes`/`resets`
pair to one of that mailbox's own zero-cell PMGR roots. RTKit then holds that
reset while releasing shared buffers, proves reset release and only afterward
releases power. Missing, unowned or ambiguous reset authority remains
unsupported, and ordinary destroy still rejects faulted state. The resource is
priority 102 with `RTF_COLDSTART`, after PMGR and utility but before AROS.
Client callbacks own DMA/DVA mapping and run under the resource semaphore, so
they are bounded and non-reentrant. Generic QEMU keeps the resource inert on
its non-Apple FDT; no ASC or RTKit hardware behavior is claimed until the
physical R0/R1 card passes on the M1.

The pre-hardware reboot-NVMEM layer is a separate `spmi.resource`. Its bounded
final-FDT parser requires one enabled SoC-specific Apple SPMI controller, one
`apple,spmi-nvmem` PMIC child with a supported SID/address type and one
`fixed-layout` catalog of uniquely phandled cells. The locked J313 description
selects the SERA PMIC at SID 15 and resolves `shutdown_flag`, `boot_stage`,
`boot_error_count`, `panic_count` and `rtc_offset` without hard-coded phandles.
The priority-101 `RTF_COLDSTART` resident only parses and initializes callbacks;
`SPMIStart()` performs the first controller read. Transactions accept only the
selected SID, use finite polling, validate opcode/SID/ACK/parity and exact FIFO
length, and fault permanently after ambiguity. Cell writes preserve adjacent
bits with read-modify-write and require exact readback. The public resource
exposes only FDT-selected cell operations rather than arbitrary raw bus access.
No SPMI register or PMIC cell has been accessed on hardware.

The separate pre-hardware R2 candidate adds `smc.resource` as the first RTKit
client. Its bounded final-FDT parser requires one enabled SoC-specific
`apple,smc` node, exact `smc`/`sram` register identities, a nonzero mailbox
phandle and the complete four-cell `apple,smc-reboot` NVMEM description. The
transport-independent state machine implements endpoint `0x20`, the shared
SRAM negotiation, 4-bit request identities, read, write and read-write keys,
notifications, finite polling and permanent faulting after a corrupt reply or
timeout. The priority-100 `RTF_COLDSTART` resident only discovers the bounded
FDT; it does not boot RTKit or touch MMIO until `SMCStart()` is called. A
faulted RTKit/SMC session is deliberately not destroyed because coprocessor
state is unknown. SMC resolves all four reboot cells through `spmi.resource`
during inert discovery, but the raw key API still does not expose reboot or
poweroff: those operations remain gated on physical SPMI/NVMEM qualification,
the physical R0/R1 sequence and a dedicated ordered system-off transaction.
Generic QEMU proves only that this 22-resident candidate remains inert and does
not disturb the startup contract.

From a separate build directory:

```sh
/path/to/aros-apple-core/configure \
    --target=apple-aarch64 \
    --with-toolchain=llvm \
    --with-aros-toolchain=yes \
    --with-aros-toolchain-install=/path/to/aros-crosstools \
    --without-x
make apple-boot-payloads

PROBE=bin/apple-aarch64/gen/arch/aarch64-apple/target_probe.o
file "$PROBE"
/path/to/aros-crosstools/bin/llvm-readelf -h "$PROBE" \
    | grep -q 'Machine:.*AArch64'
strings "$PROBE" | grep -qx 'AROS:apple-aarch64:probe-v1'
```

If an Apple-private kernel header changes, do not trust old generic objects in
an incremental module build. The explicit bounded clean path is:

```sh
make apple-kernel-rebuild
```

It cleans only `kernel.resource`, removes MetaMake's stale generated-include
completion stamp, regenerates the module headers and rebuilds the resource. The
private integration repository's `make test-apple-aic` then verifies both AIC
semantics and the generated dependency/symbol chain. That is host/cross-build
evidence only; A6 still requires a real M1 device IRQ.

After changing PMGR headers, configuration or compile flags, use the analogous
bounded path:

```sh
make apple-pmgr-rebuild
```

It removes only PMGR module outputs plus the stale generated-include stamp,
regenerates its ABI headers and rebuilds `pmgr.resource`. The integration
repository's `make test-apple-pmgr` rejects any early StdC dependency and
decodes the emitted resident contract.

RTKit has the same focused rebuild rule:

```sh
make apple-rtkit-rebuild
```

It removes only the generated RTKit resource outputs and completion stamp,
then rebuilds `rtkit.resource`. The integration repository's
`make test-apple-rtkit` checks the exact APL sources, locked J313 inventory,
injected transport/protocol lifecycle, FDT-authorized ordered fault recovery,
public vectors and emitted resident contract without claiming Apple hardware.

SPMI has its own focused rebuild rule:

```sh
make apple-spmi-rebuild
```

It removes only the generated SPMI resource outputs and completion stamp, then
rebuilds `spmi.resource`. The integration repository's
`make test-apple-spmi` checks the original protocol and NVMEM-cell state
machines, strict final-FDT binding, public vectors and resident contract. It makes no SPMI or PMIC hardware claim.

SMC also has a focused rebuild rule and rebuilds its SPMI dependency first:

```sh
make apple-smc-rebuild
```

It removes only the generated SMC resource outputs and completion stamp, then
rebuilds `smc.resource`. The integration repository's
`make test-apple-smc` checks the original protocol, strict final-FDT binding,
public vectors, resident contract and separate bootable candidate. It makes no SMC hardware claim.

The outputs are:

- `bin/apple-aarch64/gen/arch/aarch64-apple/target_probe.o`
- `bin/apple-aarch64/AROS/boot/apple/Devs/kernel.resource`
- `bin/apple-aarch64/AROS/boot/apple/Libs/exec.library`
- `bin/apple-aarch64/AROS/Devs/pmgr.resource`
- `bin/apple-aarch64/AROS/Libs/utility.library`
- `bin/apple-aarch64/AROS/Devs/rtkit.resource`
- `bin/apple-aarch64/AROS/Devs/spmi.resource` (only in the explicit R2 candidate)
- `bin/apple-aarch64/AROS/Devs/smc.resource` (only in the explicit R2 candidate)
- `bin/apple-aarch64/AROS/Devs/task.resource`
- `bin/apple-aarch64/AROS/boot/apple/Boot/aros-apple-stage2.bin`
- `bin/apple-aarch64/gen/arch/aarch64-apple/bootstrap/aros-apple-stage2-test.bin`
- `bin/apple-aarch64/gen/arch/aarch64-apple/bootstrap/aros-apple-stage2-a4-observer.bin`
  (only after `make apple-stage2-a4-observer`; never installed as production)
- `bin/apple-aarch64/gen/arch/aarch64-apple/bootstrap/aros-apple-stage2-a4-cache-observer.bin`
  (only after `make apple-stage2-a4-cache-observer`; physical diagnostic only)
- `bin/apple-aarch64/gen/arch/aarch64-apple/bootstrap/aros-apple-stage2-a4-cache-test.bin`
  (only after `make apple-stage2-a4-cache-test`; generic AArch64 regression)
- `bin/apple-aarch64/gen/arch/aarch64-apple/bootstrap/aros-apple-stage2-a5-observer.bin`
  (only after `make apple-stage2-a5-observer`; physical diagnostic only)
- `bin/apple-aarch64/AROS/C/TrustVerify` and
  `bin/apple-aarch64/AROS/C/TrustTransitionVerify` (only when the explicit
  recovery targets are requested)
- `bin/apple-aarch64/AROS/C/TrustAcceptedPlan` and
  `bin/apple-aarch64/AROS/C/TrustAcceptedObserve` (only when their explicit
  recovery targets are requested)

The probe's ELF machine must be AArch64 and its read-only data must contain
`AROS:apple-aarch64:probe-v1`. The probe also emits `bootinfo_validate.o`; the
private integration repository's `make test-boot-abi` checks the generated
MIT/APL layouts for drift and runs the validator's positive and negative unit
cases. Its `make test-aros-core-output` additionally rejects missing symbols,
undefined symbols, non-AROS or non-AArch64 ELF headers, and relocation types
not supported by AROS's AArch64 bootstrap loader.

The recovery commands are deliberately outside `apple-boot-payloads`: they are
bounded AROS commands, not residents and not boot-time policy mutation. The
primitive accepts one canonical Ed25519 SubjectPublicKeyInfo PEM, one raw
64-byte signature and at most 256 KiB of exact message bytes. The transition
engine accepts a closed, at-most-2-MiB byte-preserving capsule; it independently
checks its binary hashes and inventory, four strict public policy documents,
policy/key identities, monotonic epochs, planned or emergency semantics,
revocations and both recovery thresholds when the recovery authority rotates.
It uses 16 keys and 128 records as hard maxima and wipes its bounded caller-
owned workspace before returning. Its Public Domain OpenBSD curve source is
pinned and documented in
`recovery/PROVENANCE.md`; an APL wrapper supplies caller-owned workspace and
Mbed TLS 3.6.7 supplies SHA-256 and SHA-512. The build excludes signing, key
generation and random-number entry points. The private integration repository's
`make test-target-ed25519` checks RFC 8032 vectors, strict malformed-key and
signature negatives, independent OpenSSL signatures, source custody, the AROS
ELF boundary and zero x18 references. `make test-target-trust-engine` runs
planned and emergency transitions through the host and exact target engine,
including threshold and corruption negatives. The hosted tests execute both
commands under Darwin-hosted AROS. Archive parsing stays host-side: the capsule
producer first verifies the custody and nested bridge archives, while the
target bridge command separately streams the six signed release assets. The
accepted-state planner selects two redundant records and computes only an
in-memory successor. The narrower observer selects and hashes the same record,
queries `kernel.resource` for the retained BootInfo v4 payload identity and
emits canonical evidence; it has no planner, Ed25519, SHA-512, storage-mutation
or reboot path. None of these commands persists an accepted record, performs a
hardware update or claims Apple-hardware execution.

The second stage is strict-aligned freestanding APL code with fixed maxima of 256 resident
modules, 8,192 sections, 2,048 BSS ranges and 512 memory-map records. It
treats the first bundle record's zero-filled remainder as its only arena,
switches from the unreported firmware stack to a linked 64-KiB owned stack,
separates read-only and writable allocations on a page boundary, checks every
ELF/table/range before use, binds every module's `SysBase` reference to the
storage in `kernel.resource`, implements only the reviewed AArch64 relocation
set, tracks and terminates the BSS list, and locates `Kernel_resident` plus
`Exec_resident`. Before relocation it requires every live loader page allocation
to have a complete, non-overlapping BootInfo v4 ownership record contained by
exactly one loader-data range. Raw UEFI descriptors and fixed normalized m1n1
ranges pass through the same validated internal view. It converts available ranges and owned handoff fields
into drift-checked AROS `KRN_*` tags; excess fragmentation retains the eight
largest ranges in address order. The linked payload has no remaining
relocations. After installing owned vectors it permits FP/SIMD for the generated
AROS entry glue and, once the 848-byte runtime frame plus 816-byte task contexts
own vector state, for EL0 task execution. Apple early-runtime kernel and Exec C
remain compiled with general registers only. The production
binary has no QEMU marker; the test entry differs only by its final semihost
observation.

Before enabling translation, the same stage two derives an early device map
from the final BootInfo FDT. A non-Apple root selects an explicit empty profile.
An Apple root must provide a direct `soc` or `soc@...` node with explicit one-
or two-cell widths and empty identity `ranges`; only enabled direct-child `reg`
tuples become mappings. The parser bounds the FDT at 2 MiB, depth at 32,
structure counts, 128 source ranges and 64 merged ranges. It rejects page zero,
addresses outside 48 bits, bus-relative descendants, unused contract data and
every overlap with the validated RAM plan. Device descriptors are EL1-only
read/write, Outer Shareable, access-flagged, PXN/UXN and MAIR index 0
(`Device-nGnRnE`); RAM stays normal write-back at index 1 with the existing W^X
split. The private integration test runs this production table builder over the
locked J313 map and walks the resulting UART/AIC/PMGR plus exclusion
descriptors. This establishes an off-target A4 contract only; it does not prove
MMIO, cache or TLB behavior on Apple hardware.

The explicit `apple-stage2-a4-observer` target closes the corresponding
physical-observation seam without introducing another loader, FDT parser or
MMU implementation. Production and observer link the same
`stage2_loader.o`, `stage2_fdt_map.o`, `stage2_mmu.o` and
`stage2_prepare.o`; the production main calls that preparation function with
no observer, while the A4 entry supplies callbacks immediately before and
after translation activation. The diagnostic derives its console from the
`serial0` FDT alias and `apple,s5l-uart` compatibility, uses only bounded TX
status/data accesses to the UART already configured by m1n1, and never reads or
writes AIC, PMGR or any other peripheral. It emits an original freestanding
SHA-256 of the exact final FDT, map counts, selected role ranges, architectural
registers and descriptors for stage-two text, writable RAM, UART, AIC, PMGR,
page zero and one disabled-node sample. Finally it reads unmapped page zero;
the ordinary owned vector frame accepts only the exact current-EL translation
fault at the compiled instruction, records it, advances ELR once and returns.
Every other exception still halts. This observer is permitted only after A0-A3
on the physical target, is not included by `apple-boot-payloads`, and cannot by
itself make an A4 hardware claim.

The separate `apple-stage2-a4-cache-observer` extends that exact physical
observer with one APL-owned, page-aligned 4-KiB RAM experiment. It never creates
cacheable/non-cacheable aliases. The page starts writable and non-executable;
the diagnostic writes a two-instruction program, cleans data to the point of
unification, applies break-before-make plus EL1 inner-shareable TLB invalidation,
changes the page to read-only/executable, invalidates instruction cache to the
point of unification and executes it. It returns the page to RW/NX and repeats
with a different result so a stale I-cache line cannot pass. The final state is
RW/NX and all 1024 words are written and read back as zero. Descriptor walks,
CTR-derived line sizes and exact maintenance counters are emitted only after
the translation observer record; `[A4C] BEGIN` is emitted first so a silent
hardware stop remains classifiable. The normal stage two and translation-only
observer do not link this experiment.

`apple-stage2-a4-cache-test` runs the same cache/TLB object after the shared
loader/FDT/MMU preparation on generic AArch64 QEMU. Its ordered `[A4CQ]` trace
is architectural regression evidence only and cannot make an Apple hardware
claim or emit any `[B2]` cold-start marker. Cross-object state used by this
freestanding PIE is resolved PC-relatively; a retained `.got`, `.rela` section
or dynamic relocation is rejected because link-time absolute table contents
would not follow the payload when m1n1 or UEFI loads it at a physical base.

Native Apple AROS does not reserve x18. Stage two, the kernel and native
modules may allocate it like any other temporary, and dispatch saves and
restores it with x0-x28 as ordinary task state; the dispatch proof seeds a
distinct x18 per task and checks it survives each switch. Only binaries that
must also run under Darwin-hosted AROS, where Darwin owns x18, are built with
`-ffixed-x18`.

The explicit `apple-stage2-a5-observer` runs the ordinary production loader,
MMU, kernel preparation, Exec initialization and physical-memory importer. It
adds bounded UART observations immediately before and after the import; it does
not replace the importer or create an observer-only allocator. Before any
reclaimable BootInfo page can be overwritten, it emits every validated source
memory range, every complete owned-allocation record and every retained plan
range with their source indices, kinds, counts and byte totals. The wire first
binds the BootInfo v4 generation, slot, flags, exact verified bundle size and
SHA-256, so installed-byte readback and the executing payload cannot silently
diverge. Recording the allocation extents, used subranges, types, flags and
module indices lets an independent raw-evidence verifier replay each
release/retain relationship instead of trusting a normalized observation. A
shared topology validator
requires every available range exactly once, every release/retain range to
match one owned allocation, runtime/reserved ranges to remain absent, and
unowned loader slack plus address holes to remain absent. After import it emits
the kernel-validated header, range, reclaimed-byte and total-free counts.

Producer scope is part of the wire contract. Raw UEFI descriptors from the
installed path are `uefi-full`; only that scope can eventually substantiate
the real-map A5 milestone. The direct m1n1 planner intentionally exposes one
owned sandbox and is always `m1n1-sandbox`: it is useful for development but
cannot be promoted into a full physical-memory claim. The observer is not in
`apple-boot-payloads`, touches no peripheral except the FDT-selected UART and
has no QEMU or A4 marker.

The private integration repository's `make test-stage2-loader` executes the
same implementation against the exact cross-built modules on the host and
checks the complete relocation set, resident self-pointers and the startup
message with fragmentation reduction. It has explicit module-order,
exhausted-arena, memory-version, memory/ownership overlap, ownership
completeness, descriptor-type and unsupported-relocation negatives. `make
test-aros-core-bundle` then executes the raw stage two after UEFI exit from both
EL1 and EL2 source paths and calls the contract validator compiled into the
relocated real kernel. A correctly hashed unsupported-relocation bundle must
reach stage two and fail closed. The positive paths also require a reversible
memory proof, valid memory header and resident list, and a shared non-null
ExecBase from both source exception levels. `[B2K]` then requires a valid
KernelBase from `InitCode(RTF_SINGLETASK)`. `[B2O]` additionally requires the
complete page ownership manifest from both source levels. `[B2P]` freezes the
full topology in retained storage, imports every eligible conventional range,
reclaims every release-after-stage-two extent, and allocator-probes each
reclaimed header. `[B2L]` additionally requires the exact selected bundle
identity to survive private tags, startup preparation and an independent
kernel-owned query after the reclaim boundary. `[B2V]` then installs the relocated 2-KiB runtime vector table,
enters a bounded probe on a dedicated EL0 stack, validates SVC dispatch and
nested supervisor execution, and returns through the complete exception frame.
`[B2D]` wraps that stack in a boot task, creates two more tasks with
`NewAddTask()`, and proves the exact six-switch Exec schedule while preserving
bounded stacks, x19-x28, q19 and complete cleanup. `[B2C]` then validates the
exact core resident priorities, enters
`InitCode(RTF_COLDSTART)` from EL0 and requires the real bootstrap task, Exec
service tasks, the inert-on-non-Apple PMGR resident contract, utility library,
task resource and the ordered real AROS
`aros`/`iffparse`/`locale`/`stdc`/`stdcio`/`posixc` runtime closure. Missing-
runtime, missing-utility and duplicate-module bundles fail closed. The later
`[B2F]`, `[B2B]` and `[B2S]` gates now
open the timer/DOS/boot-service closure and mount the retained read-only FAT
image as `SYS:`.

The private integration repository's separate 27-component framebuffer
candidate adds the existing generic OOP/HIDD/graphics closure without changing
the stable product. After `[G1Q]` validates the standard `BL_Video` handoff, the
candidate allocates the inherited mode and activates an ordinary
`View`/`ViewPort`, configures four pens with `SetRGB32()`, and renders four
quadrants with `SetAPen()`/`RectFill()`. The cold-start observer compares the
current truecolor `ReadPixel()` low-byte result and complete native RGBX/BGRX
samples in the declared aperture before `[G2Q]`. Generic EL1/EL2 execution is
integration evidence only; physical G0-G2 remain open.

The next separate candidate has 36 components and 34 residents. It adds the
ordinary generic `layers.library`, input HIDD/device, keymap and
`intuition.library` closure. The UI/input residents are retained in the
candidate's complete resident table but initialized only after IFFParse and
Locale exist, matching their real cold-start dependencies. AROS startup memory
is advertised as `MEMF_CHIP` as well as public/local/fast so the inherited
display HIDD can satisfy Intuition's displayable-bitmap contract. The G3 probe
opens a custom `Screen` and bordered `Window`, verifies their object graph and
layer-backed RastPort, renders through public graphics calls and compares both
`ReadPixel()` and native aperture bytes before `[G3Q]`. It does not first run
G2's retained full-screen visual probe: G2 intentionally leaves its View active
for capture, while G3 must own the sole final displayable bitmap. The separate
G2 candidate remains independently tested. Generic EL1/EL2 execution proves
the software closure only; physical G0-G3 remain open.

That image carries `ABIPROBE`, a deterministic AROS ELF64 AArch64 CLI command
built by the hosted SDK with x18 reserved, so the same binary also runs
hosted, and emulated TLS selected explicitly.
Cold-start proof v9 passes it `alpha "two words" SYS:B2PASS.TXT`. The command
consumes ten integer arguments including the AAPCS64 stack pair, passes and
returns 128-bit SIMD values, and exercises public Exec, Utility and DOS library
vectors including exact seek/read/EOF behavior on the 36-byte sentinel, and
mutates initialized and zero-initialized `_Thread_local` objects through the
compiler-rt emulated-TLS runtime. `[B2A]`
also retains the existing `LoadSeg()` relocation, R/W-to-R/X, return-30 negative,
return-73 success, `UnLoadSeg()` and page-reclamation contract. Failed QEMU
cold-start diagnostics now print the exact CLI returns, argument size and all
six runtime-library bases.

A separate requirement-bound FAT fixture carries `TLSPROBE`, an exact
81,608-byte object produced reproducibly by the rebuilt current hosted SDK.
It creates two pthread workers and forces both to coexist before release. Each
worker must receive distinct initialized and zero-initialized compiler-rt
emutls addresses, preserve private values across the overlap and remain
distinct from the calling process. Proof v9 loads the object through native
DOS, requires return 74, observes both W^X directions and exact page recovery,
then emits `[B2X]`. Generic UEFI source EL1 and EL2 pass. Removing only
`TLSPROBE` while retaining `TLSTEST.REQ` fails closed before the marker.

The AROS Clang 11.0.0, 11.0.1, 20.1.0 and 23.1.0-rc3 patch sets default to
compiler-rt emulated TLS backed by pthread keys; `-fno-emulated-tls` remains an
explicit override. Emulation is the interim model, not the design: every access
scans the pthread table, and code running in a task that is not a pthread (a
hook, an OOP method, a library task) shares the main thread's values. Native TLS
replaces it (integration repository, D-137): `TPIDR_EL0` saved per task by
exec, TLS descriptors with a module id per loaded module, and per-task blocks
allocated on a task's first access. The installed Clang 20 binary predates that default
correction, but an explicit `-femulated-tls` object now passes both hosted and
native runtime directions. The concurrent object also passes in the reverse
direction when built by the native SDK and run under the rebuilt hosted
runtime. Rebuilding the installed compiler default remains open; concurrent
pthread-key isolation no longer does.

The Apple scheduler preserves the opaque `TPIDR_EL0` value in each task's
private context, initializes it to zero for every task, and clears it on CPU
entry and idle. The public ExceptionContext stays 280 bytes; its existing
alignment gap holds the private value, before SIMD at offset 288 in the
816-byte allocation. No-switch exception returns preserve a lazy TCB update.
This context support does not enable native compiler TLS or provide TCB
allocation, retirement, module unloading or destructors. Those require a
coherent loader/runtime and compiler generation. Source checks and the
separate native qualification contract are described in
[the task-pointer tests](../../developer/debug/test/kernel/apple_task_tls_host/README.md).

Before the runtime table is active, Apple-specific kernel code may inspect
`CurrentEL` only while stage two is known to be at EL1. Afterwards,
`KrnIsSuper()`, `KrnCli()`, `KrnSti()` and Exec `Supervisor()` use the owned SVC
boundary, so EL0 never reads the undefined `CurrentEL` register or links Exec to
a kernel-internal helper. The legacy long-lived `SuperState()`/`UserState()`
pair remains fail closed pending a separately reviewed persistent privilege
contract; bounded `Supervisor()` callbacks and task dispatch are supported now.
