# Task-local DOS path resolver

Run `python3 run.py --output /absolute/new/directory`, adding `--tsan` for
ThreadSanitizer. The actual registry, IoErr/SetIoErr bodies and Open dispatch
prefix run with modeled Task storage and native operations. Concurrent callers
register distinct task-local objects. Tests check error isolation, registration
refusal, recursion and no borrowing of parent Task storage. Six compiled
mutations must fail: lost dispatch, lost error, parent-storage lookup, skipped
policy, illegal Task fallthrough and unheld policy registration.

SetTaskPathResolver230 (DOS 50.86) installs a caller-owned resolver instance in
the current Task. Its code, data and object must remain valid until removal;
an active callback cannot replace its registration. The callback receives
borrowed arguments valid until it returns. Path operations dispatch before
native Process-field access. Ordinary Tasks keep errors in their resolver;
Processes retain pr_Result2. No resolver preserves the original path behavior.
The operation enum includes ReplaceObject so replacement follows the same
context as other path operations; a provider must implement the operations it
admits or report an explicit error.

These fixtures execute Open's dispatch prefix, not every filesystem operation.
Compiling all changed translation units checks their current interfaces;
actual handler, POSIX binding, SMP guest and provider unload behavior require
separate qualification. The resolver does not itself own DOS locks or handlers.

Version2 providers supply a pure same-Task acceptance policy. A declined
operation falls through only in a real Process; a plain Task receives a
refusal before native Process fields can be read. Policy and invocation both
hold registration active. The gate exercises both boundaries and rejects
dropped policy and illegal Task fallthrough mutations.
