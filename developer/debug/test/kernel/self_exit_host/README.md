# Apple SMP self-exit handoff

Run `python3 run.py --out /absolute/new/directory` on the HOST with Clang and
AddressSanitizer/UndefinedBehaviorSanitizer. Each subprocess is bounded; cases
compile sequentially. macOS LeakSanitizer is unavailable and is disabled.

The tests compile the actual slot source and extract the actual Apple
`cpu_Dispatch` and API queue adapter bodies with narrow HOST scheduler/register
shims. Normal and idle destinations publish only after replacing the old state;
the callback frees task storage immediately. Wrong CPU/task, duplicate/reentrant
admission, missing interrupt exclusion, nonterminal tasks and API generation
mismatches are refused. Four compiled runtime mutants must fail the same tests.
The idle branch rescans readiness once only after PUBLISHED; EMPTY does not
rescan. A publisher making the service ready must select it before returning,
while nonlocal/no eligible service remains idle after the bounded second pass.
The shims do not qualify the real scheduler, SMP ordering, EL1 callback execution,
interrupt masking, module linkage or hardware.

Run `python3 idle_jit.py --out /absolute/new/directory` for the idle JIT
regression. It extracts actual mode/flag/mask definitions, the idle prefix
through CPU-state publication, the dispatcher and `context_of`/`JitRestore`.
The platform permission selection is observed through a callback; CPU, Task and TLS
environments are HOST facades. The idle body with its neutral restore removed must fail the exact permission
assertion. Idle selects the executable EL0 JIT table without changing the saved
writer flag, which its next dispatch restores. NONE and RWX remain no-ops.
This verifies shared control flow, not platform registers, TLS registers or SMP execution.

The Apple SMP API must be rebuilt as a coherent version-4 kernel/Exec cohort.
The added entry follows the existing `spin_trylock`; size/version checks reject
old tables, including at startup. CPU_BIND already precedes Exec startup; an
unbound CPU at dispatch is a contract violation, not a successful empty drain.

Self-removal alone is covered. Remote RemTask still lacks a proved remote CPU
quiescence boundary; this change must not be cited as making remote reclamation
safe. Context/ETask cleanup ordering is inherited; Task and its MemEntry stack
are retained until publication. Non-Apple and UP RemTask keep their existing
path. The normal task JIT and TLS restore sequence is retained; idle explicitly
neutralizes the outgoing task's EL0 JIT write permission.
