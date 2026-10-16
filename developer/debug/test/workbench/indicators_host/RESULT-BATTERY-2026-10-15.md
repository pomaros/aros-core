# Battery snapshot adapter checks, 2026-10-15

`BatterySource` compiles and links natively with the shared AArch64 toolchain
(`aros-build-indicators/battery-02.log`). A strict syntax pass uses
`-Wall -Wextra -Werror` (`syntax-battery.log`). The source reads the kernel's
stable battery publication; it never issues SMC I/O itself. Each asynchronous
event remains owned until the destination replies.

`aros-build-indicators/host-03/result.json` records the exact parser, state and
battery conversion sources and fixture hashes, under ASan/UBSan. Assertions
cover the discharge estimate, adapter/charging formatting, parsed plug/gauge
state, invalid percentages/capacity, INT32_MIN current, zero current and small
output buffers. Parser validation, lease expiry, clock wrap and screen-edge
placement remain in the same fixture. No sanitizer errors were reported.

These checks qualify the conversion logic and native build only. The kernel
hold refreshes the battery snapshot every five seconds (`coldstart_proof.c`,
`next_smc`); the adapter rejects snapshots ten seconds old. PWR6's charger
subscription and a measured two-second glyph transition require additional
service work and physical evidence. The panel has no top-consumer list or
implemented settings application. B7 and WM15 are partial.
