# Wi-Fi panel and review corrections — 2026-10-16

The shared network indicator now embeds the existing Prefs/WiFi worker and
locale code. No second Prefs executable is introduced. The borderless panel
contains scan, known-first network rows with protection/signal/joined labels,
network/password fields, optional protection for manual networks, connect,
other-network, disconnect and shared Settings. Configuration and device access
remain entirely in the worker, including Wireless.prefs rollback/reused keys.
The UI starts it without waiting, keeps independent list strings/snapshot,
passes one owned message at a time and waits asynchronously for its final reply
and DOS child-exit notification before freeing shared memory/code. Password
fields clear on submission and returned message keys are zeroed before copying.

LINK on/off is a separate association capability; it draws the Wi-Fi glyph
without inventing an RF radio value. GET RADIO remains unavailable without an
explicit RADIO sample. bwfm_dev.c1858–1869 only sets software online state,
tries association and flushes reads: it has no verified RF-off command. The
panel reports radio control unavailable. It uses the configured existing
AROSTCP/WirelessDevice; selecting among multiple adapters is not implemented.

Review thread26 F1–F5 addressed: SET confirmation must match the requested
value; class-zero/drop messages no longer toggle; second-click dismissal has
no press/release timeout; commodity children/filter errors are checked; TILE
CHANGED failure/timeout retains the dirty bit and retries at most once/second.
F6 retained borrowed-message shutdown is explicitly documented; F7 confirmed
but superseded operations retain the newer state and are documented.

Verification retained under aros-build-indicators:

- Native Indicator and probe compile/link PASS (wifi-native-final-03.log,
  wifi-probe-panel.log); final new sources cross syntax Wall/Wextra/Werror PASS
  (wifi-syntax-final-02.log). Catalogs initialized at pinned355d6c5 and generated.
- Actual-source host ASan/UBSan state/CPU suite PASS (wifi-host-01), including
  LINK capability without RADIO availability and malformed/wrong-kind LINK.
- Actual UP TCG final vm-015/tcg-01 plus independent verification.json PASS;
  matching frozen vm008 runtime libraries, normal QMP exit0, empty host/error
  logs and three-second final observation. Five real input phases include a
  1.1-second held second glyph click. Wrong valid SET43→LEVEL5 is rejected,
  GET42 stays unchanged; invalid/timeout/late/newer-push cases still pass with
  exactly five fake-source requests. CPU resource enumerates25 live tasks.
- Production Wi-Fi worker starts, handles missing configuration, keeps UI
  commands responsive, rejects unsupported scan/disconnect, then exits fully
  after QUIT. The actual Wi-Fi panel opens anchored116,174; screenshot inspected
  and text input/list frames corrected. vm013 and vm014 also passed earlier
  protocol/panel versions; final screenshot is vm015/wifi-panel.png.

This qualifies the real worker's absent-device lifecycle and UI integration on
UP TCG. It does not qualify physical scan, WPA association, a blocked driver's
recovery, RF switching, SMP, PWR6, full WM15 or Bluetooth/audio hardware.
Those gates remain open; no simulated radio or fabricated volume is substituted.
