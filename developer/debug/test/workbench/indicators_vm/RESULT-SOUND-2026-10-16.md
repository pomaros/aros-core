# Sound controls and A2DP controller adapter — 2026-10-16

The sound panel now offers a slider and Mute. Changes coalesce behind one
borrowed control slot and never optimistically change the authoritative state
or glyph. Missing/stale volume disables controls; queued changes expire with
that lease. Output routing/enumeration remains unavailable.

SoundSource exposes the existing shared bta2dp.device controller through the
typed service port. QUERY requires a configured stream, then GETVOLUME supplies
the real controller level. SET validates its range, uses SETVOLUME and returns
a matching readback. It does not own AHI's AudioCtrl or a PCM stream. Device
calls stay on the source task; the UI retains its nonblocking timeout and late
reply quarantine. Service startup, command drain and shutdown retain all
outstanding message ownership.

Native Indicator, SoundSource and probe builds PASS in sound-native-02.log,
sound-source-final.log and sound-probe-02.log. Wall/Wextra/Werror cross syntax
passes in sound-syntax.log. Final UP TCG vm-018/tcg-01 and independent replay
verification.json PASS with QMP exit0, empty host/error logs and three-second
final observation. A real QMP pointer click on measured Mute coordinates sends
SET LEVEL0 to an explicit fake source; its reply confirms GET LEVEL0, six total
requests including the five existing adversarial cases. Coherent frozen
runtime libraries are reused. The production SoundSource separately starts
without a device, reports unavailable, rejects GET/SET rather than inventing
zero, then exits completely after QUIT. Sound panel screenshot was visually
inspected in vm017; final vm018 uses the same presenter. vm016 and vm017 passed
preceding panel/backend-lifecycle versions; final binary inputs are vm018.

The actual controller's SETVOLUME writes nh_Volume and signals its unit task;
GETVOLUME reads that shared logical level immediately. Readback is a controller
confirmation, not proof of a completed PCM gain change or a matched AVRCP
accepted response. Physical A2DP/AVRCP, native jack/speakers, output selection,
SMP and full WM15 remain unqualified. The fake volume source and absent-device
backend fixture cannot qualify those hardware gates.
