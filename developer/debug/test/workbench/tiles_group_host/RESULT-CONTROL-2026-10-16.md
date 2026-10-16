# Initial Control drawer and cached lift — 2026-10-16

Task17 delivers the initial lifted Control presenter, not complete TILE3/TILE4.
Production branch: `codex-tiles-control`; runtime frozen at `91b911d19f`.
Later commits contain only regression tests and documentation.

## Delivered behavior

`TilesControl` explicitly provisions genuine, idempotent Control and child `.info`
files. Four definitions target typed network/Bluetooth RADIO, sound LEVEL and
battery PERCENT contracts. Existing user definitions survive installation/retry.
The providers from `codex-system-indicators@9b72c5a6ef` are a separate integration;
unavailable providers retain static icons instead of invented state.

The resident Tiles service loads bounded `SHOW=lifted` drawers, validates capacity
and layout, and rejects nested group drawers. LAmiga C lifts Control at the pointer;
LAmiga D includes cached desktop tiles from other owners. Escape, the lift key
again and an outside click dismiss the nonactivating toolbox presenter. ARexx
`TILE LIFT [group]` and `TILE DROP` provide the equivalent command path.

Independent presenter snapshots retain copied pixels and fallbacks. Clicks check
registration/frame identity and current placement before bounded asynchronous
owner delivery. Dead, stale, suspended or removed owners cannot leave borrowed
bitmap pointers in the presenter; refreshing a snapshot retains its bitmap addresses.
Source requests remain bounded, and lift/drop never waits for a provider.

Mouse releases follow the matching intercepted press across lift transitions.
The service checks the front screen before opening and queued actions, and dismisses
on the next service check after a screen change. This is sampled validation, not an
atomic screen-switch/input guarantee. Snapshot repaint runs one stacking-order suffix,
avoiding repeated triangular repaint of every following tile.

## Host verification

Fresh actual-source suites passed with ASan/UBSan and warnings-as-errors:

- `control-final-host-01/tiles_group_host`: parser, layout, placement and bounds.
- `control-final-host-01/tiles_lift_host`: allocation rollback, dimming to 30%,
  clipping, item refresh/overlap, hit routing and message-port cleanup.
- `control-final-host-01/tiles_control_install_host`: preservation, seed copying,
  failures/retry and exact provider ToolTypes.
- `control-final-host-01/tiles_host`: existing grid, tile lifecycle, hover and
  indicator suites plus cross-owner snapshot ownership, capacity, frame/generation
  rejection, copied fallbacks, stale/dead owners and deferred reclamation.
- `control-final-host-02/tiles_input_host`: bounded broker queue, keys and matched
  mouse releases, including a press preceding the lift.
- `control-final-host-02/tiles_group_load_host`: actual loader accepts exactly40,
  rejects41 before loading, rejects incomplete enumeration and capacity growth;
  two separately compiled bad-code mutations fail assertions.
- `control-final-host-02/tiles_front_host`: actual front-screen helper and routing,
  queued screen changes and deferred dismissal; three bad-code mutations fail.

Directories above are under `aros-build-tiles-indicator-api/`. Host OS/graphics
fixtures establish lifecycle and routing behavior, not physical input or GPU timing.
The prelift mouse-release regression also rejects the old condition under an
isolated mutation run (`/tmp/tiles-input-host-release-01/mutant.log`).

## Native verification

Sequential canonical native builds passed for Workbench, Wanderer, Tiles,
TilesControl and appicon_control_probe. Retained final logs:
`control-workbench-02.log`, `control-wanderer-02.log`, `control-service-06.log`,
`control-install-02.log`, `control-probe-02.log` in the production build directory.
Wanderer reports two instances of its existing non-literal-null conversion warning;
other final logs contain no warnings. Exact binary/log hashes and shared-header
consumer timestamps are retained in `control-native-2026-10-16.json`.

Both Workbench and Wanderer consume the private WBTile structure. Rebuilding only
one is invalid after a layout change. The VM packager now rejects stale consumer
objects and stale linked binaries before creating its immutable output directory.
The guard also passes against the final native binaries.

## Actual guest verification

Immutable attempt021 uses VM branch `codex-tiles-control-vm` source
`9073604b288e161432bb3cac9113b692de29561b`, with production runtime91b911d19f.
The first strict UP TCG run passes: five lift/drop pairs, zero renderer increments
for pure operations, six focus-key receipts, actual toggle changes and pixels,
a separately owned AppIcon click and refreshed lifted pixels, and source-held
interaction followed by a safe late reply. Initial measured service operations:
open15–43ms and drop8–34ms. These measure service execution, not physical
key-to-visible latency. Source hold27338–35331 encloses lift/drop28880–32867.
The unchanged bootstrap gate, three-second post-completion observation, final raw
log scan, empty QEMU error trace and orderly QMP shutdown/exit0 pass.
The independent fresh same-image repetition `attempt021/tcg-02` also passes:
open15–31ms, drop6–36ms, six focus keys, ten replies and a held-source interval
31417–39396. Full verdicts and artifacts are retained under
`aros-tiles-validation/attempt-021/tcg-01` and `tcg-02`; the VM branch contains
`developer/debug/test/workbench/tiles_control_vm/RESULT-2026-10-16.md`.

Earlier failures remain preserved: attempt019 opened its test focus window before
the existing desktop readiness gate; a bounded readiness barrier fixes the fixture.
Attempt020 used mismatched WBTile layouts in Workbench and Wanderer and omitted
desktop snapshots. A complete rebuild fixes the omission; the packaging guard
rejects the actual stale objects as well as synthetic negative controls.

## Remaining full-feature gates

TILE3 is not complete: creating groups by drag/drop, dragging members back to
left-out entries, drawer-position synchronization, SHOW=always/edge and mode-change
anchoring remain. Four defaults do not satisfy the seven-item Control requirement.

TILE4 is not complete: the fallback periodically brings its toolbox window forward,
so a newly opened foreign window can briefly cover it. No compositor, hard layer
ordering, hardware/SMP result, physical input latency, screen-switch guest proof or
full40-item worst-case latency is claimed. Lifted hover/fast pointer interaction is
not part of this initial presenter; existing ordinary desktop hover remains separate.
A provider that never returns a borrowed request can defer service shutdown; normal
service activity continues without freeing a live reply port.

Usage and ARexx examples: `workbench/system/Tiles/CONTROL.md`.
