# PIPE readiness / PX3

The generic handler implements the packet API in `<dos/pipe.h>`:
`ACTION_PIPE_SET_NONBLOCK`, `ACTION_PIPE_READ_NOTIFY`, and
`ERROR_PIPE_WOULD_BLOCK`. Nonblocking mode affects reads only. Reads return
available data rather than waiting for the entire requested buffer. Existing
interactive-handle behavior is preserved.

The signal recipient is a borrowed task pointer. Clients must synchronously
unregister before freeing its signal or exiting; closing the reader unregisters
as well. Signals coalesce, so clients must recheck readiness with nonblocking
reads. Registration immediately rechecks buffered data or EOF. This API does
not implement POSIX `fcntl`/`poll` wrappers or task-lifetime tracking.

The production changes originate in graft commits
`ed9dd99db6d44cb1919d340c6b50614ce4c3aaab` and
`8ad4f3c58650e6d46a92558b7b9d897a22f829e9` from
`aros-upstream` branch `fork/aarch64-darwin-graft`. Interactive handles from
`0c9058425d4aebe76f99f2fd227a996a16d35039` already exist in the base. This port
publishes the constants, validates read-notification ownership, and handles
zero-length requests without parking them.

## Build and real guest test

Configure a separate output directory using the project's canonical `env.sh`
and toolchain. Run build goals sequentially to avoid generated-tool races:

```sh
gmake -C "$BUILD" -j2 workbench-fs-pipe
gmake -C "$BUILD" -j2 test-pipe
python3 developer/debug/test/pipe/readiness_vm/run.py --selftest
```

The `readiness` executable mounts the candidate handler `SYS:PIPEHDLR` as its
own `PX3PIPE:` device, then tests actual DOS packets and reads/writes. It verifies
empty nonblocking reads, zero-length reads, interactive handles, notification on
new/already-buffered data, partial blocking reads, EOF, invalid registrations,
and a control which writes after unregistering and receives no signal. The
mount remains resident until guest shutdown because this handler has no
`ACTION_DIE`; this fixture should run once in a disposable VM.

Package committed candidate sources with an existing headless AROS VM baseline
whose DOS startup executes `SYS:SMP0` using `RunCommand`:

```sh
python3 developer/debug/test/pipe/readiness_vm/package.py \
  --base-esp "$BASE_ESP" --build "$BUILD" --vm-tools "$VM/tools" \
  --output "$ARTIFACTS/esp"
python3 developer/debug/test/pipe/readiness_vm/run.py \
  --esp "$ARTIFACTS/esp" --output "$ARTIFACTS/run" --backend hvf
```

The SMP0 label belongs to the baseline command transport; this test makes no
scheduler fairness claim. Packaging records the baseline revision independently
from the candidate revision and hashes the handler, executable and bundle.
The runner requires the exact PX3 proof and unregister control, command return,
startup proof, empty QEMU error trace, three seconds of post-proof observation
and orderly QMP exit status zero. It rejects timeout, late faults, duplicates
and missing evidence. Verifier self-tests alone are not guest evidence.

## Recorded native result

`evidence/attempt-002/verdict.json` records an actual one-CPU HVF AROS run:
PX3 and its unregister control passed, DOS returned success, startup reached
B2Q, QEMU's guest-errors/unimplemented trace stayed empty, and QMP shutdown
returned zero after three seconds of observation. Candidate source was
`e7220b252db83689cbb9fe80e4c46d7832cd1d01`; the independently built headless
baseline was `30b8756088ffa1f71d8cd24ca7fc95f6822d4a52`. Candidate handler and
test were compiled from the `e9d0e0f437` merged-tree base with the canonical
`aros-crosstools` LLVM 20.1.0 toolchain. Their hashes and baseline build details
are in the recorded provenance. The handler/test goals both exited zero.

The preceding `attempt-001` failed before the test marker because the shell
macro entry did not initialize the global library bases used by helper
functions. The fixture now uses normal startup with the existing headless flags;
the production handler did not change. Failed logs/verdict are retained alongside
the successful run. Full bundles, generated build output and build logs remain
in the claimed external `aros-build-pipe` directory; log hashes are recorded here.

This verifies the packet API through the actual handler and DOS guest path.
It does not establish SMP behavior, physical Apple hardware behavior, or a
complete POSIX runtime. Notification lifetime still follows the explicit client
unregister contract above.
