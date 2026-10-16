# Declarative tile service

`Tiles` is one resident task for up to forty `.info` files. It uses copied
AppIcon surfaces, one coalesced timer, asynchronous ARexx requests and ENV
notifications. The production executable has no Zune dependency.

```sh
Run SYS:System/Tiles HOME:Tiles/wifi.info HOME:Tiles/volume.info
```

The basename without `.info` is the desktop tile ID; group members use
`drawer/member`. Duplicate canonical definition paths are ignored. A
second service instance is refused while the public `TILES` port exists.
Add `TRACE` to record source changes, generated frames and service counters on
the kernel debug transport; normal operation does not emit those diagnostics.

## ToolTypes and sources

A Wi-Fi-style toggle may contain:

```text
TILE=toggle
SIZE=1x1
GLYPH=wifi
LABEL=Wi-Fi
GET=NETWORK RADIO
SET=NETWORK RADIO %v
EVERY=60
PANEL=network
```

Supported templates are `toggle`, `gauge`, `slider`, `value`, `glyph`, `eyes` and `weather`.
Toggle values are `on`, `off`, `1` or `0`; numeric values are integers 0..100;
text values must be nonempty, bounded and printable. `SIZE` accepts dimensions
1..4; invalid sizes fall back to 1x1 and are logged. Rendering uses the current
RastPort font and pens 0..3, with compact centered layouts. `wifi` and
`sound`/`volume` have resident geometric glyphs; other glyph names use a neutral
symbol. The stateless drawing module can be reused by another presenter.

`GET=<port> <command>` sends an asynchronous ARexx command requesting a result.
`SET` has exactly one `%v` substitution and no other percent expansions. Clicks
on toggles send the opposite state; slider clicks map the horizontal position
to 0..100. A successful SET is followed by GET, so the source remains authoritative.
An external source sends `TILE CHANGED <id>` to the `TILES` ARexx port when its
value changes. Repeated changes during a pending request coalesce into one
follow-up GET. `EVERY` is floored at 60 seconds and is used only for port sources.

Alternatively, `ENV=<relative-name>` uses `GetVar` and `StartNotify` without
polling. Writes use the same global ENV variable. Oversized or binary/control
values are rejected rather than silently truncated. Exactly one GET or ENV
source is required except for built-in `eyes`, which rejects external sources.
An ENV SET relies on its active notification subscription for one coalesced
refresh; it does not also reread the value synchronously. `PANEL` is retained, but the shared indicator-panel resolver
is not available: `TILE PANEL <id>` reports an unsupported action. No arbitrary
command or invented Prefs mapping is executed. OPEN, settings/service sources,
and title-bar presentation are separate work. Drawer groups are described below.

Malformed GET or an unknown template leaves the `.info` image as an ordinary
AppIcon and logs one diagnostic line. One invalid tile does not stop the rest.

## Budget and source isolation

Normal generated frames share one global slot every 500 ms. The first frame
at registration consumes a slot too. Unchanged typed values only renew health;
they do not regenerate a bitmap. When the slot is free, source notifications
request immediate drawing; otherwise the latest value waits for the next slot.
The 100-ms notification target applies only to a free slot. Ready sources may
publish between loading icons, but initialization receives no burst exception:
ten initial frames still require at least 4.5 seconds between first and last.

`HOVER=ON` opts into local pointer enter/move/leave with normal frame pacing.
`HOVER=FAST` additionally permits a 33-ms global fast slot while the tile is
hovered, visibly presented and healthy. The fast and normal budgets have
separate clocks, so interactive eyes do not starve ordinary tiles. Leaving or
covering a tile restores normal pacing; each tile also respects its last frame
clock. Pointer messages wake the service, which reads authoritative coalesced
coordinates from the owner query and immediately replies. Unchanged eyes do
not generate frames or arm a continuous animation timer.

`TILE=eyes` uses a local pointer source. `TILE=weather` accepts a signed Celsius
integer followed by `|` and sunny/cloudy/rain/snow/storm/fog, for example
`18|cloudy`; LABEL supplies the location. GET/ENV select the source, so weather
can use a provider without putting network waits in the resident service.
`TilesExamples` creates genuine Eyes and WeatherOffline `.info` examples.
The weather example is explicitly offline; see [EXAMPLES.md](EXAMPLES.md).

Before generating an update, the service queries actual presenter visibility.
It generates no steady-state updates for a fully covered/offscreen tile; a
visibility-change message wakes it when the tile becomes visible. Initial
registration requires one frame before the presenter can report visibility,
so initialization is an explicit exception to the steady-covered rule.

Healthy cached images receive a NULL-surface heartbeat roughly every two
seconds. Frame cadence and heartbeat freshness are separate. Heartbeat and
publication both stop while a GET/SET validation is outstanding. The request
times out after one second, and the cached `.info` fallback returns within the
five-second presenter deadline measured from the request/control event.
A source disappearing silently between 60-second polls is not detectable until
its next request or notification; the service does not claim otherwise.

