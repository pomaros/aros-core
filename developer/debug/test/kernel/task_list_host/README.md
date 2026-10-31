<!-- Copyright (C) 2026 John KNIPPER. AROS Public License (APL) Version 1.1. -->

# Task-list critical sections

The runner extracts the production list lock, unlock, cleanup, removal and
lookup helpers. Modeled lists, interrupt nesting, spinlocks and allocation
callbacks check that local preemption is excluded before acquisition and that
reclamation runs after unlock. A nested caller's interrupt nesting is preserved.
Active iterators retain tombstones; new-list removal and deferred list merging
are exercised. Sanitizers check memory access and explicit allocation tracking.

Three semantic mutations must hit their expected assertions: an unmasked
owner, freeing while holding the lock, and ignoring iterator retention.

```sh
python3 developer/debug/test/kernel/task_list_host/run.py --out /ABS/FRESH
```

This is HOST evidence for extracted helper logic. It does not execute task
scheduling, resource vectors, interrupt masking, remote task deletion or SMP.
Target compilation and a coherent native churn/iteration workload remain
separate gates. Raw Task/TaskListEntry return-value lifetimes are not extended
by this change; this is not a replacement for caller retention contracts.
