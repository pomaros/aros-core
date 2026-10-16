# Indicator / Control icon integration

Actual UP TCG run `aros-build-indicators/vm-028/tcg-01` and independent replay
`vm-028/verification.json`: PASS, `control_icons=true`, `wifi_palette=true`,
six real input phases, empty host/guest-error logs, QMP quit exit 0.

The packager accepts an explicit `--control` executable. This run used
`aros-build-tiles-indicator-api/bin/apple-aarch64/AROS/System/TilesControl`;
its exact bytes are frozen and hashed alongside all other program inputs in
`vm-028/esp/provenance.json`. Runtime libraries are the verified vm-008 set.

The guest runs that actual installer into `RAM:IndicatorTiles`, loads the real
serialized network/sound/battery DiskObjects, and checks their GET ToolTypes
against the indicator ports. It uses the installed battery icon as the fallback
for its persistent battery AppIcon (with the witness position changed to 64,64).
The regular panel/input, lease, protocol, subscription and shutdown scenario
then passes. The icon installer and public indicator protocol are compatible;
this is not a proof of the entire lifted Control presenter or all providers.

Native probe build: `control-icons-probe.log`, exit 0. Python packaging syntax
check and git whitespace check pass. The README now gives explicit per-user
start commands using installed Control icons; no boot startup file is changed.

Physical charger, Wi-Fi and audio behavior, persistent Bluetooth pairing,
Settings/palette registry integration, SMP and full WM15/WM16 remain unqualified.
