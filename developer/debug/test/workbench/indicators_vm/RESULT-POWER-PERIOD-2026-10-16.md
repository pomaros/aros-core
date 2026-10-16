# Single SMC producer period — 2026-10-16

Following Claude's thread24 message252, the existing sole SMC producer in
coldstart_proof.c accepts the decimal chosen-node property
`aros,smc-period-ms`. Missing/out-of-range values retain the established5000ms;
valid values were250..60000ms in the initial delivery. Review38 subsequently
limits the property to250..5000ms to retain headroom below the ten-second
subscriber cache lease. Tick conversion rounds up and never schedules a
zero-tick loop. Each next sample is scheduled after the previous sample has
completed, avoiding catch-up loops under slow SMC I/O. The NIC status poll has
its own unchanged five-second cadence. No second SMC reader is introduced.
The selected period is printed as `[KI] SMC period ms <hex>` for retained
hardware evidence. A1000ms period is a candidate for battery event-latency work,
not a qualified two-second guarantee.

Native architecture compilation PASS (power-period-arch-02.log), followed by
kernel.resource compile/link PASS (power-period-kernel-quick.log). The latter
retains three existing printbacktrace format warnings. This build initially
lacked generated SMC/PMGR/DCP headers; resource include generation completed
before the successful build. No header/API layout changes were required.
The produced kernel.resource SHA256 is:

`57dfd137db15e49ce338adffe5b7a6a0099a70b186248ec9aae05a3f85adb79d`

No Apple hardware boot, configured-period runtime measurement or cable cycle
was run. PWR6 requires the service event path and ten real plug/unplug cycles,
and remains open. The cached battery source conservatively expires hardware
samples after ten seconds, so periods longer than that deliberately result in
unavailable intervals; use a period no greater than the default for live UI.
