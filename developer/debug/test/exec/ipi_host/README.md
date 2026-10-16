# IPI lock and claim host regression

Run from any directory with Python 3 and Clang:

```sh
python3 developer/debug/test/exec/ipi_host/run.py
```

`--core PATH` selects another source checkout; `--cc PATH` selects the host
compiler. The script extracts `ipiq_lock`, `ipiq_unlock`, `core_TryClaimCallIPI`
and `core_ClaimCallIPI` from that checkout on every run. It also reads the
generic AArch64 production FIQ transport predicate, rather than maintaining
copies of these helpers.

The mock IRQ backend and queue verify preservation of all IRQ/FIQ mask
combinations, exclusion before touching the queue, unlocking before restoration,
no callback drain while locked, bounded empty-pool try-claim, invalid-target
rejection and inline draining when the caller cannot receive its transport's
interrupt. Expected mask policy is independent of the extracted predicate.
The generic AArch64 FIQ variant runs with ASan/UBSan. The negative control
discards the saved mask and must fail an assertion. The public single-CPU
VM target has no Apple IPI transport fixture.

These are deterministic host contracts. They do not execute AArch64 privileged
instructions, SVC transitions, real spinlocks, inter-CPU races or guest scheduling.
Target compilation and SMP qualification on supported targets remain necessary.
