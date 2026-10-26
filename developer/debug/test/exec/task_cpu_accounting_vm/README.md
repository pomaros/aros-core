# Public task CPU accounting record checks

The workload is `../task_cpu_accounting.c`, built by `test-exec-public-quick`.
It queries the public task resource for busy/idle windows, the current
uncommitted slice and 32 child lifetimes. It does not read private counters.

Run the retained record classifier checks from the core root:

```sh
python3 -B developer/debug/test/exec/task_cpu_accounting_vm/test_oracles.py
```

`oracles.py` checks complete single-CPU CPU21 records, CPU-time/wall-time
agreement, record ordering, reserved error tokens, QEMU trace presence and
process termination. Synthetic records only verify the classifier. They do
not qualify a guest, SMP, system sleep or physical hardware.

The historical boot wrapper emitted the guest-command and startup records
required by this classifier. Product harnesses own workload execution and
must collect those records and raw process/trace evidence before using it.
The old validation-only kernel boot hook, package script and machine-specific
QEMU runner are deliberately not part of the common OS. These host checks
are not an executable end-to-end guest test.

[RESULT-2026-10-16.md](RESULT-2026-10-16.md), `results.json` and
`timing-disclosure.json` describe historical runs only. Their source identities
and paths do not identify a current product build. `producer-disabled.patch`
is a historical negative-control patch, never production code.
