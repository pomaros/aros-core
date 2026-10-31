<!-- Copyright (C) 2026 John KNIPPER <code@jkn.me> -->
<!-- Licensed under the AROS Public License (APL) Version 1.1. -->

# Restricted wait scheduler fixture

Run `python3 run.py --out /absolute/fresh/path/outside/source`.
The runner extracts the production scheduler park/wake functions, both outgoing
and incoming park hooks, channel wake/cancel helpers and scalar INFO/WAIT/WAKE
cases. Task locks, scheduler lists, current task and IPIs are modeled on HOST.

The baseline exercises wake before completed context save, wake after parking,
repeated wake, native signal followed by re-parking, wrong context/domain/channel,
ordinary native and removed tasks, independent native freeze ownership and a remembered wake on thaw, stale
wake generations, exhausted generations and domain-close wakeup. Nine separately
compiled mutations must fail their intended assertions under sanitizers.

This is deterministic interleaving coverage, not concurrent SMP execution.
It does not qualify hardware register save/restore, IPIs, list lock fairness,
exception return or task retirement. The guest workload must test two CPUs,
peer/controller wake, cancellation while parked and full retained-state drain.
Timeouts and suspend/resume have separate contracts and are not supplied here.

The controller state query distinguishes admitted waits from contexts actually
on the wait list; a task made ready by a native signal is not reported parked.
