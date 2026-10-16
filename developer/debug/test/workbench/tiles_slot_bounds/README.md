# Mixed grid slot bounds regression

Run from the repository root:

```sh
python3 developer/debug/test/workbench/tiles_slot_bounds/run.py --output /tmp/slot-host-new
```

The runner extracts the actual IconList lasso rectangle and selection-state
transition, single-entry rectangle projection and viewport clipping, and
neighbor rectangle projection. Unique start delimiters must occur exactly once;
missing delimiters or mutation guards fail the run. The generated C runs under
AddressSanitizer and UndefinedBehaviorSanitizer. The MUI area-rectangle service
is mocked to the registered area's local extent; this does not execute graphics
blits, the MUI dispatcher, or the neighbor redraw call.

Cases cover mixed 1x1/2x2 explicit slots, nonzero view and object offsets, partial
viewport clipping, lasso selection/deselection and unchanged selection, and
unchanged legacy non-slot centering and free positioning. In the reproduced
lasso case the content rectangle ends at x52, before the old incorrectly shifted
slot minimum x53. Exact clear and neighbor bounds detect both horizontal and
vertical unwanted centering.

Three separately compiled negative controls remove the production slot guard
from only the lasso, clear, or neighbor snippet. Each must compile successfully
and fail a runtime assertion; a build failure is not accepted as detection.

Evidence is retained in the requested fresh output directory: source snapshot,
four generated C files, compiler commands, build/run logs, return codes and
SHA256 hashes. `aros-build-tiles-indicator-api/slot-host-001` passed all checks.
These are host geometry/state tests, not VM rendering evidence.
