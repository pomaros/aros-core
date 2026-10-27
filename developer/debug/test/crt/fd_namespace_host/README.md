# Retained descriptor namespace

Run `python3 run.py --output /absolute/new/directory`, adding `--tsan` for
ThreadSanitizer. The actual namespace engine runs with modeled allocation,
descriptors and registry close. Eight workers retain the namespace while its
creator releases it. Immutable process identity belongs to the namespace. Owner
finalization runs once before any descriptor disposal, outside the table guard.
Final disposal must close descriptors before releasing
the owned registry. Allocation refusal is checked. Three compiled mutations
remove retention, close the registry early or omit descriptor disposal; all
must fail at runtime.

Each runtime/admission must own a reference before publication. Retain requires
an existing reference; it cannot revive an unowned borrowed pointer racing its
last release. The disposal callback must remain executable through final release
and handle every populated table entry, including any reserved-entry sentinel
used by its eventual client. The engine invokes it without the table guard.
This fixture does not exercise POSIX descriptor operations, provider callbacks,
pthread admission or a guest. Namespace integration must verify those separately.

The active POSIXC attachment and close paths have a separate client regression:

```
python3 runtime.py --output /absolute/new/client-directory
python3 runtime.py --tsan --output /absolute/new/client-tsan-directory
```

It compiles the actual namespace engine plus POSIXC attachment/exit, namespace
lookup, descriptor allocation/free, optional-library close, private-pool cleanup,
and record-owner identity/finalization functions. It also invokes the actual
module record-table initializer. Modeled boundaries are parent-base discovery,
Task identity, DOS stdio construction, native file disposal and lock removal.
Eight workers attach while the creator is alive, then use the table after the
creator's library base, Task and private pool have been freed. Process identity
remains a scalar in the namespace; locks and the registry survive until the
last worker closes. Allocation refusal and repeated rollback are checked.

Five compiling ASan/UBSan controls restore an unretained client, creator-base
lookup, premature registry close, missing owner finalization and lost process
identity. TSan checks production and the four controls that do not deliberately
read freed memory. Each control must fail for its named cause, not a timeout.

This proves post-attachment table ownership at modeled boundaries. It does not
prove capture-before-create admission, relative runtime-base binding, cwd,
module loading, inherited global numeric reservations, full native file/lock
semantics or a linked guest. Those contracts require separate integration.
