# Shared indicator presenter

`Indicator` supplies a commodity, a desktop AppIcon, a Zune panel and an ARexx
command endpoint for battery, network, Bluetooth and sound providers.

```text
Run SYS:System/Indicator battery SYS:Tiles/Control/Battery "lcommand b"
```

For an explicit per-user start with the Control defaults installed by
`TilesControl` (HOME must already be assigned):

```text
SYS:System/TilesControl
Run >NIL: SYS:System/BatterySource
Run >NIL: SYS:System/SoundSource
Run >NIL: SYS:System/Indicator battery HOME:Tiles/Control/battery "lcommand b"
Run >NIL: SYS:System/Indicator network HOME:Tiles/Control/network "lcommand n"
Run >NIL: SYS:System/Indicator sound HOME:Tiles/Control/sound "lcommand s"
```

BatterySource is built for the Apple target. The network presenter starts its
own existing chooser worker; SoundSource remains unavailable without a configured
A2DP stream. These are explicit commands, not an installed boot script.
TilesControl preserves existing user icons. The Control definitions use the
same public indicator ports and GET/SET commands as these desktop processes;
running a lifted Control group does not start the providers or presenters.
Persistent Bluetooth pairing still needs N3 before its indicator can be qualified.

The icon argument names an existing `.info` file without its extension. The
program registers one 1x1 AppIcon for its entire lifetime. The static image
belongs to that file. Fresh provider events publish a copied 64x64 surface;
the UI generates at most one frame per 500 ms. The initial surface is an
unavailable symbol. The shared glyph demonstrates a gauge and a plug.

The port `INDICATOR.battery` (or `.network`, `.bluetooth`, `.sound`) accepts:

| Command | Result |
|---|---|
| `UPDATE 75 1 Charging` | A provider event: percentage 0..100, adapter flag 0 or 1, printable single-line text up to 191 bytes |
| `GET` | The healthy provider's text; error 10 after its four-second lease expires |
| `PANEL` | Toggle the panel |
| `HIDE` | Close the panel |
| `SETTINGS` | Asynchronously request `SHOW PAGE battery` from `SETTINGS` |
| `COMMANDS` | List user commands for a palette consumer |
| `QUIT` | Request shutdown |

The caller owns its ARexx message until the reply. Malformed updates preserve
the previous state and timestamp. A sender should push the latest snapshot
every two seconds, and push changes immediately. No provider is called on the
UI thread. Thirty-two commands are consumed per event-loop turn to bound input
work; a remaining queue renews its signal. Settings requests use the shared
asynchronous request engine from `Tiles/source.c`. A missing or failed settings
endpoint is reported in the panel. An unanswered request retains its reply
port and the task until the receiver returns ownership, including after QUIT.
The window remains responsive during that retention.

AppIcon activation and the commodity hotkey open the borderless panel. Zune's
close key, window deactivation and a second glyph activation close it. The
panel uses `QueryAppIconGeometry` for the complete glyph rectangle on its
Workbench screen, then clamps its real window dimensions to that screen. Missing
or stale geometry leaves the panel closed. A click that deactivates the panel
on its own glyph is consumed as closure when the later region message arrives.
The title-bar presenter, Control group and lift belong to separate work.

Provider lease expiry calls `SuspendAppIcon`, retaining the registration and
port while selecting the static `.info` image. A fresh valid surface resumes the
same registration. Suspension is asynchronous and never waits for the desktop.

The shared presenter includes battery subscriptions/CPU consumers, Wi-Fi worker
controls and sound slider/Mute with an A2DP controller adapter. A palette can
discover its COMMANDS endpoint; the registry itself and Settings pages are
separate components. Persistent Bluetooth pairing, RF control, physical audio
routing, the HIDD power facade and complete WM15/WM16 still require their
prerequisites and qualification. Provider-specific evidence is described below.

```sh
python3 developer/debug/test/workbench/indicators_host/run.py --output /tmp/indicator-proof-NEW
```

