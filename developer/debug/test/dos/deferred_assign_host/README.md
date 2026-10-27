# Deferred assign resolution

Run `python3 run.py --output /absolute/new/directory` with host Clang. Actual
GetDeviceProc deferred-resolution helpers and the actual generation engine run
against deterministic namespace, handler, allocator and security facades.
Every handler operation asserts that both DOS guards are released. Replacements
and deletions free the old node/text before consuming the copied target. A
second mutation during discarded-lock release checks that the next iteration
refetches the node after cleanup. Tests also cover stale errors, kind changes,
bounded retries, existing primary locks, OOM and per-user expansion/create paths.

Four compiled mutations must fail: waiting under READ, borrowing target text,
accepting a stale generation and reusing a node captured before cleanup. The
latter two lifetime mutations are checked by ASan/UBSan. Temporary executables
are removed automatically. The fixture does not execute the complete public
GetDeviceProc, real DOS semaphores, security server, handler startup or SMP guest.

The helper enters and returns with ALL READ held. It snapshots name/target under
ENTRY READ using allocation without expunge callbacks, resolves outside both
guards, and revalidates before promotion or return. A nonbinding result owns its
native lock and exposes no borrowed DosList node; only DVPF_ASSIGN results may
be iterated. Caller-owned directory locks, handler ordering and callback/code
lifetimes keep their native DOS contracts. Callers must not retain outer DOS
namespace guards across operations that may need a handler. This change does
not make bound-assign iteration or device/volume metadata globally lifetime-safe.
