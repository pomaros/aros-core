# Tile drawer transactions

Run from the source root:

```sh
python3 developer/debug/test/workbench/tiles_transaction_host/run.py --output /absolute/new/output
```

Compiles the production `tile_transaction.c` with ASan/UBSan and uses real host
files for the four-step create journal. Each forward rename failure restores
both original files; an existing user destination is preserved; failed undo
retains the originals under their recorded backup names. Success, path/step
bounds, duplicate/CRLF backdrop lines and append overflow are checked. Removing
the actual rollback loop must fail the same fixture. Sources, hashes, commands,
logs and mutant are retained. This tests the portable journal, not AROS DOS,
MUI input or filesystem power-loss behavior.

Wanderer's asynchronous worker copies at most one bounded request; a busy worker
rejects a new request with a beep. It stages private icon copies in exclusively
created directories and uses rename journaling, refusing destination collisions.
The built-in RAM and AFS handlers reject existing rename destinations. Any failed
undo retains files and prints every recovery path; this is process-time rollback,
not a power-loss-atomic filesystem transaction. Original icons are quarantined
beside their originals; saved copies are staged on the destination volume, so
no commit rename crosses volumes. No owner process, AppIcon, MUI object or bitmap
is borrowed.

Real guest acceptance must independently exercise tile-on-tile create, actual
drawer reposition, drag-out plus `.backdrop`, nested drawer refusal and continued
desktop responsiveness. `[TILE3] transaction` reports accepted worker outcomes;
`[TILE3] rejected reason=drawer-in-group` acknowledges the pre-copy refusal.
Group definitions are `HOME:Tiles/Group-<nonce>.info` with a matching drawer,
`TILE=group`, `SHOW=always`, `ANCHOR=free`, `COLUMNS=4`. Success updates
`ENV:Tiles/Changed`; no changes are silently accepted as a complete TILE3 proof.

The same runner extracts the production worker's `TileDropPoll` and
`TileDropFinish`: clearing busy before DOS exit must retain the exact child ID;
the GUI only reaps `CHILD_EXITED`, while explicit quit waits for `ChildWait` and
then releases the ETask through `ChildFree`. A child not yet scheduled is also
covered. Replacing this with a busy-only quit must fail. `NP_NotifyOnDeath` uses
Exec's parent ETask receipt port and `SIGF_CHILD`, not a caller-owned reply port.
Native code must still verify the real DOS/Exec lifecycle; the host uses controlled
lifecycle mocks and does not claim a real guest exit proof.

Review40 M2: `transaction-host-008` additionally extracts the actual IconList
selected-entry notification loop and the production admission, queue, launch,
poll and quit functions. Three selected members (with an unselected entry between
them) retain every coordinate. The harness checks copied request ownership,
latest pending position coalescing, 40-slot saturation, process-creation retry,
structural-operation exclusion, and a worker clearing `busy` before its actual
DOS death receipt. GUI calls never wait; explicit quit drains accepted positions
and joins every child. Resource shortages retain queued positions for retry,
including during quit. The worker stack budget is 64 KiB.

ASan/UBSan pass; restoring the original busy admission rejection fails the
multi-selection case, and replacing exact-child quit with a busy-only return
fails the lifetime case. Existing removed-rollback negative control also fails.
These host checks do not replace the separate actual VM multi-selection proof.
