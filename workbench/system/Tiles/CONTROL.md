# Installing the Control drawer

Run `SYS:System/TilesControl` explicitly once to create `HOME:Tiles/Control`.
HOME must already be assigned. The utility creates the missing Tiles and Control
drawers, preserves existing files and icons, and stops on unreadable or conflicting
paths. A retry keeps completed entries and creates only missing icons. Run one
installer instance at a time; icon.library does not offer an exclusive-create API.

For image staging, run `SYS:System/TilesControl SYS:Tiles`. On subsequent per-user
installation the utility imports missing matching icons from `SYS:Tiles/Control`
using icon.library, preserving their ToolTypes and positions. It generates a default
only when that seed icon is absent. It does not recursively copy unrelated files.
Every `.info` is a genuine serialized DiskObject, including the Control drawer icon.

Start `Run SYS:System/Tiles HOME:Tiles/Control` once no other Tiles service owns the
TILES port. Control uses `TILE=group`, `SHOW=lifted`, `COLUMNS=2` and `LIFT=LAmiga C`.
Existing user customization is never deliberately overwritten.

The initial provisioned definitions use these typed indicator service contracts:

| Tile | GET | SET |
| --- | --- | --- |
| Network toggle | `INDICATOR.network GET RADIO` | `INDICATOR.network SET RADIO %v` |
| Bluetooth toggle | `INDICATOR.bluetooth GET RADIO` | `INDICATOR.bluetooth SET RADIO %v` |
| Sound slider | `INDICATOR.sound GET LEVEL` | `INDICATOR.sound SET LEVEL %v` |
| Battery gauge | `INDICATOR.battery GET PERCENT` | none |
| Brightness slider | `INDICATOR.brightness GET LEVEL` | `INDICATOR.brightness SET LEVEL %v` |
| Do-not-disturb toggle | `INDICATOR.notifications GET DND` | `INDICATOR.notifications SET DND %v` |
| Clock value | `INDICATOR.clock GET TIME` | none |

Typed providers were delivered separately in `9b72c5a6ef` with host and VM evidence;
they have not yet been integrated into this Control candidate. A missing provider leaves the static
icon; these definitions contain no fabricated state or sample ENV values. Toggle
responses must be on/off/1/0; slider/gauge responses are integer 0..100. Brightness and do-not-disturb definitions intentionally remain static/unavailable
until their real providers implement these contracts; no ENV toggle pretends to
control hardware or notification delivery. Clock is provided by
`Run SYS:System/TilesControlSource`, reading DOS DateStamp/DateToStr on each GET.
It follows the OS wall clock and refreshes at the tile's 60-second minimum. Stop
that provider with Ctrl-C (or the Shell Break command for its process); it removes
its public port and replies to queued requests before releasing resources. A second
instance refuses the already-owned port. Seven definitions are installed, but that
alone does not prove seven working controls or complete TILE3/TILE4 acceptance.

## Using the initial Control presenter

After installation, `Run SYS:System/Tiles` loads `HOME:Tiles/Control` by default,
falling back to `SYS:Tiles/Control` when the HOME drawer cannot be locked. An explicit
path avoids that fallback. The service does not install or rewrite defaults itself.
Only one instance may own the `TILES` ARexx port.

Press left Amiga+C (`LAmiga C`) to lift Control at the pointer. Left Amiga+D lifts
the registered desktop snapshot together with loaded lifted groups. Press the lift
key again, press Escape, or click outside the lifted tiles to drop the presenter.
The current implementation also dismisses on a non-left mouse-button press. A lift
request while already lifted drops the current presenter; it does not switch groups
in place. The intended behavior preserves the previously active window and its
keyboard focus. The presenter uses a nonactivating toolbox window and cached tile
bitmaps over a screen snapshot dimmed to 30% brightness.

Commands use ARexx messages addressed to port `TILES`; they are not shell commands.
For example, save this as an ARexx script and run it with `RX script-name`:

```rexx
/* Lift the loaded Control group */
ADDRESS TILES 'TILE LIFT Control'
```

Other command strings are `TILE LIFT` (global lift), `TILE DROP` (idempotent drop),
`TILE STATS`, and `TILE QUIT`. Group names are case-sensitive drawer basenames and
must already be loaded. `TILE LIFT Control` toggles when a presenter is already open,
just like the hotkey. `TILE QUIT` waits for outstanding external replies: it retains
normal service activity rather than abandoning borrowed messages. A provider that
never replies can therefore defer shutdown indefinitely.

Lifting and dropping use existing caches, without waiting for a source or calling
the tile renderer. A click on a healthy toggle/slider sends its configured SET;
source replies or ENV notifications then update the value through normal scheduling.
A cache update repaints that tile and later overlapping tiles without rewriting the
dimmed background. Missing or invalid source values use the static fallback. The
held-source path retains bounded requests so a slow provider need not block other
controls. The initial Control path passes two strict UP TCG runs in attempt021, including
actual foreign-owner clicks, source-held interaction and focus/pixel checks. See
`developer/debug/test/workbench/tiles_group_host/RESULT-CONTROL-2026-10-16.md`.

## Current limits and verification scope

This service accepts group drawers only with `SHOW=lifted`. `SHOW=always` and
`SHOW=edge` are rejected by the current presenter, despite their parser/layout
support. Creating groups by dragging tiles together, dragging members back to
left-out entries, live drawer-position synchronization and screen-mode anchor
validation remain unfinished. Global desktop snapshots and foreign tile interaction pass the scoped attempt021
checks. Attempt020 remains a retained failure caused by inconsistent builds of
Workbench and Wanderer after their shared private structure changed. The packager
now rejects stale consumers before creating an image.

The non-compositor presenter periodically restores its front position; this is not
a guarantee that a newly opened foreign window is never briefly above it. Complete
TILE4 timing, layer-order and focus acceptance needs its own final evidence. The
seven provisioned definitions and this initial lifted-group interface do not establish
full TILE3/TILE4 or the seven-item default Control gate.
