# Actual group loader capacity regression

```sh
python3 developer/debug/test/workbench/tiles_group_load_host/run.py --output /tmp/group-load-new
```

Extracts the complete, uniquely delimited production `load_path` function from
Tiles/main.c and compiles it with actual tiles_group.c under ASan/UBSan and
warnings-as-errors. DOS enumeration and load_tile are controlled mocks; this
checks loader decisions and resource ownership, not filesystem persistence.

Cases cover exactly40 accepted children;41 rejected before loading; insufficient
remaining service capacity; enumeration failure during preflight; failed rewind;
enumeration failure after three loads; directory growth beyond40 between passes;
and duplicate group names. The directory-change/failure cases may retain child
entries for ordinary cleanup, but their valid flags and group layout are cleared,
so no truncated layout is liftable. Lock/FIB/icon cleanup is asserted throughout.

Separate production-code mutations remove the preflight capacity check and
ignore the first enumeration error. Both must compile and fail runtime
assertions. Build failure does not count as mutation detection. Output retains
source snapshots, generated translation units, command lines, logs, hashes and
return codes. Recorded run: aros-build-tiles-indicator-api/group-load-host-001.

The current loader uses stable tile slots and explicit group membership indices.
The fixture now models active versus retired slots, validates that retired slots do
not consume the active preflight budget, and checks membership when reused indices
are nonzero. Failed/incomplete enumeration deactivates staged members and publishes
no group; its icon is released. Successful groups retain their drawer icon until
cleanup. Both preflight-removal and ignored-enumeration-error mutants must fail.

This harness extracts actual load_path only. load_tile, DOS notifications, provider
callbacks and screen transitions remain modeled; it is not a pending-reply/reload
lifetime or native filesystem test.
