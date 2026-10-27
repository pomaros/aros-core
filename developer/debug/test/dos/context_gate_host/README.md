# Synchronous DOS context gate

Run `python3 run.py --output /absolute/new/directory`, adding `--tsan` for
ThreadSanitizer. The actual RunWithContext source executes with pthread-backed
Process, signal, directory-lock, packet and allocation adapters. It checks
owned directory copies, callback results/errors, allocation/admission rollback,
lock release before completion and waiting for the final notification before
reusing the signal/request. Four compiled mutations must fail at runtime.

RunWithContext229 (DOS 50.86) calls a callback in a temporary real Process using
owned cwd/progdir locks and the supplied resident DOS base. The callback must
return normally and preserve the helper's directories. It cannot assume a C
runtime or borrow its caller's Process/console state. Data and callback code
remain caller-owned until return. NULL cwd resolves the boot root; NULL progdir
inherits cwd. Final completion runs in resident DOS code after the callback and
owned directory releases. Forced removal/cancellation of a waiting caller is
outside this synchronous contract.

Host adapters do not qualify actual CreateNewProc, Signal/Wait ordering on SMP,
module linkage or a guest. Regenerate the common DOS interfaces and rebuild the
runtime/consumer cohort before guest qualification.
