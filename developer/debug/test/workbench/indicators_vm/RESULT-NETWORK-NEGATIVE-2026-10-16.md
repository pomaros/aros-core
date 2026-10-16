# Unavailable network panel

The network panel hides its Wi-Fi controls while the provider lease is absent
or expired, leaving the service notice and Settings button. Command discovery
also drops the Wi-Fi actions; direct action requests fail. WIFI STATUS remains
a diagnostic. Fresh data restores the controls without replacing the AppIcon.

Retained evidence in `aros-build-indicators`:

- `wifi-negative-build.log` and `wifi-negative-probe.log`: native focused builds,
  exit 0; `wifi-negative-syntax.log`: cross clang Wall/Wextra/Werror, exit 0.
- `vm-029/tcg-01/result.json`: actual UP TCG PASS, six real input phases,
  QMP normal exit 0, empty host and guest-error logs, final three-second settle.
- `vm-029/verification.json`: strict independent replay PASS, including new
  `wifi_negative=true`, existing Control icons and palette checks.
- `wifi-unavailable.ppm`: inspected screenshot showing Service unavailable and
  Settings alone. A healthy fixture subsequently restores the full Wi-Fi panel.
- Guest marker `wifi-negative settings-only=1 lease-expired=1 recovered=1 same-registration=1`:
  checks initial absence, four-second lease expiration while open, rejected
  actions/discovery and recovery. Existing registration-count replay remains.

The provider disappearance is a controlled fixture; physical network-stack
shutdown, RF control and association remain unqualified. This completes this
presenter behavior, not the full WM15 hardware gate.
