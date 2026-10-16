# Battery subscriber service — 2026-10-16

BatterySource reads only the sole producer's kernel cache, never SMC, at250ms.
The public INDICATOR.SOURCE.battery port supports GET, STATUS, SUBSCRIBE,
UNSUBSCRIBE, EVENTS on/off and QUIT. Charge/adapter/estimate events publish on
change and at most a two-second unchanged heartbeat while the sample is valid.
There are at most eight stable subscriber slots plus one separate CPU slot.
Inactive slots cannot be reassigned while any old/expired message remains
borrowed; failures retain dirty state and retry no faster than250ms. Data
changes coalesce while held. Names are bounded63-byte ASCII alphanumeric/._-.

The UI explicitly subscribes asynchronously, retries an absent/stale service
registration at two seconds, and unsubscribes before shutdown. It never polls
GET per frame. There are no implicit subscribers. Each task retains ports and
messages until ownership returns. CPU samples and their lease stay independent
of EVENTS off and battery state. Hardware sample age greater than ten seconds
makes GET fail and stops renewing the UI's four-second lease.

Native source/UI/probe compile-link PASS (battery-subscribe-source.log,
battery-subscribe-ui.log, battery-subscribe-probe-final.log). Final strict
AArch64 Wall/Wextra/Werror syntax PASS (battery-subscribe-syntax-final.log).
Exact-source ASan/UBSan battery-service-host-final PASS covers full capacity,
idempotent subscription, retired-but-borrowed slot rejection, reuse only after
ownership return, malformed/long names, retry/heartbeat boundaries and clock
wrap, along with the existing state/CPU tests.

Actual final UP TCG vm-023/tcg-01 and independent verification.json PASS.
The production source is absent-data, not a fake battery: GET errors, event
switches alter STATUS only, a real indicator registers one subscription and
removes it on quit, all eight slots fill, a ninth fails, unsubscribe allows
replacement and source shutdown removes its port. Typed SET adversarial cases,
real Mute click, Wi-Fi worker lifecycle, 25-task CPU enumeration, five standard
input phases and held-provider responsiveness still pass. QMP exits normally0,
raw host/error logs are empty, and the final observation lasts three seconds.
Native Indicator/BatterySource/SoundSource bytes match frozen vm023 inputs.

vm019/020/021 passed preceding service versions. vm022's guest assertions and
QMP smoke passed but independent replay FAILED: concurrent TILE tracing split
the outside-phase STATS witness. The retained verification-failure.txt explains
it; it is not an independent PASS. The probe now emits each bounded UP witness
under Forbid/Permit; production UI/device scheduling is unchanged. Finalvm023
passes the unchanged strict phase checks without weakening log validation.

The separately delivered Apple period change is4fa699687e; default5000ms and
configurable250..60000ms, with one SMC producer and independent NIC poll. The
VM uses its established frozen kernel substrate and does NOT qualify that
Apple kernel change. Physical charger latency/ten cycles, fresh telemetry
through EVENTS off, HIDD facade, RF switching/WPA, N3 persistent pairing,
physical audio/output routing, SMP and full WM15 remain unqualified.

Reviewed-base reconciliation
----------------------------

After the API stack was merged upstream, private mergea6647d1223 reconciled
this branch with apple-silicon e6559ed4ae. Old inherited Settings conflicts
were resolved by taking exactly the reviewed e655 tree for workbench/libs/settings
and developer/debug/test/settings; their diff against e655 is empty. No
indicator/probe source changed in that merge. The four focused native builds
pass in latest-base-{ui,battery,sound,probe}-final.log and all four executable
bytes remain identical to their frozen vm023 inputs, preserving that proof.
The auto-merged coldstart_proof.c was also recompiled and kernel.resource linked
successfully (latest-base-kernel-arch.log and latest-base-kernel.log); one
pre-existing pointer-type warning remains at coldstart_proof.c1163.

This is a private branch reconciliation, not a merge into apple-silicon or a
hardware deployment. Reviews36/38 are pending. The HIDD prerequisite is offered
to the power implementation owner as task20; N3 pairing, RF/routing driver
capabilities and physical gates remain dependencies of the full task13.
