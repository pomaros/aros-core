# Exchange disable and enable

Disable now closes the indicator panel, selects its static fallback and ignores
AppIcon/PANEL activation. Enable restores input and requests a fresh publish on
the original registration. Source caches continue independently. Queued Wi-Fi
UI actions also require a current source lease before dispatch.

Actual UP TCG proof `aros-build-indicators/vm-030/tcg-01` and independent replay
`vm-030/verification.json`: PASS, `exchange=true`, eight real input phases,
network-negative and actual Control-icon checks preserved. The probe sends
real commodities.library BrokerCommand DISABLE/ENABLE requests. Disable closes
an open panel; an actual QMP glyph click while disabled leaves it closed;
PANEL returns failure; after Enable another real glyph click opens it. Existing
single-registration checks pass. Host and guest-error logs are empty and QMP
exits normally after the final three-second observation.

Focused native builds: `exchange-build.log`, `exchange-probe.log`, exit 0.
This treats the low follow-up in review36 message312; it does not qualify a
title-bar presenter, physical providers, SMP or the full WM15/WM16 gates.
