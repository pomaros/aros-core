# Left-out tile reconciliation

```sh
python3 developer/debug/test/workbench/tiles_backdrop_host/run.py --output /absolute/new/output
```

Compiles the actual registry adapter plus ParseBackdrop's existing canonical
NameFromLock/.info normalization and new suppression branch under ASan/UBSan.
The transition is ordinary left-out → registered live tile → registered stale
fallback → removed tile → ordinary left-out. Case differences match, a prefix
collision does not, and an AppIcon without tile metadata does not suppress it.
The fixture asserts that DOS resolution runs outside the registry lock and that
all comparison paths release locks and temporary allocations. Disabling the
actual suppression branch must fail. Sources, hashes and logs are retained.

The service stores canonical volume paths; the backdrop parser resolves its
file or .info lock to that same namespace. Comparisons use copied service
metadata while holding the registry lock, and retain no owner/string pointers.
The .backdrop record remains on disk. Existing registration add/remove root
updates therefore restore an ordinary disk icon when the tile goes away.
External noncanonical metadata and failed path resolution fail open: they do not
hide a potentially unrelated disk icon. Actual desktop uniqueness and restoration
still require guest evidence; this host test mocks DOS resolution and registry.
