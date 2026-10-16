# Task 18 group and lift evidence — 2026-10-16

Review40 follow-up: runtime `6633055968` fixes asynchronous clock stream ownership
and multi-selection position persistence. [Correction report](RESULT-REVIEW40-2026-10-16.md)
records native/host checks and actual groups034/Control035, both independently replayed PASS.
The original acceptance evidence below is retained unchanged.

**Accepted evidence: runtime `0236a083cb`; native/host checks, groups 031 and Control 032 PASS, with independent guest replays.**
The prior approved stack is merged at `e6559ed4ae`; task 18 evidence is complete for the scoped generic UP TCG non-compositor lane.
This report does not supersede the historical task 17 Control result. Source was
still changing during these checks; the retained hashes identify what each run
actually exercised. A directory named `final` is not evidence of a frozen release.

Production worktree: `aros-core-tiles-indicator-api`, branch `codex-tiles-groups`.
Runtime checkpoint: `0236a083cb` (later documentation commits do not change binaries).
Native output root: `/Users/jknipper/Source/aros-personal/aros-build-tiles-indicator-api`.
Relative evidence paths below resolve against that output root; `/tmp` paths are
local retained review artifacts and are not portable repository assets.

## Host evidence

The listed JSON artifacts were inspected and each reports PASS. SHA-256 below is
of the result artifact itself; its contents retain source hashes and commands.
These are ASan/UBSan host models of production code, not actual AROS concurrency
or graphical output. Source extraction runners read production bodies afresh.

| Check | Retained result | Result JSON SHA-256 |
| --- | --- | --- |
| Group parser/layout | `group-host-002/result.json` | `0fdf0de41b50447769c251df94bb244dea9af41160f3f19f6d4ffe38116cfc16` |
| Backend/grid/snapshot | `groups-all-host-02/result.json` | `6efd04e51b0dad60be9475bde4b81fab9242ddf3286b0b7815c3048df63b394a` |
| Drawer enumeration/capacity | `groups-loader-host-01/result.json` | `b46bcf1d324e9954ae8a780b5eff7a47d6f9620ea9a55ff380f7f392cc66d581` |
| Reload/pending owner/screen cancellation | `groups-final-reload-host-01/result.json` | `e43b86e16f082f00b1d45d025e90cb3d93ad61ad69fd6b4bafe73a895c1d1475` |
| Cached lift/panel geometry | `groups-final-lift-host-01/result.json` | `8cc7adc50e67d8142023a75cc0a77b9e7c33a6fef1e0fd0092187e97290fac17` |
| Seven-default preservation | `groups-final-defaults-host-01/result.json` | `3fa408c9993e469bf55071fca2444c7821cb280620bc8d6b2870d21835b66b4d` |
| Parser/render | `groups-final-render-host-01/parser-render.json` | `6a6abe3e5863ecfbeada8346e850caefa3180bc8514395cf34c74079caa4c604` |
| Transaction rollback/worker lifetime | `transaction-host-006/result.json` | `25795ff0843b05397c3b6429b9a6d7ec8f352af65226399d33acce2ac7ad60a8` |
| Protected layer priority | `/tmp/tile-overlay-host-02/result.json` | `deadc3e676efdbfdf89f6bef4b3e57d2a782a021bc5c583b6d51581e005ee877` |
| Malformed/disabled group keys and panel input | `/tmp/tiles-input-key-isolation-01/result.json` | `4d80893cf788c468fa2978c584bf480e8c49a48052070c3a86e8a41a5495715d` |
| Front-screen panel/modal routing | `/tmp/tiles-front-panels-01/result.json` | `fd09eea112cf75f5e1a3ba7f2f55aa78f5c172032b816b803669fccb7cb9b7f0` |
| Owned lift caches/deferred publication/list-view transition | `lift-cache-host-004/result.json` | `535373a53c424e7b236357bf0d877a5396b1dc11f54cc1c53db17954c6faf38e` |
| Left-out suppression/restoration | `backdrop-host-001/result.json` | `4878788e8c559619b70e43014f958ede712c834629f6b03dc08e8434fbc38051` |
| Backend suites after left-out reconciliation | `backdrop-all-host-001/result.json` | `b2a588d5f5d2857ebdfe302ba151fd1b79ee04c3246fd1f7bfe513160ae534a9` |

