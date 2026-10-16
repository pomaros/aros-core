# Front-screen lift routing regression

```sh
python3 developer/debug/test/workbench/tiles_front_host/run.py --output /tmp/front-host-new
```

Compiles the actual `lift_screen_front` helper, opening guard, queued input loop
and service-work screen guard extracted from Tiles/main.c. Intuition, input and
presentation functions are controlled mocks. ASan/UBSan and warnings-as-errors
cover front-screen opening, refusal on another screen, valid local/external
click routing, a screen switch between two queued clicks, discarded queued
hotkeys and dismissal at the next service-work check. Every mock mutation or
activation asserts the Intuition base lock has already been released.

Three separately compiled negative controls ignore screen identity, omit the
queued-event guard, or omit the service-work guard. Each must fail an assertion;
compiler errors do not qualify. Evidence retains the original main.c, generated
source, command lines, hashes and logs. Recorded run: front-host-001.

These checks act on sampled screen identity. They do not make a screen switch
atomic with subsequent activation, prove immediate dismissal, exercise the
Commodities callback, or establish a hard real-time delay. The production loop
checks before each queued event and at service-work wakes; a switch immediately
after a check is observed at a later check. Native screen/input behavior still
requires guest validation.
