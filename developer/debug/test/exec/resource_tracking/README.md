# Cooperative resource ownership (`task.resource` 1.6)

`TaskResourceControl()` is vector 22, declared through `<proto/task.h>`;
requests and counters are in `<resources/task_rt.h>`. Callers must check that
`OpenResource("task.resource")` succeeds and its `struct Library.lib_NegSize`
is at least `22 * LIB_VECTSIZE` before calling the new vector. Older resources
remain usable by older clients; the DOS exit hook checks vector availability.

This is an opt-in ownership registry, not automatic tracking of every Exec or
DOS allocation. `RT_ADOPT` asserts exclusive ownership of an independently
allocated `AllocMem` block (exact size), `AllocVec` block, one exclusively held
semaphore acquisition (nest count one), private DOS file or lock, private
`CreateMsgPort` port, or allocated signal bit. It must never register framework
owned resources, process streams/current-directory locks, borrowed pointers,
shared semaphore state, or a resource already released by another mechanism.
No Apple-specific interface is required.

## Ownership and escaping references

An adopted resource has an opaque nonreused handle. `RT_FORGET` removes its
registration without releasing the object; the caller then owns manual cleanup.
`RT_DISOWN` detaches a transferable allocation/file/lock, and `RT_ACCEPT`
attaches it to another task. Unaccepted transfers remain exported, not silently
freed. DOS files and locks can only be adopted/accepted by a Process. Signals,
ports and semaphore acquisitions cannot transfer because their release depends
on owner context. Signal bit identity is scoped to its owner, and a tracked port
already owns its signal bit.

Before publishing any pointer, message, asynchronous callback or dependency,
obtain an `RT_PIN` lease. Release that exact lease with `RT_UNPIN` only after its
last possible use, reply, cancellation and callback has finished. Leases are
single-consumption tokens; duplicate/stale unpins fail. A lease is a cooperative
capability, not a hostile-caller security boundary. The registry cannot discover
unregistered publishers.

Pin backing storage for the entire registered lifetime of an embedded resource
such as a semaphore. Registration order is not a dependency graph. For example,
register an embedded semaphore and its containing allocation, pin the allocation,
then exit: the semaphore can be released, but the allocation is quarantined
until a surviving coordinator releases the lease and establishes quiescence.
The fixture tests this with the allocation registered last. Exact same-address
aliases are conservatively rejected, even across resource types.

Before shared-state mutation, mark **every participating registration**, including
its semaphore acquisition, with `RT_BEGIN_MUTATION`. End each mark only after
consistency is restored. Marking only the data does not implicitly mark its lock.
An aborted transaction retains both dirty data and dirty lock acquisition; it
does not expose half-mutated state by automatically unlocking. The registry
neither rolls back transactions nor infers dependency/cycle graphs.

## Exit phases and refusal

The DOS process exit path invokes `RT_PREPARE_EXIT` before local variables,
streams, command segments, current directory, CLI and DOS library teardown.
This closes registration and processes only owned files/locks. A successful
return means this phase succeeded; other resource types intentionally remain.
`Close()` consumes its FileHandle even when the handler returns an I/O failure:
the record is retired, `close_errors` increments, and cleanup reports failure.
Such a handle is never recoverable or closed again.

The task.resource RemTask wrapper sends existing `TNA_REMOVED` notifications,
then performs the remaining cleanup before its own Forbid/spinlock section and
before original Exec RemTask destroys task context. Endpoint users must invalidate
published pointers during the notification or already hold leases. Notification
alone does not establish quiescence of unregistered references. All destructors
run with interrupts and scheduling enabled, outside the registry lock, while the
owner is still the current task. Metadata uses Disable plus the existing Exec
spinlock on SMP, with no allocating/blocking operation inside that lock.

Public calls refuse disabled, forbidden or logical supervisor contexts using
Exec nesting counts and the existing `KrnIsSuper()` interface. Reentrant cleanup
refuses. A remote/unsafe removal closes and retires the context without invoking
destructors. If retirement arrives while cleanup is outside its metadata lock,
the current completed destructor is accounted for and later destructors stop.
An interrupted destructor whose execution never resumes remains uncertain;
there is no claim of safe forced cancellation or automatic recovery from that
state.

Pins, dirty records, pending-message ports and failed preconditions produce
explicit quarantine. Cycles retain their resources rather than guessing an
order. `RT_RECOVER` accepts only detached, transferable quarantine with no leases,
and only with both `RTF_QUIESCENT | RTF_ROLLBACK_DONE`: these are the coordinator's
assertions, not facts the registry can independently prove. Owner-bound resources
and remote-retired contexts cannot be recovered through this API. They require
service-level intervention/restart. Retained metadata is intentional and visible
in `contexts`, `closed_contexts`, `records` and `leases`; it is not a successful
zero-leak result. This API does not promise universal reclamation after remote
RemTask, task faults or kernel shutdown.

## Verification

Run from any directory with a host Clang supporting ASan and UBSan:

```sh
python3 developer/debug/test/exec/resource_tracking/run.py --output /tmp/rt-run-1
```

The runner extracts `TaskResourceControl.c`, the complete RemTask wrapper, and
the DOS early-exit call block freshly on every run; no copied implementation is
used. It checks source ordering against DOS teardown and Exec removal, compiles
with ASan/UBSan, imposes compile/run deadlines, rejects nonzero exits and stderr,
and validates every required result before writing `resource-lifetime.json`.
Artifacts include the generated translation unit, compiler command/log, host log
and hashes of all extracted source files. Use a new output directory for evidence.

The host services model UP Exec/DOS semantics and assert that destructors have a
live current task, valid DOS phase where needed, no Forbid/Disable and no logical
supervisor context. Notification must precede port deletion. An independent
allocation ledger includes registry contexts, records and leases; file/lock/port,
library-reference and signal accounting is independent of `RT_STATS`.

Cases cover clean exit holding all seven resource kinds, reentrant cleanup,
failed Close consuming a handle, ownership transfer without peer release,
owner-local signal identity, bad context refusal before allocation, backing
storage dependencies, cycles, dirty mutation abort, pending port messages,
remote removal, and a remote request during an unlocked Close callback. The
omitted-registration negative control detects one outstanding sentinel even
though registry counters report zero. Expected quarantine is counted explicitly:
ten allocations (five records, two retired contexts, three objects) remain before
**test-only** teardown; they are not reported as reclaimed by the implementation.

This is an instrumented host fixture, not a booted AROS guest, real DOS handler
I/O test, or multicore execution proof. The DOS exit body outside the extracted
block is represented by a phase transition, with source-order assertions against
its real teardown calls. Native compilation separately checks generated headers,
resident ABI integration, platform macros and the actual DOS/task.resource code.
