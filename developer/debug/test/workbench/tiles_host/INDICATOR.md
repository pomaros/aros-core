# Geometry and suspension regression

`test_indicator.c` executes the actual library `WBTileQueryGeometry`,
`WBTileSuspend`, `WBTilePublish`, query, detach and reply collection paths.
It also calls the actual `AppIcon_TileVisibility` and `AppIcon_TileGeometry`
presenter adapters. OS/graphics services and the clock remain host fixtures.

```sh
python3 developer/debug/test/workbench/tiles_host/run.py --output /tmp/indicator-host-new
```

Coverage includes owner/nonowner queries; zeroed invalid outputs; exact 1000ms
staleness (including fresh geometry with stale visibility); hidden, invalid,
width/height-mismatched and moved geometry; full 2x2 bounds despite partial
clipping; explicit suspend with bounded asynchronous redraw; heartbeat refusal
to revive; wrong-sized and allocation-failed surfaces; successful resume on the
same registration at normal cadence; immutable borrowed enter messages and late
leave replies after detach/removal. Geometry remains available for a visible,
fresh static fallback while suspended.

The API promises registered-owner success with zero geometry when no fresh
visible placement exists. It never exports a screen/owner pointer. Suspension
keeps owner/port registration intact, stops dynamic freshness and hover, and
requires a successfully copied new frame to resume. No destructor touches a
message still owned by its recipient.

Recorded host run `aros-build-tiles-indicator-api/host-003` passed ASan/UBSan,
including existing tile/grid/pointer regressions. The output retains generated
source, compiler command, logs and a SHA256-based result. Native compilation
and real desktop placement/suspension evidence belong to the integration lane.
