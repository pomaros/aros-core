# Tiles reload lifecycle host regression

Run `python3 run.py --output /tmp/tiles-reload-host-unique` with Clang available.
Every run extracts current production `discard_app_messages`, `release_tile`,
`reload_catalog`, `release_screen`, `load_tile`, `screen_messages` and `unsubscribe_screen` into retained C translation
units and runs them with AddressSanitizer and UndefinedBehaviorSanitizer.

The mock AppIcon removal queues a final old Region message. Both existing and
removal-time messages must be replied before catalog IDs are reused. Screen
unlock requires overlays, snapshots, panels, input broker, icon registrations,
bitmaps and old messages already released. Stable pending source owners survive
new definitions and same-path contract changes; an exact-contract move keeps its
owner address, while a completed owner can be recycled. Four mutation controls
must fail assertions: omitted queue drain, early screen unlock, pending-slot reuse and lost subscription handle on failed cancellation.

The notification fixture injects a WAIT_REPLY message after the initial drain,
returns FALSE from cancellation, and requires the original handle and live port
to survive until a retry replies to the message and cancels successfully. A
queued AFTER_OPEN during shutdown must be replied without reacquiring a screen.
It also exercises startup with neither port nor subscription.

This is a deterministic service model, not native concurrent execution. It models cancellation contention; it does
not execute Intuition locking or prove OS message-port lifetime,
source transport implementation, callback concurrency or actual screen pixels.
The lifecycle fixture's catalog scan and overlay shutdown are mocks with checked
postconditions; the pending fixture executes the real slot-selection code.
Logs, generated units, compiler commands and the captured main.c hash are retained
in the chosen output directory. Existing output directories are refused.
