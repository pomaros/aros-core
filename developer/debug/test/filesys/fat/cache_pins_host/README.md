# FAT file-transfer cache borrows

Run from the shared core:

```
python3 developer/debug/test/filesys/fat/cache_pins_host/run.py --output /tmp/fat-cache-pins
```

The output must not exist. ASan/UBSan execute the actual `OpRead`, `OpWrite` and
release-helper bodies with a modeled four-range cache and disk operations.
Thirty-two idle handles must coexist; successful, failed and zero-progress
transfers release their borrowed block and data pointer. Metadata updates must
begin after data release, and dirty ownership stays with the cache. Two compiling
negative controls remove the read or write release and must fail the matching
assertion. Compile failures do not count as detected defects. Debug-only unused
parameters are allowed; other warnings are errors.

This is a HOST mechanism check, not an actual FAT, disk-flush or guest pass.
The existing resize regression checks zero extension separately.

`../cache-open-files.c` is the Native DOS regression (`test-fat-cache` build target). Build it against the selected
SDK and run `cache-open-files WORK:` on a disposable FAT volume with a sixteen-range
cache. It atomically creates its own test directory, refuses an existing one,
writes thirty-two 32KiB files, retains all readers while checking the first/last
bytes, then closes and deletes its own resources. More handles than cache ranges
are essential to the regression. The required marker is:

```
[FATCACHE] PASS readers=32 bytes=32768 reread=1 cleanup=1
```

An SDK compile is not guest acceptance. Bind the actual handler, image and bounded
runtime receipt before claiming that result.
