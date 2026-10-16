# AppIcon tile foundation

This change adds copied, paced, program-drawn AppIcon surfaces. Native target
compilation and the host regression pass. Three actual TCG guest runs on
2026-10-15 pass the scoped B2.1 task 5 checks: two-region clicks (including
rapid repeats), mixed 2x2/1x1 grid placement and dragging, paced publication,
and static fallback before five seconds after silence and abrupt owner exit.
The validation branch `codex-tiles-review-vm` records the report and artifact
hashes in `developer/debug/test/workbench/tiles_vm/RESULT.md`. This is not
acceptance of the broader TILE1/TILE5 snapshot, reboot or folding requirements.

## Owner protocol

Register with `WBAPPICONA_Cells = WBAPPICON_CELLS(w,h)`, a `BitMap *` in
`WBAPPICONA_Surface`, optional `AppIconRegions *`, and cadence in milliseconds.
Both dimensions must be 1..4; this initial presenter uses 64-pixel cells.
The bitmap must exactly match `64*w` by `64*h`. Regions are half-open pixel
rectangles, at most eight, with unique nonempty NUL-terminated inline names
of at most 31 bytes. Declaration order resolves overlapping regions. Invalid
registration returns NULL and `ERROR_BAD_NUMBER`. Allocation failure currently
uses that same error; callers must treat any NULL registration as failure.

Registration copies the bitmap, region values/names, fallback DiskObject and
label. The program may subsequently redraw or free its originals. To publish,
call `WorkbenchControlA(NULL, tags)` with `WBCTRLA_PublishAppIcon` pointing to
an `AppIconPublish { icon, bitmap }`. The bitmap is borrowed only for the call;
copying and `WaitBlit` finish before it returns. An invalid-size, allocation
failure or too-early publish returns FALSE and leaves the previous frame intact.
Cadence clamps to at least 250 ms. NULL bitmap renews the cached image lifetime and reclaims replies without
copying pixels or advancing the frame-cadence timestamp. Drawing into a bitmap alone does not publish a frame.

Clicks and menu events are delivered immediately to `ai_MsgPort`; no later
publication or pump is needed. The port must be a PA_SIGNAL port owned by the
registering task. Region messages extend `AppMessage` with an inline name
(`AppIconTileMessage`), use `AMCLASSICON_Region`, and require `ReplyMsg`.
Check `mn_Length` before reading the extension. Replies use a library-owned
`PA_IGNORE` port. A global 64-message budget bounds withheld replies; new events
are rejected when the budget is full. Later clicks, publication or removal
reclaim replies.

**Port lifetime:** keep the port valid until `RemoveAppIcon` or
`WorkbenchControlA(NULL, {WBCTRLA_DetachAppIcon, icon})` returns, then drain/reply
pending messages before `DeleteMsgPort`. Detach is owner-only and irreversible;
it leaves the cached icon registered for a supervisor to remove later.
`task.resource` notifies Workbench before task teardown: under the same mutex
used by click dispatch, the hook clears both owner and port. Reusing a Task
address cannot reactivate that registration. This protects abrupt task exit,
but freeing a live task's port without detach violates the lifetime contract.
Tiles are refused if lifecycle notifications are unavailable. Applications
that exit without replying may consume the bounded in-flight budget; the
desktop never frees messages that a receiver might still hold.

The desktop never calls owner render hooks or reads original bitmaps later.
Freshness/cadence use monotonic `ReadEClock` through a retained per-tile
`timer.device` open, so wall-clock changes cannot revive a frame.

`WBCTRLA_QueryAppIcon` takes an owner-only `AppIconQuery` and returns
`APPICONQF_VISIBLE` when the presenter reports uncovered surface pixels in the
visible viewport on the front screen. A missing report or one older than one
second returns hidden. `AMCLASSICON_Visibility` carries the new boolean in
`am_MouseX`; reply as for region messages. Visibility is sampled by the existing
250-ms desktop timer, so it is not an instantaneous occlusion lock. A service
must query again before drawing, and retry from its coalesced timer if the
bounded notification budget was exhausted.

