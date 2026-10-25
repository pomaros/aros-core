# IPI lock and claim host regression

Run from any directory with Python 3 and Clang:

```sh
python3 developer/debug/test/exec/ipi_host/run.py
```

`--core PATH` selects another source checkout; `--cc PATH` selects the host
compiler. The script extracts `ipiq_lock`, `ipiq_unlock`, `core_TryClaimCallIPI`
and `core_ClaimCallIPI` from that checkout on every run. It also reads each
production transport predicate, rather than maintaining copies of these helpers.

The mock IRQ backend and queue verify preservation of all IRQ/FIQ mask
combinations, exclusion before touching the queue, unlocking before restoration,
no callback drain while locked, bounded empty-pool try-claim, invalid-target
rejection and inline draining when the caller cannot receive its transport's
interrupt. Expected mask policy is independent of the extracted predicate.
Both native FIQ and Apple IRQ/FIQ variants run with ASan/UBSan. Negative
controls discard the saved mask and substitute the native FIQ-only predicate
for Apple's IRQ/FIQ predicate; both must fail an assertion.

These are deterministic host contracts. They do not execute AArch64 privileged
instructions, SVC transitions, real spinlocks, inter-CPU races or guest scheduling.
Target compilation and bounded VM/native SMP qualification remain necessary.
