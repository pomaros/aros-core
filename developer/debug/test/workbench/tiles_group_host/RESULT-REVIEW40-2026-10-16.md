# Review40 corrections — 2026-10-16

Production runtime `6633055968` includes M1 `c7d39fe216` and M2 `6633055968`.
M1 gives the asynchronous clock child owned NIL streams; M2 retains up to40
pending position requests and gates workers on exact child death receipts.
Queued paths coalesce to their latest position; caller memory is never borrowed.
GUI polling does not wait. Explicit quit drains accepted work and may wait for
resource recovery. Worker stack is64KiB. No unrelated layout changes were made.

Native Tiles build session38701 and Wanderer build session40133 exited0.
`NATIVE-REVIEW40-2026-10-16.json` retains native binary/source/log hashes.
`clock-launch-host-001` verifies private stream ownership/failure handling;
`transaction-host-008` exercises actual selection-loop/queue/worker admission
and lifetime code under ASan/UBSan. Restoring busy rejection, unsafe busy-only
quit, or removing transaction rollback fails the respective negative control.

Actual groups `attempt-034/tcg01` passed all17 phases. The new real Shift-select
and drag persisted both members with identical nonzero deltas (+32,+16), in
addition to the prior single-member reposition and all original pixel checks.
Its detailed report belongs to `tiles_groups_vm/RESULT-2026-10-16.md` in the
VM evidence branch. Control regression details and hashes follow.

The initial attempt033 failed packaging because the USB desktop seed exceeded
its reserved space. The packager now stages added runtime files through the
existing expanded-image path; failed artifacts remain retained.

## Review40 corrected runtime: Control 035

Actual `aros-tiles-validation/attempt-035/tcg-01` passed on production runtime
`6633055968` (M1 private clock-child streams and M2 queued selected positions),
VM merge `3d7e08a0aa`. Packaging and shared-layout/dynamic-module freshness checks
passed. The complete Control collector was unchanged; its six unit checks passed.

Real clock replied in 29 ms (3298 → 3327). Openings took 44/15/30/21/16 ms;
drops took 9/14/12/7/11 ms. The strict 50 ms limits were unchanged. Seven focus
keys, both toggle values, foreign cached pixels/activation, eleven commands and
held-source interval 28388–36390 passed. The process snapshot records concurrent demo QEMU PID5644
(`demo-tiles-settings-003`) alongside proof PID27640, retained in
`attempt-035/concurrent-load.json`. Earlier demo PID89131 was absent when
groups034 recorded its separate context. Groups034 had
already exited before this automated run began.

Final raw scans passed, guest error trace was empty, three-second observation
completed, and explicit QMP stop/quit returned exit 0. This is generic UP TCG
regression evidence, not physical hardware, SMP or compositor qualification.
The separate groups034 run covers actual multi-selection persistence.

| Artifact | SHA-256 |
| --- | --- |
| `verdict.json` | `25d4b0bb923871090bffbee45ef52fd590fef042eb4cc5e64385dfee863a559b` |
| `verified-manifest.json` | `732abcb0f29d20366f118a36a96bfc3c0026098a2de28bb5e3e2c9d368fe62ea` |
| `serial.log` | `aef9da7adc7f675eea1c6933bbc6e7707d2382e064e864cf4c6c6aed96575b77` |
| `host.log` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `guest-errors.log` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `qmp.jsonl` | `f9b8df7045b2f4dc1fbafce6a4129af482a10e9f7e0158f369eee11e8a08ea4b` |

Independent root replay passed for Control035 (raw oracles, exact ESP membership,
boot-input and screenshot hashes, pixels, logs and QMP lifecycle), and separately
for groups034. Evidence hashes:

- `aros-tiles-validation/attempt-035/tcg-01/root-replay.json`: `29a7fdc1f75cdb9d2ee4cd199b1ba0559cb4c1c529b474da516af63e5469d2c7`
- `aros-tiles-validation/attempt-034/tcg01/root-replay.json`: `d05262090e85e3742c0ec908da1a233d40dc2655a4b30fc0e70581173452aba5`
