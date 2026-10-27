# CRT context registry regression

```
python3 developer/debug/test/pthread/crt_registry_host/run.py --output /tmp/crt-registry
python3 developer/debug/test/pthread/crt_registry_host/run.py --sanitizer thread --output /tmp/crt-registry-tsan
```

Output directories must not exist. The runner includes the actual
`aros/crt_thread_context.h`; empty SDK header stubs substitute the host boundary
types and functions declared in `host.c`. Namespace exclusion, semaphores,
library opens and closes are modeled with host locks and allocated instances.
This is protocol evidence, not pthread/POSIX integration or guest acceptance.

Both UP and SMP configurations run concurrent first publishers, creator/worker
admission and library ownership. Assertions cover one permanent registry with
an owned name, invalid/overlong/duplicate registration, withdrawal of unregistered nodes,
creator-side pin release, reverse worker unwind, partial capture cancellation,
inactive providers, pin/entry failures, provider replacement during capture,
bounded retry exhaustion, overlong names, excess providers and final expunge.
SMP also refuses a missing lock resource or failed namespace write lock; UP
uses scheduler/interrupt exclusion. Allocation and provider callbacks must run
outside namespace exclusion, and callbacks other than Active must run outside
the provider semaphore.

Eight compiling controls remove individual guarantees. They must fail for their
named assertions; compile failures and timeouts do not qualify a control. Tests
run with ASan/UBSan or TSan. The fixture cleans its permanent registry only after
all host users have joined; production never frees that registry.


The header uses bounded scalar operations and Exec primitives for initialization
and capture. A target compilation must also inspect undefined symbols: the
protocol cannot depend on a C runtime that it is admitting. That target check is
separate from this host model; both optimized and debug objects need inspection.
The provider and creator migration is separate from staging this shared header.

Provider selection occurs before pin opens. The mock pin deliberately changes
its current instance; capture must receive the original opaque selection.
The `source-reselected-after-pin` control must fail for that exact mismatch.
The selected source is synchronous creator state only: worker contexts retain
their own references. The version-3 namespace separates this callback contract
from earlier prototypes without a compatibility path.