Reload tests require old AppMessages to be replied before reused IDs, retain
pending source owner slots across changed definitions, and release screen-bound
resources before unlock. The screen-cancellation case models EndScreenNotify
returning FALSE with a queued WAIT_REPLY, then succeeds after reply without
losing the handle or freeing the port. Four deliberate regressions are rejected.
The cache fixture rejects deferred-publication rerendering, publication while a
source still holds a request, foreign snapshot copying at the fast cadence and
stale grid slots after switching to list view. Each successful render is charged
to its budget even when the desktop copy is deferred.
The front-screen fixture rejects three guard-removal mutations. The transaction
fixture rejects removed rollback and premature busy-only worker shutdown.

The protected-layer fixture executes priority selection and actual ordering
functions with modeled layer links. It does not execute full clip recomputation.
The panel and renderer fixtures record drawing operations rather than guest
pixels. Input fixtures test coherent snapshot refusal and bounded queues, not
simultaneous SMP execution. Installer mocks prove no-overwrite/error handling;
actual seven-icon and clock-provider guest round trips also pass in Control 032.

## Native compilation observed

The following retained logs reach their stated module/link milestone. This is
inspection of logged build output, not an independently captured process-exit
manifest and not a promise that these binaries include later edits.

| Log | Observed milestone |
| --- | --- |
| `groups-includes-01.log` | SDK header/FD generation |
| `groups-layers-02.log` | layers.library and liblayers.a built |
| `groups-intuition-02.log` | intuition.library and libintuition.a built |
| `groups-workbench-02.log` | workbench.library and libworkbench.a built |
| `groups-wanderer-01.log`, `groups-wanderer-02.log` | Wanderer linked, including transaction/drop sources |
| `groups-service-01.log` through `groups-service-03.log` | Tiles links; third log compiles main.c and input.c |
| `groups-service-04.log` | Incremental invocation completed without a new link line |
| `groups-install-01.log` | TilesControl linked |
| `groups-clock-01.log` | TilesControlSource linked |
| `groups-probe-02.log` | Corrected `test-workbench` target completed exit 0 (root session 78118), including the seven-default/clock probe |

Retained unsuccessful invocations are not hidden: `groups-layers-01.log` used
unknown target `rom-hyperlayers` and ended Error 20; `groups-probe-01.log` used
unknown target `test-workbench-appicon-control-probe` and ended Error 20. They
are wrong-goal failures, not compiler failures. The corrected full
`test-workbench` target subsequently completed successfully; root reported
session 78118 exit 0 and the retained `groups-probe-02.log` contains the final
probe compile/link. Final runtime `5e8fdaf2b1` was then rebuilt successfully in root session75086:
`groups-service-final-01.log` compiles/links Tiles and
`groups-wanderer-final-01.log` compiles/relinks IconList.mui; Wanderer itself was
already current from its earlier successful build. The sequential invocation
exited0. [NATIVE-GROUPS-2026-10-16.json](NATIVE-GROUPS-2026-10-16.json) retains
exact final binary/log hashes and the passing shared-WBTile freshness guard.

Warnings remain visible in the logs: generated make targets override recipes;
Intuition addglist has an empty-body warning; setiprefs and MUI frame diagnostics
have format-width warnings; dynmodule_glue and MUI area have return-path warnings;
Wanderer reports a nonliteral null conversion. The earlier Tiles service log has
a STRPTR/char-pointer mismatch at load_tile; the later service compile should be
used to assess its correction. This report does not call the build warning-free.

