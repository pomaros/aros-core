# Group surface geometry matches drawing

```sh
python3 developer/debug/test/workbench/tiles_group_geometry_host/run.py --output /absolute/new/output
```

Compiles actual IconList area calculation, image offsets, anchor placement and
sampling branch with ASan/UBSan. The observed 64px group with a 92px label must
report and draw x19 from entry x5; 128px groups retain x5. Checks bottom anchoring
at screen heights768/900, exact left/right/top surface edges, free placement and
view/layer scrolling. DOS/registry dispatch and label measurement are mocked;
actual drawing and reported geometry use the same production calculations.

Restoring the old surface-only sampling area fails the query-versus-draw check.
Ignoring image offsets when anchoring fails the left/right edge checks. Exact
source, extracted blocks, hashes, commands and both failed mutants are retained.
Main service diagnostics also re-emit group geometry when its queried position
changes; no pixel oracle or expected centering is relaxed by this change.