The settings endpoint can be selected with the global ENV variable
`Indicators/settings/port`. Its default is `SETTINGS`. The command remains
`SHOW PAGE <indicator-name>`; a missing future settings application leaves the
indicator operational. `STATS` reports health, panel state and accepted publish
count for verification. The presenter requires the published Tiles geometry and reversible-suspension
API.

The retained UP TCG smoke scenario covers real keyboard/pointer activation,
Escape/outside/second-click closure, provider expiry and recovery, and an
unresponsive Settings endpoint. Its exact scope and failed attempts are in
`developer/debug/test/workbench/indicators_vm/RESULT-2026-10-15.md`.

## Apple battery adapter

```text
Run SYS:System/BatterySource
```

`BatterySource` reads `AROS_APPLE_KATTR_BATTERY` using its bounded consistent-copy
reader every250ms. It never calls SMC. Charge, adapter/charging text and a
capacity/current-based discharge estimate are pushed on changes, with a
maximum two-second heartbeat while valid. Zero current and missing fields
produce an unavailable estimate; hardware samples older than ten seconds stop
renewing the UI's independent four-second lease.

The public port `INDICATOR.SOURCE.battery` supports:

| Command | Result |
|---|---|
| `GET` | Current full `percent adapter summary` payload, error10 if absent/stale |
| `SUBSCRIBE port` | Register/idempotently renew a port name, at most8 clients |
| `UNSUBSCRIBE port` | Stop new events; retain any borrowed message until its reply |
| `STATUS` | Availability, event enable flag and active subscriber count |
| `EVENTS off` / `EVENTS on` | Disable/re-enable new battery event sends; cached GET remains independent |
| `QUIT` | Stop and drain borrowed messages before removing ports |

The battery indicator subscribes asynchronously, retries missing/stale service
registration every two seconds and unsubscribes during shutdown. It never polls
GET for frames. Each subscriber has one stable outgoing ownership slot. An
inactive slot with an outstanding/expired message cannot be reused for another
port. Changes coalesce while borrowed; dirty failures retry no faster than250ms.
Names contain at most63 ASCII letters/digits or `.`, `_`, `-`. There are no
implicit subscribers. Eight consecutive failed sends retire a disappeared
subscriber (250ms retry floor); a successful send or new SUBSCRIBE resets the
failure count. Retired slots still cannot be reused while a message is borrowed.
CPU updates retain a separate ownership slot
and lease. EVENTS off does not revoke already borrowed messages or disable CPU
samples; the real cable-change-without-events negative gate still needs hardware.

The sole kernel producer defaults to5000ms. Its `/chosen` decimal property
`aros,smc-period-ms` accepts250..5000ms (invalid/missing→5000), independently of
the NIC's five-second poll. Candidate1000ms enables latency work without a
second SMC reader. This is not proof of the PWR6 two-second cable-to-gauge bound:
SMC duration, scheduling and ten physical cable cycles must still be measured.
The five-second upper bound leaves headroom below the ten-second cache lease;
a genuinely stalled producer still becomes unavailable.
No physical battery/charger or HIDD facade qualification is claimed.

## Typed Control commands

| Indicator | GET | SET |
|---|---|---|
| battery | `GET PERCENT` returns 0..100 | Read only |
| network, bluetooth | `GET RADIO` returns `on` or `off` | `SET RADIO on` or `SET RADIO off` |
| sound | `GET LEVEL` returns 0..100 | `SET LEVEL <0..100>` |

`GET` alone remains the panel summary. Typed GET returns ARexx error 10 when
its value is unavailable, stale or unsupported. A healthy summary without a
reported radio/level is also unavailable to typed GET. It never becomes a
synthetic `off` or `0`.

Provider pushes are `UPDATE <percent> <adapter> <summary>` for battery,
`UPDATE RADIO on|off <summary>` for network/Bluetooth, and
`UPDATE LEVEL <0..100> <summary>` for sound. Network/Bluetooth/sound providers
may send `UPDATE STATUS <summary>` while the numeric/radio capability is absent;
the glyph then uses an unknown symbol. Invalid events leave state unchanged.

