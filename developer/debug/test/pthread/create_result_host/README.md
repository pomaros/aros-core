# pthread creation-result ownership

Run `python3 run.py --output /absolute/new/directory` with host Clang. The
fixture extracts the complete current pthread_create body and supplies narrow
allocation, task creation and semaphore facades. It deterministically schedules
record retirement or slot reuse at the semaphore release boundary, checking
that a successful immediate detached exit still reports success and that failed
creation cannot clear a replacement owner's record. It also checks allocation
refusal, slot exhaustion and invalid arguments. ASan/UBSan execute the positive
and two compiled runtime mutations; both mutations must fail their assertions.
Temporary executables are removed automatically, including on test failure.

The fixture does not run StarterFunc, real task scheduling, C-runtime admission,
module unload or guest code. Its public API names are renamed in the host
facade so the host sanitizer cannot interpose an incompatible pthread ABI.
