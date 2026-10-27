<!-- Copyright (C) 2026 John KNIPPER. SPDX-License-Identifier: APL-1.1 -->

# POSIXC native path callback

```
python3 developer/debug/test/crt/native_path_host/run.py --output /tmp/native-path
```

Use a new output directory. The fixture compiles the complete production
`__cwd_native_call` callback with the actual public DOS operation numbers.
DOS calls are boundary models. Replacement must reach native `ReplaceObject`
once with the original source and destination pointers, return its result and
preserve its error after clearing stale caller state. Unsupported operations
must retain the native refusal. The missing-relay regression fails before the
correction; three compiling mutations remove dispatch, swap paths or erase the
native error, and must fail for named assertions under ASan/UBSan. Compilation
failure and timeout never qualify a control.

This checks callback dispatch, not DOS helper scheduling, directory ownership,
filesystem replacement atomicity, module linkage or guest runtime behavior.
