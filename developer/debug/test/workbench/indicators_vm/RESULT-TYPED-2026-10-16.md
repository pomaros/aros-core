# Typed indicator contracts, 2026-10-16

The shared presenter exposes PERCENT, RADIO and LEVEL through strict per-kind
GET/SET commands. Numeric/radio values have an explicit availability bit;
a summary with an unavailable capability produces error 10. SET borrows the
caller until the source confirms a valid payload or times out. Late expired
replies cannot alter state, and newer push events supersede old confirmations.
The forwarding slot and reply port remain alive until ownership returns.

Native Indicator and probe compile/link pass (`typed-native-final.log`,
`typed-probe-02.log`). The strict AArch64 syntax check passes with
`-Wall -Wextra -Werror` (`typed-syntax.log`). The exact-source state, typed
parsers and battery conversions pass ASan/UBSan in `typed-host-02`.

Actual UP TCG `aros-build-indicators/vm-008/tcg-01` passes with QMP exit 0,
empty host log and empty guest-errors/unimp trace. Its independent
`verification.json` replays hashes, measured anchor positions, all five real
input phases, registration lifetimes and command observations. Fake audio
responses verify confirmed SET, invalid confirmation, one-second timeout,
second-request rejection while borrowed, expired-reply suppression and a newer
push surviving an older successful confirmation. Exactly four requests reach
the fake source. Radio checks distinguish absent, healthy-summary-only, off
and on; malformed generic numeric events cannot create a radio value. Battery
PERCENT checks cover unavailable, valid, wrong field, read-only SET and expiry.
Each of the three sequential indicator registrations has one initial surface;
none is recreated while active. `vm-007` passed the preceding typed smoke before
the explicit capability-absence case was added; final evidence is `vm-008`.

This qualifies the protocol bridge and shared presenter on UP TCG. The radio and
audio providers in this fixture are explicit test processes, not physical
devices. Hardware radio control, output selection, Bluetooth persistence, full
WM15 and PWR6 remain unqualified. Battery PERCENT is backed by the existing
cached snapshot adapter; its five-second kernel-sampling limitation remains.
