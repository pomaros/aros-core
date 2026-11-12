# Restricted native-alias propagation component check

Run `python3 run.py --out /absolute/fresh/path/outside/source`.

The sanitized HOST fixture extracts the native protection bridge and actual
restricted-domain iteration. It checks arena-only policy propagation, writable
JIT sources becoming privileged NX aliases, partial refusal/retry, skipping
retired roots, and no allocation while the modeled map lock/rendezvous is held.
The unchanged-data preflight checks native and every retained alias root under
the map lock/coordinator without a remote rendezvous. A mismatching native or
alias root forces the mutation path; executable requests cannot take this
shortcut. Nine semantic mutations must fail at their specific assertions,
including omission of either native or alias preflight.

The generic aarch64_mapping_host suite separately executes the actual per-leaf
protection engine, including expected physical ownership, guard restoration,
EL0-access/execute refusal and malformed permissions. These serialized models
are not native SMP/cache or complete executable publication evidence.

The alias callback carries the native VA being remapped; the fixture checks
that each root uses that address and the leaf-specific invalidator. The actual
TLBI instruction and cross-CPU effects require the AArch64 build/runtime check.
