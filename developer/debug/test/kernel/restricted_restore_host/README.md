# Restricted context restoration

Run `python3 run.py --out /absolute/fresh/output` outside the source tree.
The fixture extracts the production copy and restore helpers, uses modeled
page ownership/translation, and runs with address/undefined sanitizers.

It checks all 31 GP registers, both lanes of all 32 SIMD registers, SP/PC,
NZCV and FP state; kernel roots, TLS and exception metadata remain unchanged.
Failures cover a missing second page, mismatched physical stack backing,
unowned/overflowing/misaligned sources, wrong record size, invalid target PC
and SP, privileged PSTATE bits, oversized FP fields and pending work.
Rejection preserves the whole exception frame; source admission failure also
preserves the destination record. Both stack and mapped sources are exercised.

HOST results do not prove actual exception return. The API30 restricted_vm
payload separately snapshots every restored architectural register and checks
all 100 words against the source record under QEMU, with a wrong-size refusal
and a drained lifetime assertion. That test must actually pass before runtime
qualification is claimed.
