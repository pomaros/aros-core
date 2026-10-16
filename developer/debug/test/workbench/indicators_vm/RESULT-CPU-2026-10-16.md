# Battery CPU consumers — 2026-10-16

BatterySource now reads bounded top-three CPU accounting on its background task
through task.resource. It copies names and scalar usage while the task list is
locked, retains no task pointer, unlocks before formatting/messaging, and marks
an enumeration exceeding 128 tasks as partial. Names are bounded/sanitized;
zero accounting is explicitly idle-or-unavailable, not measured energy usage.
The panel renders separate consumer rows. TOP/GET TOP use an independent
four-second lease; auxiliary events never renew charge/charger state. Two
outgoing ownership slots isolate CPU and battery replies and remain alive
through shutdown until borrowed messages return.

Native Indicator, BatterySource and extended probe compile/link PASS:
`aros-build-indicators/cpu-native-final-02.log`, `cpu-source-02.log`,
`cpu-probe-02.log`. Strict AArch64 Wall/Wextra/Werror syntax PASS in
`cpu-syntax-final.log`. Exact-source CPU ranking, safe names, bounded output,
wraparound auxiliary expiry, malformed updates and battery independence pass
ASan/UBSan in `cpu-host-final/result.json`.

Actual UP TCG final `vm-012/tcg-01` and independent `verification.json` PASS:
25 live tasks enumerated using the production resource adapter, auxiliary
roundtrip/expiry/invalid-payload rejection verified, and fresh CPU data leaves
expired battery PERCENT unavailable. All preceding typed-command and five real
input phases still pass, with four paced battery frames, exact glyph anchor,
normal QMP exit 0 and empty host/guest-error logs. Final program inputs are
frozen in vm-012/inputs. vm-011 passed the preceding single-line presentation.

vm-009 is retained FAIL: the concurrently rebuilt shared VM SDK supplied a
changed workbench.library, causing missing geometry before the input phases.
The packager now supports --runtime-from, verifies the prior base bundle and
every frozen library hash, and reuses those exact runtime libraries. vm-010 is
a retained packaging assertion failure while implementing that check. vm-012
uses the previously qualified vm-008 libraries and matching base bundle.

Scope: real task.resource enumeration and presenter/protocol on UP TCG. This
is not a physical battery test, SMP qualification or complete WM15/PWR6.
The kernel still samples battery hardware every five seconds; charger events
within two seconds and the full Wi-Fi/Bluetooth/sound panels remain pending.
