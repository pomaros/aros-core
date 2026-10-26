<!-- Copyright (C) 2026 John KNIPPER <code@jkn.me> -->
<!-- Licensed under the AROS Public License (APL) Version 1.1. -->

# DeviceTree command host regression

This suite owns the common command regression used by product test launchers.
It compiles the exact selected `workbench/c/DeviceTree/main.c` bytes against
synthetic DOS/OpenFirmware resource calls. Fourteen scenarios cover default and
property output, string lists, unaligned big-endian cells, byte output, truncation,
control-character escaping, withheld properties, missing nodes/resources,
trailing slashes, depth bounds, Ctrl-C and output-flush failure.

A disk-loaded resource need not have a ROM resident tag. A compiled mutation
that adds a ROM-tag lookup must hit the exact assertion in the resource facade;
a generic crash is insufficient. OpenResource remains the command's source of
resource availability. The resource implementation, native ABI and guest runtime
are not exercised: the facade uses host printf-width scalar types.

Run with a selected core Git checkout and a fresh output directory outside it:

```sh
python3 developer/debug/test/openfirmware_host/run.py \
  --source /ABS/CORE --ref COMMIT --output /ABS/NEW-OUTPUT
```

Omit `--output` for a temporary routine run. Retained runs record the exact input
bytes, source commit, commands, logs and compiler hash. Dirty source requires
`--allow-dirty-diagnostic` and yields DIAGNOSTIC_PASS. Compilation and every case
have wall/CPU limits and disable core dumps. ASan and UBSan remain active;
LeakSanitizer is disabled for the Darwin host where it is unsupported.
