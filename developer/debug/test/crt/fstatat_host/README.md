# Directory-relative metadata entry points

```sh
python3 -B developer/debug/test/crt/fstatat_host/run.py --output /absolute/new-output
```

The runner compiles complete production fstatat.c and its fstatat64 include
wrapper under ASan/UBSan. The HOST adapter supplies distinct narrow/wide metadata
layouts, directory operations and stat/lstat providers; it does not implement a
filesystem or the production path resolver.

Cases check relative, POSIX-absolute and native-volume paths, follow/nofollow
dispatch, directory restoration on successful and failed metadata queries,
invalid arguments/descriptors, unsupported flags before directory or metadata
operations, values above 4 GiB and surrounding canaries. Exact mutations that
ignore flags or dispatch a wide request into the narrow provider must fail by
runtime assertions. Executables are temporary; input hashes and logs remain.

Public-header symbol redirection, generated library exports, actual stat-buffer
layout/filling and linked guest execution require separate target checks. HOST
macro aliases only isolate provider names from the host C library; they do not
supply missing production functions.