The actual extraction run exposed duplicate live and static left-out icons.
Commit `310d0d3f49` reconciles registered canonical definitions with the backdrop
list without deleting the stored left-out entry. Host tests cover registration,
fallback and removal/restoration; disabling suppression fails the negative
control. Native Wanderer was rebuilt in session30916 (exit0), with updated
appobjects and volumelist objects in `groups-wanderer-final-02.log`. The manifest
now records this runtime and updated executable.

## Acceptance and scope

Groups `attempt-031/tcg02` and Control `attempt-032/tcg-01` both pass with
independent `root-replay.json` results. Note the different directory spelling:
groups uses `tcg02`, Control uses `tcg-01`. Both use runtime `0236a083cb`.
Their immutable manifests, raw logs, QMP transcripts, captures and empty traces
are retained. Native artifact hashes are in `NATIVE-GROUPS-2026-10-16.json`.

The group result covers real create/reposition/extract drags, exact left-out
registration and unique live representation, persisted positions and pixels,
cursor-independent edge reveal/hide, ordinary/nested negatives, copy fidelity,
actual mode change/pan/restoration and foreign-window ordering/focus. Control
covers seven definitions, real clock, cached toggle/dismiss/focus and held-source
behavior. These complementary runs establish TILE3/TILE4 for generic UP TCG
without a compositor; they do not claim physical hardware or compositor timing.

Brightness and DND definitions use explicit provider contracts but remain
unavailable until real providers exist. Seven installed definitions must not be
reported as seven functioning hardware controls. Clock is a real DateStamp/
DateToStr provider; the added probe checks bounded HH:MM:SS replies rather than
substituting fixture time.

## Complete Control: actual accepted evidence

The actual UP TCG replay `aros-tiles-validation/attempt-029/tcg-01/verdict.json`
is PASS, with the independent `root-replay.json` also PASS. This uses production
Wanderer `310d0d3f49`; it must not be relabeled as the later geometry-fix image.
The immutable detailed report is in the validation worktree at
`developer/debug/test/workbench/tiles_control_complete_vm/RESULT-2026-10-16.md`
(commit `ad856c62db`). Original task 17 evidence remains unchanged.

Seven genuine definitions and custom-icon preservation passed. Real clock
`00:00:02` replied in 27 ms; five lift openings took 13–19 ms and drops 6–19 ms.
Six focus keys, on/off toggles, external-owner cached pixels/click and the held
source interval 27393–35380 passed. Raw logs and the empty guest trace were checked
through the final observation; explicit QMP shutdown ended with exit 0. Root
independently checked input membership/hashes, screenshot hashes, dimming,
restored/toggled/foreign pixels, raw oracles and QMP lifecycle. Generated guest
`boot.log` is distinguished from immutable boot inputs.

| Attempt 029 evidence | SHA-256 |
| --- | --- |
| `verdict.json` | `fcd0730a0bbc5d8cdf017ad75bdd694ae1330b22b50f8361fd00db642fa120df` |
| `root-replay.json` | `f54800dac27a03104818b12e44dcbba213e89f1d1cacb6f1f6ed66488888f2a7` |
| `verified-manifest.json` | `5908ff2caa88b6110f647bc7338af86000cb498363783fa595aa768a5bdb15c8` |

## TILE3/TILE4 requirement audit

This maps the feature contract, including lifecycle and filesystem behavior,
rather than treating a list of green unit tests as feature completion.

