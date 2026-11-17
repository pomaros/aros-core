<!-- Copyright (C) 2026 John KNIPPER. AROS Public License (APL) Version 1.1. -->

# Restricted JIT release-store regression

```sh
python3 developer/debug/test/kernel/restricted_jit_store/run.py \
  --cc /path/to/llvm/bin/clang --out /absolute/fresh/output
```

The HOST runner requires an AArch64 host with FEAT_LRCPC2, Clang and its
ASan/UBSan runtimes. The second execution uses QEMU TCG's `max` CPU. Both
compare the common decoder and release writer with assembler-generated
STLURB, STLURH and 32/64-bit STLUR instructions, including signed offsets,
SP, zero registers and the restricted fault's operands. Twelve instructions
each run with sixteen values; surrounding bytes must remain untouched.
Alignment, address overflow, loads and unrelated stores are refused.

The extracted production JIT fault classifier and emulator use a modeled
ownership adapter and the production exception-frame layout. Checks cover
mode, instruction/data permissions, exception syndrome, instruction address,
effective address, operand bytes, unchanged registers and refusal without a
write. Three deliberately broken variants must fail their intended checks.
The runner verifies the trap dispatch excludes refused JIT-to-JIT stores
from lazy fault repair. Page-table ownership itself is covered by the
[space adapter regression](../restricted_space_host/README.md).

The generated AArch64 ELF must contain release stores of all four widths in
the production writer; a relaxed-order control must lose that property.
Sequential byte comparisons alone do not establish memory ordering.

QEMU execution is a freestanding mechanism test with a stopped-register
preflight, bounded completion and exact output/exit requirements. It does
not exercise SPRR, an actual data abort, production scheduling, concurrent
JIT modification or installed product behavior. `result.json` records source
hashes, commands and ELF identity; dirty sources report `DIAGNOSTIC_PASS`.
`--source` selects a separate common/composed source tree without editing it.
Composed sources retain their manifest and actual input hashes and report
`DIAGNOSTIC_PASS`, since they have no Git working-tree cleanliness evidence.
The runner's own file hash is retained separately from the selected inputs.

`RestrictedJitStore.c` plus `guest.S` builds with the matching AROS SDK and
standard runtime libraries. It runs four release-store widths from a JIT
page into another JIT page through the production restricted API. The native
controller checks every destination byte, the return status and resource
release. A registered lazy repair handler exits with status 91 if called;
it must not be entered for these already committed pages. An unsupported
ordinary store must end with a permission exception in SPRR mode, and may
succeed in RWX mode. Require the final PASS and command RC zero; compilation
alone does not establish guest execution.
