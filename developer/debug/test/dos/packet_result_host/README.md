# Explicit DOS packet results for Tasks

DoPktResult adds an explicit secondary result to synchronous DOS packet calls.
Ordinary Tasks have no Process pr_Result2 field. The wrapper accepts seven
native-width arguments, or NULL for zero arguments, and reports transport
allocation/invalid-target errors explicitly. Existing DoPkt keeps its vector
and signature. NIL retains the existing primary result and explicit secondary
zero, leaving a Process's prior pr_Result2 unchanged.

The common source exposes DoPktResult at LVO228 in dos.library 50.85, immediately
after ReplaceObject227. Use interfaces generated from this source and rebuild
consumers with DOS. Historical development SDKs used other unqualified vector
layouts; they are not interchangeable. No empty vectors are reserved for an
unintegrated mapping prototype.

Run `python3 run.py --output /absolute/new/directory`. The actual packet helper
and wrapper execute against host Exec allocation/IPC facades. Seventeen cases
cover Task/Process callers, arguments, primary/secondary results, NIL, invalid
targets, allocation failures and cleanup. Three compiled runtime mutations must
fail: Process-only assertion, lost allocation errors and Process-field access
in an exactly Task-sized allocation. ASan/UBSan check the real helper bodies.
The fixture does not execute the historical DoPkt wrapper, filesystem drivers,
a real handler or forced-removal/cancellation behavior.

The internal dopacket_after_send helper supports namespace users releasing a
guard after FIFO enqueue and before handler wait. Its caller must clean up
admission failures that occur before enqueue; the callback runs only after a
successful send. It adds no handler-lifetime or forced-task-removal guarantee.
