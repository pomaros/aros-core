<!-- Copyright (C) 2026 John KNIPPER. AROS Public License (APL) Version 1.1. -->

# Owned-buffer HOST regression

Runs the actual `user_buffer.c`, `user_service.c`, mapping, domain and service
engines with ASan/UBSan, using a separate simulated translation/permission
oracle. All test calls represent a caller-held common mapping/service lock;
this suite does not qualify real translations or concurrent SMP callers.

```sh
python3 developer/debug/test/kernel/user_buffer_host/run.py \
  --out /absolute/fresh/output/directory
```

Output must be outside the checkout. Dirty inputs require
`--allow-dirty-diagnostic`, reporting `DIAGNOSTIC_PASS`. Clean committed inputs
report `PASS`; receipts record source/compiler/input/binary identities, build
commands and exact assertion output. Each build has a 30-second deadline and
each executable a 10-second deadline. Crashes/sanitizer output/compile failures
never count as detecting semantic negative controls.

Coverage includes cross-page and adjacent-object copies; foreign domain and
second-object ownership; bounds/overflow and pin-count overflow; unmapped/guard
ranges; software and platform read-only permissions independently; page/domain
and captured worker generations; preflight without partial output; mapping and
domain close with retained backing; and registry generation exhaustion.

The actual service adapter runs normal completion plus queued/running cancel,
unmap during work, domain close during work, and unmap/close after completion.
Every rejected publication leaves output untouched and every case drains both
request and buffer ownership before final reclamation. Worker completion uses
its captured token, so reuse of a kernel record cannot redirect a delayed reply.

Eleven semantic controls must fail at named assertions: wrong mapping owner,
wrong domain owner, stale domain generation, omitted page permission check,
stale page generation, ignored mapping close, omitted mapping pins and stale
worker token acceptance, resetting a record generation on retirement, conflating
thread identity with memory authority, and copying with the wrong authority.
A cross-queue test deliberately creates identical queue tokens for different
domains; the retained record generation must still reject the old worker. The platform oracle is independent of the mapping
ledger, allowing one check to be broken without another hiding it.

See `rom/kernel/mapping/user_buffer.h` and `user_service.h` for caller contracts.
This is an anonymous resident-buffer mechanism, not a POSIX copyin interface,
faulting pager, public kernel ABI or proof of restricted task admission.

Two distinct requester identities also submit through one domain/queue. One
thread cancels and leaves while the other collects its own reply. Foreign-thread
cancel/collect is refused, retained memory stays live, and final close drains.
