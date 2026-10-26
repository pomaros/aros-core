# Exec namespace access and CRT hook publication

Run from the common core root:

```sh
python3 -B developer/debug/test/crt/exec_registry_host/run.py --output /absolute/new-output
```

The fixture executes the actual registry constructor, CRT hook initializer,
optional-library lookup and shared namespace guard. Eight contenders must
publish one initialized registry, release seven losing allocations and perform
80,000 lookups. The eight-way allocation barrier makes publication contention
deterministic. A refused write lock releases every unpublished candidate.

The guard uses `execlock.resource` for system lists. UP without that resource
uses local interrupt exclusion; an SMP build without it refuses the operation.
Allocation, blocking waits and library opens happen outside the namespace lock.
Already initialized semaphore nodes are published directly under that lock;
AddSemaphore would reacquire it. Registry storage lives until system shutdown.

Missing registry access or task-storage allocation causes CRT initialization to
fail. Slot failure leaves no linked callback and releases the registry semaphore;
a later successful initialization links once. Optional-library discovery does
not open absent libraries or call OpenLibrary while holding the list lock.

Both UP and SMP compilation paths execute with modeled resource/mutex/task-slot
operations under ASan/UBSan. Exact negative mutations must reject duplicate
publication and an unsafe missing-resource fallback. These are HOST algorithm
checks, not real interrupt, spinlock, loader or guest qualification. Generated
executables are temporary; source, diagnostics and input identities remain in
the output directory.