| Requirement | Implementation and current evidence boundary |
| --- | --- |
| Drag one tile onto another creates HOME drawer and bounding group | Transactional `.info` movement and service reload exist; group guest attempts exercised creation and preserved member pixels, and final group verdict PASS. |
| Drag out restores desktop left-out entry | Transaction writes the actual volume `.backdrop`; duplicate static/live suppression is fixed in `310d0d3f49`. Probe now checks the unique entry and extracted desktop pixels; final group run PASS. |
| Drawer icon positions move group members | Persisted DiskObject coordinates feed normalized member layout. Collector now checks independent saved coordinates against fresh generation and exact pixels; final group run PASS. |
| Bottom anchor survives actual mode change | SetIPrefs to 1024×900 and bottom viewport pan exercised. Actual pixels revealed an origin discrepancy despite self-consistent logs; `0236a083cb` corrects this; final mode/pan/restored pixel checks PASS. |
| Edge reveal/hide | Nonactivating protected cached panel exists. Collector requires cursor footprint exclusion and original member pixels, not mere screenshot inequality; final group run PASS. |
| Copy to another HOME reproduces group | Probe copies genuine drawer/member metadata, reassigns HOME, and compares original Beta pixels. Final corrected geometry and original Beta pixel comparisons PASS. |
| Nested drawer refused; ordinary drawer not promoted | Transaction refusal and explicit TILE=group parsing exist; actual nested refusal observed. Both negatives are verified in the final group PASS. |
| Global lift includes desktop tiles and always groups at original position | Snapshot backend and cached group presenter implement this. Control 029 proves local/external desktop paths; final groups prove always-group position and exact pixels. |
| Lifted group at pointer; own group key isolates it | Control 029 logs first Control origin (700,400), matching injected pointer, with second member at (764,400). Group-key openings contain only the two Control members; global opening contains four entries including external owner and desktop tile. |
| Toggle, Esc, same key, outside click, focus continuity | Actual Control 029 passes all interaction phases and six focus keys; raw focus and pixel assertions independently replayed. |
| Dim to 30%; preserve caches; zero operation-triggered renders | Control 029 exact dim/restoration and begin/open draw counters pass. State changes while lifted may legitimately generate frames; this is not a prohibition on live updates. |
| Open/drop within 50 ms without compositor | All five Control 029 pairs pass. Final group pairs open in 27/15 ms and drop in 8/6 ms. No compositor one-frame proof is claimed. |
| Newly opened foreign windows remain below lift | Protected Intuition/layer priority path and host ordering checks exist. Actual inactive and activating foreign-window layers/pixels/focus are in the final groups sequence, now accepted in group 031. |

Screen close/reload cancellation, borrowed source ownership, transaction worker
exit acknowledgement and malformed per-group hotkey isolation have dedicated
host checks. They support safe implementation but do not substitute for the
complete graphical run. Hardware brightness/DND availability and
compositor rendering are explicitly outside the demonstrated environment.

## Geometry correction native checkpoint

Production `0236a083cb` corrects actual SampleTiles positioning: the sampler
previously reset the icon area to 64 pixels while the draw path expanded the
label area to 92, producing owner geometry x=5 but actual pixels x=19. This was
a runtime presentation defect, not just a trace discrepancy.

Root native session 96893 completed exit 0: `groups-service-geometry-01.log`
compiles/links Tiles and `groups-iconlist-geometry-01.log` compiles/builds the
dynamic IconList.mui. `NATIVE-GROUPS-2026-10-16.json` now records the refreshed
artifact hashes at this runtime, preserving earlier build/probe sessions.

The actual-source geometry fixture `group-geometry-host-002/result.json` reports
ASan/UBSan PASS and rejects both label-centering and anchor regression mutants.
Result artifact SHA-256: `4657b1480d1fbc3b1d0448e7faa9dc8298fb297790f0617933e35c9e58023e26`.
This supports the fix but does not by itself close the graphical gate. Groups
attempt 031 and final Control attempt 032 on matching bits both pass, including
independent replays as recorded below.

## Final geometry-runtime Control replay

`aros-tiles-validation/attempt-032/tcg-01/verdict.json` is PASS on runtime
`0236a083cb`. This is the same rebuilt runtime prepared for groups attempt 031.
The environments run serially to avoid competing QEMU timing workloads.

