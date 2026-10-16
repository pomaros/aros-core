# Wi-Fi panel command parity

The Connect and Other network panel actions are now available as `WIFI CONNECT`
and `WIFI OTHER`, and discoverable through `COMMANDS`. Both use the same guarded
action function as the buttons. Connect uses the current panel fields, rejects
an empty network name and unavailable/busy workers, and never takes a password
as a command argument. Other clears the fields and opens the panel. The command
reply buffer accommodates the complete network command list including QUIT.

Verification retained under `aros-build-indicators`:

- `wifi-palette-build.log`: focused native Indicator build, exit 0.
- `wifi-palette-probe.log`: focused native probe build, exit 0.
- `wifi-palette-syntax.log`: main and Wi-Fi adapter, Wall/Wextra/Werror, exit 0.
- `vm-027/tcg-01/result.json`: actual UP TCG PASS, empty host/guest-error logs,
  QMP quit exit 0, three-second final observation.
- `vm-027/verification.json`: independent strict replay PASS, existing six
  real input phases and protocol/service ownership checks retained.
- Guest marker `wifi-palette other-opens=1 connect-unavailable=1 commands-complete=1`:
  real command opens the panel, Connect fails without the wireless device,
  command discovery contains both actions and is not truncated before QUIT.

Runtime libraries remain frozen from vm-008 and hash verified by the packager.
This does not qualify a physical Wi-Fi association, RF control, full palette
registry integration, Settings pages, WM15/WM16, SMP or Apple hardware.
