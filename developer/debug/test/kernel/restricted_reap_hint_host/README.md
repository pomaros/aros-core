# Restricted orphan cleanup admission

The Native take/complete wrappers skip orphan cleanup while no retired owner's
resources remain. Retirement publishes a pending hint; a coordinated scan clears
it only after all abandoned execution slots, managed domains and shared objects
have drained. A retained worker lease or another reaper's claim keeps it set.
The hint changes cleanup admission, not resource ownership or release rules.

Run `python3 run.py --out /absolute/fresh/output` from this directory with Clang
available. The gate extracts the production retirement and pending predicates,
uses AddressSanitizer and UndefinedBehaviorSanitizer, and requires thirteen
semantic negative controls to fail. It checks foreign/null retirement, wait
state removal, held leases, concurrent release ownership and final drain.

This is a serialized HOST model. It does not execute SVCs, Native task removal
or SMP interleavings. The [restricted task workload](../restricted_vm/README.md)
exercises external owner/worker retirement, held worker acknowledgements and
shared object/view orphan cleanup on the guest. A compiled kernel requires
that separate runtime evidence before integration.
