# Restricted native-alias propagation component check

Run `python3 run.py --out /absolute/fresh/path/outside/source`.

The sanitized HOST fixture extracts the native protection bridge and actual
restricted-domain iteration. It checks arena-only policy propagation, writable
JIT sources becoming privileged NX aliases, partial refusal/retry, skipping
retired roots, and no allocation while the modeled map lock/rendezvous is held.
Four semantic mutations must fail at their specific assertions.

The generic aarch64_mapping_host suite separately executes the actual per-leaf
protection engine, including expected physical ownership, guard restoration,
EL0-access/execute refusal and malformed permissions. These serialized models
are not native SMP/cache or complete executable publication evidence.
