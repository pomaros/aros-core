# Lift cache and view transition regression

```sh
python3 developer/debug/test/workbench/tiles_lift_cache_host/run.py --output /absolute/new/output
```

ASan/UBSan compiles extracted production `draw_tile`, publication/cached-bitmap
helpers and the actual retry/snapshot-refresh blocks. A deferred Workbench publish
still charges the render budget and clears dirty, while retaining a pending cache.
Retry publishes that cache without rendering; an outstanding provider blocks retry.
Owned healthy/fallback/group caches and foreign independent snapshots are selected
separately. Owned hover repaint runs at 33ms; foreign snapshot copies remain 500ms.
A second fixture extracts the actual IconList prepass: both ordinary and 32x32
slots clear before switching to list view, without querying tile owners.

Four negative controls restore publication early-return, stale list slots, retry
while a source owns a message, and foreign fast-copy behavior. Each must fail.
Artifacts retain exact production sources, extracted blocks, mutants, sanitizer
logs and hashes. Graphics/Workbench calls are mocks; native and guest validation
remain necessary for actual pixels and lifecycle.
