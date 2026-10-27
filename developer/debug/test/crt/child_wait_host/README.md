# Child status and wait admission

Run `python3 run.py --output /absolute/path/outside/source`.

The production ChildStatus and waitpid bodies execute under ASan/UBSan with
modeled task queues and reaping. The lock model requires the parent-task lock
before the exit-port lock, covering both scans. Cases distinguish no child
(ECHILD), active child with WNOHANG (zero), exited child (PID/status and reap),
missing PID, unsupported options/groups and non-ETask callers. Wildcard queries
prefer an exited child when an active child also exists. Two compiling controls
remove wildcard matching or report a nonexistent child as still active.

This does not model scheduler overlap, real DOS child teardown or shared POSIX
process ownership across worker Tasks. Compilation and these host checks do not
qualify native/guest process execution.
