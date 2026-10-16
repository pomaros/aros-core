# Indicator geometry and reversible fallback — 2026-10-15

Task14 supports the separately owned B6–B10 indicators. Runtime revision
`f7d4109209` adds owner-only `WBCTRLA_QueryAppIconGeometry` and
`WBCTRLA_SuspendAppIcon`; usage and ABI are in
`workbench/system/Tiles/INDICATORS.md`.

The geometry query copies the complete tile rectangle in screen coordinates,
not a clipped fragment. Visibility and geometry must both be fresh. Unknown,
foreign or removed registrations fail with zero outputs; a registered but hidden
or stale tile succeeds with zero validity and bounds. No screen/window pointer
escapes the presenter. Reversible suspension preserves registration and port,
revokes hover, and requests a coalesced asynchronous static redraw. Heartbeats
and invalid/failed frame publication cannot revive it; a valid new surface resumes
at ordinary cadence. Borrowed pointer messages retain their existing lifetime.

## Verification

Actual-source host suites with ASan/UBSan pass in
`aros-build-tiles-indicator-api/host-003` and independent `host-final-review`.
They exercise actual library code and presenter adapters, with host OS/graphics
mocks: ownership, hidden/stale/moved geometry, zero outputs, dimension mismatch,
repeated suspension, failed and successful resume, cadence, bounded notifications,
and late borrowed-pointer replies after detach/removal. Generated source SHA256:
`56b8c5c15570bfba669532861ffa3c274a520005ca85fb7cbcbba557b2dfde65`.

The actual `IconTileProject` helper passes ASan/UBSan tests for viewport clipping,
negative screen coordinates, movement and preserving full bounds. This is host
projection evidence, not an actual desktop viewport-scroll exercise.

Native dependency-aware builds pass: `workbench-libs-workbench`,
`workbench-system-wanderer-app`, then `test-workbench`, sequential goals with
`make -j4` internally. The final Wanderer and probe rebuilds include runtime
`f7d4109209` and strengthened hover probe `01921c2ba2`. The canonical environment
uses LLVM20.1.0 and `aros-crosstools`, configured for apple-aarch64 with
`--with-toolchain=llvm --with-llvm-version=20.1.0 --with-aros-toolchain=yes
--with-aros-toolchain-install=/Users/jknipper/Source/aros-personal/aros-crosstools
--without-x`; Python comes from `aros-build-smp-merged/.venv/bin/python`.
Logs and binary hashes are in `aros-build-tiles-indicator-api/native-verification.json`.
Existing baseline build warnings are retained; no warning-free whole-tree,
hardware or SMP claim is made.

## Defect exposed by the guest fixture

A 1×1 control next to a 2×2 tile rendered at its explicit grid slot, but its
SELECTDOWN rectangle still received legacy largest-icon centering. The visible
center x51 lay outside the incorrectly shifted left bound x53. Runtime
`f7d4109209` skips that extra centering for explicit slots, matching rendering.
Attempt015/tcg02 retains the failing real-input trace. Attempt015/tcg01 instead
exposed a collector assumption: initial blit can precede periodic visibility
reporting; validity plus actual pixels are the presentation gate.

Readonly review also identified older similar centering in lasso and
single-entry redraw paths. This narrow click correction does not claim to
validate those separate paths or all mixed-grid interactions.

## Actual guest acceptance

Immutable `aros-tiles-validation/attempt-017/tcg-01` and `tcg-02` both PASS on
UP TCG with the same image and collector revision `5dc961dede`. Each verifies
actual drag from (132,20) to (326,174), full 128×128 bounds under partial cover,
zero outputs under full cover, immediate active hover revocation, static
checkerboard despite heartbeats and invalid resume, and a new accepted/displayed
frame on the same handle after valid resume. A separate 1×1 control changes its
actual front-buffer pixels in response to clicks both during suspension and
after resume. Recorded suspension-to-fallback latency is 14 ms and 13 ms;
these are observations of these runs, not a universal timing guarantee.

Both runs retain raw serial/host logs, empty guest_errors/unimp traces, QMP
transcripts and register dumps. The guest remains running through a three-second
post-completion observation, then QMP stops it and quits with exit0. Screenshot
and ESP hashes are checked. Root independently replays saved serial, geometry,
coverage/fallback/resume/control pixels, hashes and final QMP sequence using
`aros-build-tiles-indicator-api/replay-indicator.py`; both retained
`independent-review.json` files PASS. The VM collector also received a separate
read-only review; its four oracle regression tests pass.

The validation branch `codex-tiles-indicator-api-vm` contains collector/probe
instructions and the detailed result under
`developer/debug/test/workbench/tiles_indicator_vm/RESULT.md`. Attempt016 was
rejected during packaging because the source contained an uncommitted report;
no image acceptance was claimed. Failed attempts remain available.
