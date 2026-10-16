# Bounded lift input broker regression

```sh
python3 developer/debug/test/workbench/tiles_input_host/run.py --output /tmp/input-host-new
```

Includes actual production `input.c` with OS/Commodities mocks, compiled with
warnings-as-errors and ASan/UBSan. Tests group/global hotkeys, disabled groups,
invalid key parse failure, singleton ownership, Esc only while lifted,
key repeats/releases, coordinate capture before subsequent pointer movement,
modal button release after dropping lift, passthrough motion,16-event overflow,
signal counts, close and reopening. Parser mocks do not validate real ParseIX
syntax or real Commodities ordering; native/guest tests must cover those.

Input callbacks are serialized by AROS Commodities' cx_SignalSemaphore;
DeleteCxObjAll removes the broker under the same semaphore before freeing it.
The source reference is `workbench/libs/commodities/commodities_inputhandler.c`
and `removecxobj.c`. The fixed ring uses acquire/release atomics for the callback
producer and main-task consumer. The callback does not allocate application
memory, call tile sources, wait for main, or send cloned input messages.

Caller owns the screen lock and commodities.library lifetime through Close.
Screen MouseX/Y are copied at callback time; this prevents later main-task
pointer motion changing the queued click, but actual input-device/Intuition
batch ordering still requires guest validation. The queue deliberately drops
new events when full, while modal mouse buttons remain consumed. Mouse release
is consumed even if main already dropped the lift after the corresponding press.

Mouse pairing regression also verifies that a button pressed before lift passes
its release through while lifted. A button pressed during lift still has its release
consumed after dropping. Restoring the old `lifted || tracked` release condition
fails the actual production-source test.

Scoped edge panels publish up to eight copied rectangles. The callback reads atomic
fields between two sequence checks; an odd/changing publication is forwarded without
waiting. Tests cover topmost overlap selection, negative-left clipping, exclusive
right/bottom bounds, outside passthrough, an in-progress publication, invalid-set
preservation, panel removal before release, and full-lift precedence. All APIs remain
single-main-writer. This host fixture models interleaving but is not an SMP scheduler
or live input.device test; root must clear the panel set when its screen is not front.

Malformed, oversized and non-key group bindings are isolated to their own group
and each emits a diagnostic. The mixed-binding test requires global lift and a
later valid group binding to remain usable; `none` (case insensitive), empty and
NULL bindings disable quietly without removing the broker.
