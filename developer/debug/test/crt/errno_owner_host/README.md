# Shared C-runtime task errno ownership

```
python3 developer/debug/test/crt/errno_owner_host/run.py --output /tmp/errno-owner
python3 developer/debug/test/crt/errno_owner_host/run.py --tsan --output /tmp/errno-owner-tsan
```

Use new output directories. The fixture compiles the actual stdc owner and
provider functions with two instances of the actual linked/shared accessor
body. Native task slots, incarnation identities, memory allocation, provider
registration and library opens/closes are modeled boundaries. A real host
pthread supplies the independently admitted worker and its task-local slot.

Checks cover cross-module error identity, three same-task openers, arbitrary
close order, preserved error across additional opens, owner and detached-binding
close refusal, reference overflow, zero initialization, allocation/publication
failure, worker admission and errno isolation, failed admission/cancellation,
final slot retirement before disposal, and resource/registration rollback. The actual program-startup body resets
errno at program entry; opening another library instance preserves it.
Six compiling controls split the module error cell, admit foreign close,
lose opener retention, retain a stale task slot, allow reference wrap or omit
the program-entry reset. Each
must fail the named assertion; compile failures and timeouts do not pass.

The shared stdc runtime, not pthread's program-local ThreadInfo, owns errno.
Every task executing shared C-runtime code must enter its runtime first;
StdC's v3 admission provider primes a worker independently of POSIX file use.
Same-task openers share the cell, and the final close clears the binding before
freeing it. Foreign owner closes refuse before generated module teardown.
Forced owner removal belongs to the separate post-stop cleanup protocol.

The STDC_STATIC implementation is the freestanding bootstrap error cell. It
has no shared-module or worker-admission contract. Threaded native applications
use the shared runtime; this fixture does not qualify static threaded C code.

This fixture does not execute generated module OpenLib/CloseLib, the complete
provider registry, loader relocation, guest task creation, a filesystem or
hardware. A public StdCBase layout change requires a coherent rebuild of the
runtime and all consumers before linked guest qualification. Host success
alone does not resolve the observed guest defect.
