# Aggregate group double-click routing

```sh
python3 developer/debug/test/workbench/tiles_group_click_host/run.py --output /absolute/new/output
```

Extracts the actual IconList group/region decision, double-click dispatch and
SELECTUP click-production blocks. ASan/UBSan verifies that a free-anchor group
opens once without a second child-region click; an ordinary live region still
receives rapid repeated clicks, stale/outside regions retain generic Open, and
the first group click still reaches its child. View/centering offsets are checked.
Disabling the group exception must fail this same fixture. Sources, extracted
blocks, commands, hashes and mutant evidence are retained. MUI/Workbench methods
are mocked; actual drawer opening still requires guest verification.
