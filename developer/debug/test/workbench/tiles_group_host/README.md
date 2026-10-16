# Drawer group parser and geometry tests

```sh
python3 developer/debug/test/workbench/tiles_group_host/run.py --output /tmp/groups-new
```

Compiles the actual `workbench/system/Tiles/tiles_group.c`, without mocks,
under ASan/UBSan and warnings-as-errors. Retains source/header/test snapshots,
hashes, compiler command and logs in a fresh output directory.

Coverage: mandatory TILE=group; unrelated keys; whitespace/case; defaults;
duplicate, missing, malformed, overlong and overflowing values; 40-member cap;
nested/empty group rejection; explicit signed icon coordinates and overlap;
automatic placement avoiding existing footprints; column wrapping; bounded
extents; no input mutation; all four screen anchors, screen resize, pointer and
centred lift placement, clipping/clamping and oversized-screen rejection.

The caller must translate DiskObject NO_ICON_POSITION to explicitPosition=0,
provide actual surface sizes, and reject drawer members (isGroup=1 also rejects
nested groups). Coordinates are pixels, not cell indexes. Layout returns both
the original bounding origin and normalized member offsets; persistence should
retain original coordinates rather than replacing them with normalized offsets.
Automatic layout searches at most 256 rows of up to32 columns. Explicit
positions take precedence and may overlap; the parser does not silently repair
user placement. Total extent is at most32767 pixels on either axis.

LIFT is bounded printable text for the caller's key-expression parser; this
module does not claim to validate Commodities key syntax. Unknown ToolTypes are
ignored. SHOW=edge requires an anchored edge. Default columns are4; only the
Control drawer defaults to LAmiga C, other drawers default to none.

This is parser/layout evidence only. No drag, filesystem move, persistence,
input hotkey, screen tracking or full TILE3/TILE4 acceptance is claimed.