SET forwards asynchronously to `INDICATOR.SOURCE.<name>` (override with
`ENV:Indicators/<name>/port`). A successful source response must contain a full
confirmed provider payload without `UPDATE`, such as `LEVEL 42 Speakers`.
The public caller's message remains borrowed until that confirmation or a
one-second source timeout. A missing source, invalid confirmation, timeout or
second in-flight request returns error 10. An expired underlying message stays
owned until its late reply; no new SET reuses that slot. A newer provider push
wins over an older SET response. SET does not optimistically alter the display.

To connect a service-managed `.info`, use e.g. `GET=INDICATOR.battery GET PERCENT`
or `SET=INDICATOR.sound SET LEVEL %v`. Set
`ENV:Indicators/<name>/tile` to the tile's exact token ID; value changes then
send bounded asynchronous `TILE CHANGED <id>` requests to `TILES`. Pending
notifications coalesce. With no tile ID configured the presenter has no service
notification dependency.

The real battery snapshot adapter supplies PERCENT. Hardware radio and audio
providers remain separate work; the command bridge does not create them. The
VM fixture uses explicit fake radio/audio providers solely to test protocol,
confirmation, timeout and stale-reply behavior.

Battery CPU consumers
---------------------

BatterySource also copies the three largest `TaskTag_CPUUsage` values through
`task.resource` every two seconds, on its background task. Enumeration is
bounded to 128 tasks and names to 31 characters. A truncated enumeration says
`CPU (partial)`; this is CPU accounting, not measured energy attribution.
The portable all-zero formatter says `Idle or accounting unavailable`; the
Apple source instead explicitly says `Per-task CPU accounting unavailable
(Apple kernel)` because its kernel has no per-task publisher yet. Enumeration
alone does not qualify CPU percentages. No task pointer survives the resource
lock; formatting and messages execute after unlock.

The battery panel receives `TOP <printable summary>` separately from UPDATE.
`GET TOP` returns that summary or `CPU data unavailable`. TOP has its own
four-second lease and cannot renew battery charge or charger state. It is
rejected by the other indicator kinds. Battery subscriber and CPU outgoing messages
use separate bounded ownership slots, all retained through late replies and
shutdown. The hardware sampler still defaults to five seconds or
qualify the PWR6 two-second cable-event requirement.

Wi-Fi panel
------------

The network instance now embeds the existing chooser worker, not a second
Prefs program. Its scan, association, disassociation and Wireless.prefs handling
remain in `workbench/prefs/wifi/worker.c`. The main task starts it without a
startup wait, sends at most one shared message, keeps independent copied list
rows and remains responsive while the worker owns the message. Shutdown retains
the shared message, reply port and code until both its final reply and DOS child
exit notification have arrived. A stuck device operation can delay final
process termination; it cannot make the panel wait on the driver.

The panel offers scan, known networks first, security/signal labels, joined
network, selection into the network-name field, a secret password field,
Connect, Other network and Disconnect. Blank passwords reuse stored keys;
manual unknown open networks can clear Password required. Worker errors,
including a missing device/configuration, appear in the panel. Network settings
continues through the shared SETTINGS endpoint. `WIFI STATUS` reports worker
readiness/busy/wireless/dead flags. `WIFI SCAN` and `WIFI DISCONNECT` return
accepted only when the configured wireless worker can take the request; they
are asynchronous and are not promises that association has completed.

`WIFI CONNECT` submits the network and password currently entered in the panel,
using the same guarded action as its Connect button. It fails when the worker
is unavailable/busy or the network name is empty. Passwords are never command
arguments. `WIFI OTHER` clears both fields and opens the panel for entry. Both
actions are discoverable in COMMANDS, along with Scan and Disconnect, while the
network source lease is healthy. When that lease is absent or expires, the
Wi-Fi controls disappear from the panel and command discovery; the Settings
button remains. Wi-Fi actions then fail without touching the worker. A fresh
provider event restores the controls on the same AppIcon registration. WIFI
STATUS remains available as a diagnostic regardless of the source lease.