At most one request per tile and forty requests total may be outstanding.
Timeout never frees a message another task may still hold. Late replies reclaim
storage without overwriting the UI with expired data. A timed-out request
occupies only its owner's slot; the other thirty-nine owners retain capacity.
The main loop never waits synchronously for a source. Exit during a request is
handled like a timeout. This is process/source isolation under the normal Exec
message ownership contract, not protection against kernel corruption or a
broken graphics driver.

`TILE QUIT` or Ctrl-C requests shutdown. While foreign tasks retain messages,
the service immediately removes its presentations and stops new work. It keeps
collecting late replies, and retains notification/reply ports until cancellation
and borrowed-message reclamation finish. A provider that never replies can keep
this small retiring task alive; it cannot keep the desktop or other services
waiting. The service never deletes a port still referenced by another task.
The conventional Forbid/FindPort/PutMsg named-port lifetime contract is exercised
on UP; this service does not claim cross-CPU source-port retirement safety.

## Drawer groups and lifting

Running `Tiles` without definition arguments discovers `HOME:Tiles/` and installs
missing Control defaults there. `TilesControl [root drawer]` can also provision
explicitly; existing user `.info` files are preserved. Control contains network,
Bluetooth, sound, battery, brightness, Do Not Disturb and clock definitions.
`TilesControlSource` serves the clock from DOS system time. The other definitions
use their named indicator providers; missing providers retain static icons.
Installing a definition does not supply a hardware driver or invent its state.

A group is an ordinary drawer whose own `.info` has `TILE=group`. Its children
are the member `.info` files. Optional ToolTypes are `SHOW=always|lifted|edge`,
`ANCHOR=free|top|bottom|left|right`, `COLUMNS=1..32`, `TITLE` and `LIFT`.
Defaults are `always`, `free` and four columns. Only Control defaults to
`LIFT=LAmiga C`; `LIFT=none` disables a group shortcut. A malformed individual
shortcut is logged and disabled without disabling the global or other shortcuts.
An ordinary drawer without `TILE=group` creates no group. Nested groups are refused.

Dragging a declarative tile onto another creates a group under `HOME:Tiles/`.
Double-click the group to open its drawer. Moving a member in that drawer stores
its position in its `.info` and updates the group's layout. Dragging a member out
to the desktop moves its definition back under `HOME:Tiles/` and records a left-out
entry. A drawer dropped into a group is refused. Copying a group drawer and its
`.info` to another HOME preserves its definitions and layout; referenced source
providers must also be available there.

Wanderer performs these filesystem transactions in a separate worker with a
bounded rollback journal. The UI never waits for file operations during normal
interaction. Wanderer joins its own worker on exit before unloading executable
code. A failed rollback keeps recoverable staged files and logs their paths.
The Tiles service reconciles directory notifications while retaining any source
request storage still borrowed by another process.

`SHOW=always` draws one aggregate desktop tile. Edge-anchored groups follow the
screen dimensions; `SHOW=edge` reveals a small nonactivating panel when the
pointer reaches its anchored edge and hides it when the pointer leaves.
The screen-close notification releases presentations before acknowledging the
close; reopening rebuilds them from the saved definitions and new geometry.

LAmiga D lifts cached desktop tiles and groups; an always-visible group keeps its
actual desktop position, while a lifted group appears at the pointer. A group's
own shortcut lifts that group alone. Escape, the same shortcut, or an outside
click dismisses the lift. The overlay dims the background to 30 percent and
uses a protected layer above newly opened ordinary windows without activating
itself. Lift/drop copies cached pixels and does not render tile content.
An opted-in fast-hover member retains its 33-ms pacing while lifted.

## Commands and verification

The `TILES` ARexx port accepts `TILE CHANGED <id>`, `TILE GET <id>`,
`TILE SET <id> <value>`, `TILE LIFT [group]`, `TILE DROP`, `TILE STATS`
and `TILE QUIT`. STATS reports generated
frame count and the number of service `Wait` returns. These wake measurements
are application counters, not kernel per-task wake accounting.

Build `workbench-system-tiles`, then the affected Workbench/Wanderer modules and
`test-workbench` sequentially. Host checks:

```sh
python3 developer/debug/test/workbench/tiles_service_host/run.py --output /tmp/tiles-parser-render-01
python3 developer/debug/test/workbench/tiles_source_host/run.py
python3 developer/debug/test/workbench/tiles_host/run.py
```

Choose a fresh output directory for each parser/renderer run; existing results
are never overwritten. The parser/renderer suite exercises the real modules
under ASan/UBSan. The
source suite exercises the real request module with message ownership fixtures:
silent/late responses, healthy progress beside 39 silent sources, bounded
allocation, clock rollover and SET substitution. The actual guest fixture
`tiles_service_probe` creates ten configured tiles and one malformed control,
then exercises source silence, late replies, process exit during a request,
ENV notifications and coverage. External real pointer input is required; a
fixture completion marker alone is not acceptance. The isolated
`codex-tiles-service-vm` branch owns the strict QMP/pixel/timing collector.

The `tiles_hover_probe` fixture adds FAST/ON eyes, port and ENV weather, a
blocked weather source and a pointer recipient that deliberately does not drain
its port. The `codex-tiles-hover-vm` collector owns actual motion, pupil/weather
pixel comparisons, leave/coverage cadence checks and bounded message evidence.
Its result report distinguishes completed guest proofs from host tests.
