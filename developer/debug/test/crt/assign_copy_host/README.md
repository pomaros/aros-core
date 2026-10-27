# Owned assign capture and mutation

Run `python3 run.py --output /absolute/new/directory`, adding `--tsan` for
ThreadSanitizer. Actual DOS packet and assign function bodies execute with
host Exec, allocator, DOS-list and FIFO adapters. A real host handler thread
requires DOS WRITE before serving COPY_DIR/FREE_LOCK. The suite covers capture
racing replacement/removal, primary and secondary members, correct handler
selection, OOM/error paths and 32-bit generation rollover. Ten compiled runtime
mutations must fail. Six deliberately restore deadlocks and are stopped by an
eight-second subprocess timeout; other mutations fail their assertions.

DupAssignLock231 (DOS 50.87) sends the primary bound-lock copy under ALL/ENTRY
READ, drops both guards after enqueue, then waits. A later FREE_LOCK is ordered
behind COPY_DIR on that port. The returned native lock is owned by the caller.
Other assign kinds return their type without resolving paths inside the guard.

RemAssignList compares an owned snapshot outside the namespace guard and only
mutates the list if the generation still matches. AssignAddToList resolves and
names locks outside the namespace guard, then revalidates before publication.
Retries are bounded. Failure preserves the incoming caller-owned lock. Changed
entries increment a two-word generation under ALL WRITE or ENTRY WRITE;
snapshot readers hold both guards, including against AddDosEntry publication.
The guard protocol requires a handler honoring native packet/lock ordering.

The fixture executes neither LockDosList's real semaphore implementation nor
GetDeviceProc's deferred resolution. Existing late/nonbinding lookup still
needs review for handler waits under READ. Native guest, handler teardown,
forced removal/cancellation and whole-system SMP behavior remain unqualified.