Association is published as `UPDATE LINK on Joined: <name>` or
`UPDATE LINK off Disconnected`. LINK has its own capability flag: it draws the
network glyph without claiming a readable RF radio state. GET RADIO continues
to fail unless a provider explicitly supplies RADIO. The radio control is
unavailable because bwfm's S2_OFFLINE only changes software online state and
flushes reads, without a firmware RF-off command. Physical scan/connect,
RF toggling and full WM15 are not qualified by the VM without hardware.

Review corrections
------------------

SET confirmation must contain the requested value; a valid different value
fails without updating state. A newer push still wins over a matching older
confirmation: Confirmed means the provider confirmed the operation, not that
no later event changed the displayed value. AppIcon activation uses region
messages only; drops/Open messages are replied without toggling. Glyph
second-click suppression correlates the copied IDCMP deactivation coordinates
and timestamp with the original mouse-down Commodity event (within50ms of event
creation). The press/release duration and UI queue delay do not set that budget.
Clicks delivered inside the panel are excluded; programmatic deactivation does
not suppress the next glyph click. Intuition can omit LEFTBUTTON in its deferred
deactivation message, so its Qualifier alone is insufficient. Commodity filter/sender/translate errors fail startup. Failed or
timed-out TILE CHANGED delivery is retained for retry, with a one-second retry
floor and borrowed-message ownership preserved. QUIT, Ctrl-C and CXCMD_KILL
retain ports/tasks while a provider holds an outstanding message; there is no
unsafe force-free path for a crashed borrower.

Sound panel and A2DP controller adapter
--------------------------------------

The sound panel has a volume slider and Mute. Widget changes are coalesced while
one control request is borrowed; state/glyphs update only from provider samples
or matching SET readback. Stale/unavailable volume disables the controls. The
last queued widget request expires with the source lease. Output selection
remains unavailable until a routing/enumeration provider is supplied.

`Run SYS:System/SoundSource` provides `INDICATOR.SOURCE.sound` through the
existing bta2dp.device shared controller. It never allocates an AHI AudioCtrl,
opens a PCM writer or reads a fabricated volume from ENV. QUERY must report a
configured stream before GETVOLUME can publish `UPDATE LEVEL n Bluetooth audio`.
No stream/device means unavailable, including legitimate level zero versus
absence. SET validates 0..100, writes SETVOLUME and reads back the same value
before replying with the full LEVEL payload. STATUS and QUIT support service
lifecycle inspection. Driver calls execute only on the source task; public UI
calls retain their existing asynchronous timeout/borrower quarantine.

Exchange Disable closes the panel and suspends the existing desktop AppIcon.
Pointer activation and PANEL cannot reopen it while disabled; source messages
still update the cache. Enable restores publication from fresh data and the
same registration. This avoids relying on mouse-correlation events while the
commodity's input filter is disabled.

This confirms the device controller parameter. SETVOLUME schedules the unit
worker; GETVOLUME immediately reads its shared logical level. These APIs do not
expose a completed DSP gain update or a matching physical AVRCP acknowledgement.
Do not treat a successful controller readback as a physical headset-volume proof.
Bluetooth streaming, AVRCP acknowledgement, native jack/speakers and output
routing remain independent unqualified gates.

Review36 qualification
----------------------

The current Apple kernel does not yet publish iet_CpuUsage. The source does not
claim that its zero placeholders are measured idle tasks; task21 owns the
separate producer work. Porting the aarch64-native sweep requires committed
per-task busy counters and a safe sampling context, not list locks in an FIQ.

Wi-Fi wake signals are configured before worker creation and freed only after
its exit notification. The standalone chooser retains its synchronous default.
UI groups are created/attached after ApplicationObject succeeds. Failed connect
sends zero their copied key. COMMANDS advertises SET RADIO only while an explicit
provider port exists; association LINK never creates RF capability.
