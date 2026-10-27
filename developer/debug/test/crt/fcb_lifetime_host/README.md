# Local file operation lifetime regression

```
python3 developer/debug/test/crt/fcb_lifetime_host/run.py --output /tmp/fcb-lifetime
python3 developer/debug/test/crt/fcb_lifetime_host/run.py --tsan --output /tmp/fcb-lifetime-tsan
```

The output directory must not exist. The runner compiles actual production C
bodies for fcb retain/hold/release, final record metadata release, read, write,
close, dup, dup2 and closedir. The recursive descriptor table, DOS I/O, registry
allocation and process record-lock removal are modeled boundaries. Final record
metadata release executes its actual function with a modeled metadata disposer.

Each read/write case pauses after admission, before taking the native I/O lock.
Another thread closes and reuses the descriptor number. The admitted operation
must use the original file, and its native handle and record metadata must each
be released once after that operation finishes. Native error translation occurs
before final handle disposal can overwrite DOS IoErr. Additional cases check
reference overflow without foreign dispatch, duplicate reference rollback,
directory reads/close and borrowed native handles. No descriptor-table guard
may span the modeled blocking DOS operation.

Five compiling controls independently remove operation retention, operation
release, record metadata disposal, duplication rollback and overflow admission.
They must fail for their exact named assertion; a compilation error or timeout
does not qualify a control. Both ASan/UBSan and TSan modes are available.

This does not prove the complete fd/cwd namespace migration, all local file
operations, record-lock semantics, directory handler behavior or a guest runtime.
The existing native-file-io and native-socket regressions cover their own
positioned-I/O, global descriptor allocation and foreign operation lease paths.