Seven installed definitions/customization and real clock `00:00:03` passed;
clock request 3391/reply 3417 took 26 ms. Five lifts took 15–29 ms, drops 28–37 ms;
six focus keys and on/off toggles passed. Held-source interval 27536–35531 covers
the final lift/drop, and all 10 commands completed. Final failure scans were clean,
guest_errors/unimp was empty, and explicit QMP exit returned 0 after the final
observation. Root independent replay also PASS: `root-replay.json` verifies raw oracle, exact
input membership/hashes, screenshots, dimming/toggle/foreign pixels and QMP
lifecycle.

This evidence is generic UP TCG with non-compositor fallback. It does not claim
hardware controls, compositor one-frame behavior, or behavior beyond the separate accepted group 031 scenario.

| Attempt 032 artifact | SHA-256 |
| --- | --- |
| `verdict.json` | `9e717ced1aad7a95978c2a78a788d8a4a76b561a32fd9e0c40dee8d9fdeacb14` |
| `verified-manifest.json` | `8442974199f1c5997e73d064f8e71391b1b43f192abb327e3305f74b6744b63a` |
| `serial.log` | `a56d84fe2effe19dc02ab9f36be10ee722672ff6ac2ac4cb50c4c3c08b1016cd` |
| `qmp.jsonl` | `86f3ec7a24cbf549a023de9adeaf7e2286bb2859af87423b87f5e80da9edcd72` |
| `root-replay.json` | `d83c45d896ef103b09b632491bf3ee93582d7ebd4d92177c69a7bd8de3f83f41` |

## Final groups: complete actual and independent PASS

Artifacts: `aros-tiles-validation/attempt-031/tcg02/`. Runtime `0236a083cb`,
collector `41f05e` at successful execution. All 15 phases completed. Actual
1024×900 mode, viewport/screen y=-132 and restoration to 1024×768 passed exact
original member-pixel checks. Geometry x=19 now matches the displayed front
buffer. Extracted Alpha has one live and zero duplicate static representations;
the exact unique `.backdrop` entry, edge/copy pixels, nested rejection and plain
drawer negative all pass. Both inactive and activating foreign windows remain
below the protected lift and retain the expected focus owner after drop.

Global openings took 27/15 ms and drops 8/6 ms. Draw counter stayed 8 throughout
both operations. Raw final failure scans and guest trace were clean; final
observation and explicit QMP exit succeeded. Root independently replayed raw
oracles, exact original input membership, unchanged boot-input hashes,
screenshot hashes and original pixels across all group operations, as well as
foreign-window output and shutdown lifecycle.

Earlier failed attempts remain retained, including missing fixture HOME in
Control026, wrong F12 acknowledgement mapping, partial trace parsing, transient
registration sampling, cursor contamination and the real geometry discrepancy.
Their partial successes were never substituted for this full PASS.

| Group031 artifact (relative to `attempt-031/tcg02`) | SHA-256 |
| --- | --- |
| `verdict.json` | `c982daded6964c81e072470a3012f467dada45e7d772f6769b1202174a85bc7b` |
| `root-replay.json` | `113a69bd4974116eaad9528298306556de4b100157c6360906336cbe709f0353` |
| `verified-manifest.json` | `58fdd89e340a9952ec2b465779ec04402fd7a496c451edb5b48956bf8d9f6bfa` |
| `serial.log` | `3471727630720b6d66ca3e279193f91c32596cb47e47875d4c701b6dcd2c50cf` |
| `host.log` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `guest-errors.log` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `qmp.jsonl` | `c1f24b21f9eef807e0341e77c1e8dd899b86e36aa090fb371d4e185367280314` |
| `tiles-groups.json` | `3b810f67ced8e5528519a2e2081f190748960cce7ae30b690946d7cf736ba897` |
| `tiles-lift.json` | `7c40f5601ea4f6aded1d2dd3031670c49eae09650be06ee012cb0788e253966d` |
