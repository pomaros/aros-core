<!-- Copyright (C) 2026 John KNIPPER. AROS Public License (APL) Version 1.1. -->

# Service coordinator HOST regression

Runs the actual `rom/kernel/mapping/service.c` and `domain.c` with address and
undefined-behavior sanitizers. The queue is an internal mechanism, not a public
library ABI or a platform-registered service. Its caller-held domain lock is
represented by serial test calls. There is no scheduler, guest pointer copy,
SMP memory-ordering or live native-worker qualification in this suite.

```sh
python3 developer/debug/test/kernel/service_host/run.py \
  --out /absolute/fresh/output/directory
```

Clang is the default compiler (`--cc` overrides it). Output must be outside the
checkout. Dirty inputs require `--allow-dirty-diagnostic` and are marked
`DIAGNOSTIC_PASS`, rather than a committed-input `PASS`. The receipt records
source revision, dirty status, input/binary hashes, compiler, commands and
exact per-variant output and exit status. Runtime is bounded to ten seconds
per executable, builds to thirty seconds each. Unexpected crashes, sanitizer
reports, timeouts and compile failures fail the suite.

The baseline checks request/reply copies, one request per owner, capacity and
size boundaries, zero-length operations, invalid tokens, wrong owners,
completion/collection once only, stale tokens after slot reuse and permanent
slot retirement on generation exhaustion. Cancellation/close are exercised
before work starts, during an outstanding worker lease and after completion.
A mixed-state close drains queued/ready requests but retains the active worker.
A separate mapping pin prevents domain reclamation even after the queue drains.
One thousand full-size transformations verify bytes and return membership to
its initial count each time.

Five semantic negative controls mutate copies of the implementation in the
output directory. Each must exit with its exact designated assertion, not just
any nonzero status:

| Mutation | Required failure |
| --- | --- |
| Ignore request generation | `FAIL stale-take` |
| Ignore collection owner | `FAIL collect-owner` |
| Release running request during cancellation | `FAIL cancel-retention` |
| Reuse exhausted generation | `FAIL generation-exhausted` |
| Accept completion after external domain stop | `FAIL external-stop-ack` |

The tests enumerate deterministic state-machine orderings. They do not prove
that a future caller actually holds the required lock, validates its domain
handle, or cancels and drains every native worker during task exit.
