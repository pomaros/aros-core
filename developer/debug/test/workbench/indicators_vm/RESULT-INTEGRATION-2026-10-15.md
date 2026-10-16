# Indicator geometry and reversible fallback integration

The indicators branch merges `codex-tiles-slot-bounds` at `303db11064`, including
geometry/suspension APIs and the mixed-slot selection/redraw corrections. The
presenter requires the published API directly. A panel uses the fresh complete
glyph rectangle on the same Workbench screen and clamps its actual window
size. Missing geometry leaves the panel closed. Provider lease expiry suspends
the dynamic surface; a valid new publication resumes the same AppIcon.

## Evidence

`aros-build-indicators/vm-006/tcg-01/result.json` is an actual UP TCG PASS:
normal QMP exit 0, empty host log, empty guest-errors/unimp trace, five real input
phases and three seconds of post-completion observation. The scenario retains
the source-expiry/recovery, malformed update, missing/held Settings and clean
shutdown controls from the first delivery. `verification-final.json` separately
replays hashes, raw logs and QMP events. Four fresh frames are accepted at least
500 ms apart; every initial/resumed frame belongs to one registration handle.
Measured open panels report `anchor=116,174 glyph=116,174`, including keyboard
activation. Three explicit suspensions are reported before final recovery.
The screenshot records the actual panel below the glyph. The independent
checker rejects a one-pixel mutation of the recorded actual window anchor
after rehashing the modified serial input (`integration-anchor-negative.json`).

The borrowed desktop substrate is `aros-tiles-validation/attempt-018/base-esp`
and its matching SDK is `aros-build-tiles-review-vm/bin/apple-aarch64/AROS`.
Inputs, runtime libraries and the collector are frozen and hashed in `vm-006`.
The indicator/probe executables come from the separate native indicator build.

Native Indicator/probe build logs are `integration-indicator.log` and
`integration-probe.log`; `integration-syntax.log` is the strict AArch64
`-Wall -Wextra -Werror` syntax check. The indicator state/battery ASan/UBSan
fixture passes in `integration-host`. The actual-source Workbench ownership,
geometry, suspension, pointer and grid ASan/UBSan fixtures pass in
`integration-tiles-host`; output is in `integration-tiles-host.log`.

## Retained assembly failure

`vm-005/tcg-01` is FAIL: copying the native indicator-API SDK libraries over a
VM desktop substrate left the panel without a valid geometry observation.
`vm-006` uses the SDK identified by the substrate's own build provenance and
passes. No production API change was made for this failure. The packager rejects
an SDK from a different build and rejects mismatched `AppIconQuery` or
`AppIconGeometry` declarations. `integration-packager-negative.json` retains the
successful rejection control for the mismatched SDK.

This integration evidence covers the common presenter and its existing smoke
scenario. It makes no Apple hardware, SMP, complete WM15, PWR6, radio control,
persistent Bluetooth pairing, output-selection or subsystem-panel claim. The
merged selection/redraw changes retain the host/guest boundaries documented in
their own report; this scenario does not exercise a lasso gesture.

The isolated native build requires the pinned catalogue submodules:

```sh
git submodule update --init workbench/libs/muimaster/catalogs workbench/system/Wanderer/Catalogs
```

The initial full Wanderer attempts retain their setup failures in
`integration-wanderer.log` and `integration-wanderer-02.log`: the missing Zune
header and Wanderer message identifiers came from those uninitialized catalogue
submodules. `integration-catalogs-02.log` records generation of the Zune catalogue.
No generated catalogue/header is committed.

The native Workbench library rebuild passes (`integration-workbench.log`). The
final dependency-aware Wanderer build passes (`integration-wanderer-03.log`),
including the icon-list classes. Existing pointer-conversion warnings remain
in the Wanderer build; no warning-free whole-tree claim is made.
