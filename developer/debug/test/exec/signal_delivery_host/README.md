# Remote signal delivery

Run `python3 run.py --output NEW_DIRECTORY` from this directory, or invoke
the script by its full path. Python 3 and Clang are required.

The fixture compiles the actual `Signal` function and its `signal_hook` IPI
callback with ASan/UBSan. The queue deliberately defers callbacks after the
sender returns. It checks synchronous signal-bit publication, clearing and
reusing a bit before the old callback runs, WAIT-to-READY wakeup, exceptions
and terminal-task rejection without a queued callback.
RUN targets without pending exceptions receive their bits without a remote
callback, including when tc_SigWait retains an older wait mask. Wait publishes
its state and checks pending bits under the same task lock. Exception delivery
and waiting targets retain the remote callback path.
Three compiling mutants must fail: delaying bit publication, redelivering
the old bits from the queued callback and interrupting an already-running task.

The Exec/task locks and IPI backend are deterministic host stubs. This is not
an inter-CPU race, real interrupt, queue-exhaustion or guest scheduling proof.
The saved pre-fix run must fail at signal publication; with that defect fixed,
the reuse check also requires the delayed callback not to restore cleared bits.
