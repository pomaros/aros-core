# Local file operation lifetime regression

```
python3 developer/debug/test/crt/fcb_lifetime_host/run.py --output /tmp/fcb-lifetime
python3 developer/debug/test/crt/fcb_lifetime_host/run.py --tsan --output /tmp/fcb-lifetime-tsan
```

The output directory must not exist. The runner compiles actual production C
bodies for fcb retain/hold/release, final record metadata release, read, write,
close, dup, dup2, closedir, fstat, fstat64, fsync, lseek, lseek64, ftruncate and ftruncate64 (including their shared resize body).
The recursive descriptor table, DOS I/O, registry
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

Metadata/sync cases pause at the modeled descriptor-table release after actual
admission, before native I/O locking. They close/reuse the number concurrently,
check both success and failure, and make final disposal overwrite errno so that
the caller must preserve its operation error. Metadata conversion runs without an I/O
semaphore held because name/time conversion can reenter CRT services. Invalid and overflow admission are
also checked without foreign fallback.

Seventeen compiling controls independently remove operation retention, operation
release, record metadata disposal, duplication rollback, overflow admission,
metadata retention/error preservation, retaining an I/O lock across metadata
conversion, sync reference release, seek retention/serialization or ignored
seek-buffer flush failure, resize retention/serialization, ignored zero-progress
writes, lost position restoration or corrupt zero fill.
They must fail for their exact named assertion; a compilation error or timeout
does not qualify a control. Both ASan/UBSan and TSan modes are available.

This does not prove the complete fd/cwd namespace migration, all local file
operations, record-lock semantics, directory handler behavior or a guest runtime.
The existing native-file-io and native-socket regressions cover their own
positioned-I/O, global descriptor allocation and foreign operation lease paths.

Seek cases retain the original object during descriptor close/reuse, preserve
native seek and position errors, and keep seek/query under one I/O semaphore.
Additional checks cover SET/CUR/END, beyond-EOF without size growth, wide offsets,
invalid whence/handle, directory refusal and pending-buffer flush failure/retry.
The guest `crt/posixc/lseek.c` test independently expects no size change until a
write; its execution is separate from this modeled-DOS host suite.

Resize cases check both widths under close/reuse, with short buffered writes,
zero-progress failure, size/position query and resize failures, fill seek/flush
and restoration failures. They cover preserved positions beyond a shrinking
EOF, negative lengths, non-writable/invalid/directory descriptors, admission
overflow, wide sizes, pending-buffer retry and the first error surviving a
second restoration error. The mock resize exposes dirty bytes deliberately;
the actual CRT body must clear every added byte while preserving the prefix.
Native filesystem zero-extension, buffered DOS internals and atomic visibility
to independently opened handles are outside this fixture's scope.
