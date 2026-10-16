# Tiles hover and examples verification — 2026-10-15

Board task 12 follows the completed B2.2 service. Production is based on merged
`apple-silicon` commit `e9770f2a47`. Status: PASS for the scoped native, host and
three final actual guest runs. Intermediate passes do not cover later corrections.

## Behavior

Pointer delivery is optional. `WBAPPICONA_Pointer` enables enter, move and leave
messages with coordinates relative to the full tile surface. The library retains
at most one borrowed pointer message per tile and coalesces later positions;
it never mutates a message owned by the recipient. The owner-only presentation
query exposes the latest position and hover state. Late replies remain safe after
the registration has been removed.

`HOVER=FAST` requests a minimum 33-ms frame interval while hovered and visible.
The fast global slot is separate from the normal 500-ms service slot. Leaving,
covering or hiding a tile removes the exception; its own last-frame timestamp also
prevents a premature normal redraw. An unchanged pair of eyes does not animate
continuously. Existing visibility sampling is 250 ms, with stale visibility and
pointer state expiring after 1 second.

`System/TilesExamples` serializes real Eyes and WeatherOffline `.info` files.
Eyes follows the local pointer and returns to neutral on leaving. Weather accepts
a configurable ENV or asynchronous ARexx provider with `temperature|condition`
values. The supplied Rome example is fictional offline data; it does not fetch
weather. See `workbench/system/Tiles/EXAMPLES.md` for actual AROS commands.

## Review corrections

The ENV SET path now relies on its active file notification rather than also
performing an immediate duplicate read. Initial loading processes ready replies
and publishes ready tiles while subsequent icons load. It preserves the normal
budget: ten initial frames still need at least 4.5 seconds from first to last.
The weather GET response preserves both temperature and condition.

Independent review also identified a queued-click regression in the first ENV
optimization: two relative toggle clicks could invert the same cached value.
Commit `23dcdea2a8` retains the last successful toggle intent separately from the
displayed value until notification processing reads the authoritative ENV value.
Failed writes preserve the previous successful intent. A focused regression
check complements the actual weather notification test.

The actual extracted service functions pass the retained
`aros-build-tiles-hover/toggle-host-001/result.json` ASan/UBSan fixture: two queued
clicks, explicit SET followed by a click, failed writes and authoritative cache
refresh. Replacing the pending-intent expression with the old cached-only
expression fails the precise two-click assertion. Root verified all retained
source hashes and the mutation failure; this is host evidence, not injected guest
click evidence.

The retained failures `attempt-013/tcg-02` and `attempt-014/tcg-02` exposed test
ordering assumptions. A presenter can display the accepted frame before the
service resumes and records its own draw bookkeeping timestamp. The corrected
collector matches the exact accepted generation, requires display and bookkeeping
timestamps to follow acceptance, and accepts either ordering between those two.
It also requires a generation newer than the input's captured baseline. Wrong
generations, pre-acceptance timestamps and stale same-side frames remain rejected.
Neither failed run is counted as acceptance.

## Native and host evidence

`aros-build-tiles-hover` contains sequential dependency-aware builds of Tiles,
TilesExamples, Workbench, Wanderer including IconList, and the workbench probes.
The final changed-module builds and artifact hashes are recorded in
`native-verification.json`. Baseline warnings in dependencies and Wanderer are
retained; they are not represented as a warning-free whole-tree build.

`parser-render-proof/parser-render.json` retains ASan/UBSan results for the actual
parser and renderer: 39 parser negatives, 480 layout cases, 3,920 compact surfaces,
16,200 weather surfaces, 5,400 eyes surfaces and 10,000 malformed inputs. Root
verified that the retained production source hashes match the delivered units.

`host-library.log` records grid and lifetime regressions plus 10,000 coalesced
pointer moves, immutable borrowed messages, independent progress beside a stalled
recipient, the global fast slot, leave/cover/stale pacing and late replies after
destruction. These host checks do not establish native display timing.

## Actual guest verification

The immutable `aros-tiles-validation/attempt-014/tiles-esp` uses production
runtime `23dcdea2a8` and VM revision `4a96912aaa`. Tiles, TilesExamples and the
guest probe are byte-identical to the independent native build's artifacts.
Collector `086e5bff47` exercises fresh pointer input, exact front-buffer
generations and pupil pixels outside the cursor area, both normally and while
the weather source retains its request. It checks neutral gaze after leaving,
normal and fast cadence, covered intervals, one queued pointer message at an
undrained receiver, ENV SET with exactly one refresh, complete weather GET values
and actual fallback pixels.

Each accepted run keeps final serial/host logs, an existing empty
guest-errors/unimp trace, screenshot hashes, immutable input hashes, at least
three seconds of observation after completion, running status, stopped CPU
registers and an orderly QMP quit with exit 0. Root independently replays these
checks in `aros-build-tiles-hover/replay-hover.py`; each run retains its own
`independent-review.json`. VM commands and full artifacts are documented in
`codex-tiles-hover-vm:developer/debug/test/workbench/tiles_hover_vm/RESULT.md`.

| Final run | Frames (fast / normal) | Wait returns | Minimum fast spacing | Weather fallback age |
| --- | ---: | ---: | ---: | ---: |
| `attempt-014/tcg-04` | 24 (12 / 12) | 111 | 94 ms | 4,382 ms |
| `attempt-014/tcg-05` | 36 (24 / 12) | 147 | 42 ms | 4,505 ms |
| `attempt-014/tcg-06` | 36 (24 / 12) | 145 | 41 ms | 4,490 ms |

All three keep the stalled receiver at one outstanding pointer message and
suppress generated frames during covered intervals exceeding 2.5 seconds.
The requested 33-ms minimum is a rate limit, not a guaranteed rendering speed;
these measurements are from TCG. Wait returns are service counters, not kernel
per-task wake accounting.

## Scope

The actual guest lane uses UP Exec in QEMU TCG. It tests a held source request and
an undrained pointer port, not arbitrary memory corruption or SMP port retirement.
The existing source timeout retains foreign-owned messages and suppresses renewal;
late replies reclaim them. Shutdown can remain deferred indefinitely if a provider
never returns its message. Other tiles and the desktop continue running.

Apple hardware, live network weather, the generic PANEL resolver and the B6–B10
system indicators are separate work. The indicator task belongs to codex-extra.
