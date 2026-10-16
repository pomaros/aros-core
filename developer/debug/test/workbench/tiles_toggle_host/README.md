# ENV toggle regression

Run from the source tree with host Clang and ASan/UBSan:

```sh
python3 developer/debug/test/workbench/tiles_toggle_host/run.py \
  --output /tmp/tiles-toggle-new-run
```

The runner extracts the actual `set_value`, `app_messages` and `source_result`
functions from `Tiles/main.c`, balances braces while ignoring comments/literals,
and rejects absent or duplicate definitions. It links the real value parser.
Only platform services, message queues and the surrounding data structures are
host mocks. This is not a guest or concurrent execution claim.

The checks dispatch two queued Region clicks before a notification, an explicit
SET followed by a click, a failed second write followed by a retry, and a failed
first write. They verify successful pending intent survives failed writes,
messages are replied exactly once, the displayed cache stays unchanged until
the authoritative notification, pending intent then clears, and unchanged
notifications do not mark a redraw. Invalid notification results clear pending
intent and invalidate health.

A mutation control replaces the production pending-intent selection with the
old cached-value-only expression. It must fail specifically at the two-click
assertion; a compile failure or unrelated failure is not accepted as detection.
The output preserves extracted source, generated translation units, commands,
compile/runtime logs, source hashes and `result.json`.

Recorded run: `aros-build-tiles-hover/toggle-host-001` passed with production
exit 0 and no sanitizer diagnostics. The old-click mutation exited with SIGABRT
at the intended assertion. `result.json` records the exact source hashes.
