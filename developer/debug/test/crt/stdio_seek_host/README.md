# Buffered stdio positions beyond 2 GiB

Run from the core checkout:

```
python3 developer/debug/test/crt/stdio_seek_host/run.py --output /tmp/stdio-seek
```

The output directory must not exist. The runner compiles the actual fseeko and
ftello engines, DOS64 dispatch helpers and buffered Seek64 body with ASan/UBSan.
Modeled DOS packets retain unread input, pending output, positions beyond 2 GiB
and injected flush errors. Checks cover relative/end seeks, non-mutating tell,
EOF clearing, output-flush failure and retry, narrow fallback, overflow and
invalid descriptors, directories and whence values.

Five compiling negative controls restore the premature legacy input flush in
seek or tell, or ignore output-flush failure in wide seek, narrow seek or narrow
tell. Each must fail for its named behavior; compiler errors do not count.

DOS64 must be present in the product. A correct 64-bit CRT helper cannot add
large-file support when a product omits that library. HOST evidence does not
qualify its installation, filesystem packets or a complete guest library.
