<!-- Copyright (C) 2026 John KNIPPER <code@jkn.me>. -->
<!-- Licensed under the AROS Public License (APL), Version 1.1. -->

# Registers survive preemption

`register_stress.c` builds `RegisterStress [ROUNDS [SPINS]]`: four threads
fill x0-x28, x30 and v0-v31 with their own pattern, spin long enough for the
timer to preempt them, then store and compare every register, TPIDR_EL0 and a
`thread_local`. A round in which another thread ran is counted as preempted.
A context switch that loses or mixes up a register shows another thread's
pattern. It prints `REGSTRESS result=PASS` and the preempted round counts.