Accepted frames enqueue at most one asynchronous `WBHM_TYPE_TILES` refresh
until the desktop replies. This wakes the presenter without waiting for its
250-ms tick. A stalled desktop therefore cannot grow the refresh queue without
bound. Heartbeats on an already fresh image do not request a redraw. A heartbeat
that restores an expired cached image does request one presentation refresh.

A stopped owner leaves only library-owned data. Wanderer expires the cached
surface 4.25 seconds after the last successful frame or heartbeat and refreshes on its 250-ms timer, reserving two
refresh intervals plus drawing time for the five-second fallback deadline. This is a scheduling
bound to verify in the GUI, not a real-time guarantee during a blocked desktop.
No task pointer is dereferenced to infer liveness. Orderly `RemoveAppIcon`
removes the icon; abrupt exit or stalled publishing leaves the static fallback.
The fallback and tile allocation persist until explicit removal. Tile render
hooks and propagation into the original DiskObject are disabled to preserve
this ownership contract.

## Verification

Run `python3 developer/debug/test/workbench/tiles_host/run.py`. It compiles the
actual `tile.c`, state helpers and Wanderer tile adapter with host OS/graphics
fixtures under AddressSanitizer and UndefinedBehaviorSanitizer. It checks cell
bounds, malformed/duplicate names, rectangle boundaries, independent copies,
allocation-failure rollback, owner identity, cadence, immediate region delivery and explicit/task-exit detachment,
invalid-size updates, stale rendering, millisecond rollover, withheld-reply
bounds and removal.
Graphics pixels, MUI event handling and real kernel messaging remain fixtures.

Build `test-workbench`, `workbench-libs-workbench` and
`workbench-system-wanderer-app` sequentially from the same checkout and SDK. Run
`Tests/workbench/appicon_tile`: a 2x2 tile updates for 20 seconds, with `left`
and `right` regions. The probe then stops publishing for six seconds for the
fallback check. `appicon_tile invalid` must refuse its mismatched surface.
`appicon_tile exit` uses a registering child task that frees its source bitmap,
fallback and port and then exits. The supervisor confirms that task has gone,
keeps the registry handle for six seconds, and removes the orphan afterwards.
This separately exercises owner exit without leaving an orphan permanently.
The probe logs observations but never prints an automatic TILE pass.

## Remaining acceptance work

The presenter supplies multi-cell image bounds, draws/clicks through a virtual
IconList method, and packs mixed layouts with exact cell reservations: ordinary
icons use 1x1, tiles use their declared width/height. The base pitch is at least
64 pixels and expands to fit ordinary icon labels/images. All placements share
the existing spacing anchor. Supplied positions snap to cells; intersecting
reservations relocate to the next free cells. Horizontal/vertical wrapping and
Clean Up share the same pure placement solver. All-normal layouts keep the old
path. Tile source bitmaps remain 64 pixels per declared cell and are centered
inside the reserved width when the common pitch is larger. Labels consume space
inside reservations. Actual display-scale-aware surface scaling remains future
work.

The host suite runs 80 mixed 40-entry packing cases, pinned collisions, narrow
viewports, bounds, snap rounding, restored cell coordinates and Clean Up span
preservation. Drag-drop now invokes this solver before final position
propagation. The actual guest probe confirms the 2x2 mixed-grid geometry and drag snapping.
Snapshot file persistence, reboot restoration and broader size/label cases
remain unverified.
Workbook, display scaling, `.info` SIZE fallback, template centering, window
fold/unfold integration, covered-tile suppression, the shared two-redraw budget
and power policies are not implemented here. These remain TILE1/TILE2/TILE5
requirements; this foundation must not be promoted as those gates passing.
