<!-- Copyright (C) 2026 John KNIPPER <code@jkn.me>. -->
<!-- Licensed under the AROS Public License (APL), Version 1.1. -->

# Exclusive atomics survive preemption

`atomic_stress.c` builds `AtomicStress [COUNT]` (`-march=armv8-a`, so the
atomics are LDXR/STXR loops): four threads increment one counter through an
exclusive pair and another through compare-and-swap. A context switch that
leaves the exclusive monitor armed lets a stale store-exclusive succeed and
loses increments. It prints `ATOMIC result=PASS` when both counters reach
four times COUNT.
