# Review36 corrections — 2026-10-16

H1: the Wi-Fi worker now uses configurable WifiMainSignal. The asynchronous
indicator allocates a private bit, sets it before CreateNewProc, waits for it
and the final DOS child notification, then frees it. No SIGF_SINGLE is cleared
or consumed by the indicator; it is reserved for contended Exec semaphores.
The original synchronous Prefs caller keeps the established default.

M1: Apple has no producer of per-task CPU usage yet. BatterySource explicitly
labels all-zero placeholders `Per-task CPU accounting unavailable (Apple kernel)`.
The existing portable ranking and enumeration work remain valid, but do not
qualify actual Apple CPU percentages. Task21 is offered separately for real
CPUTime/CPUUsage production, including missing Switch/Dispatch accounting and
safe sampling context. No fake percentage or measured-idle claim is substituted.

L1: provider UI groups are created and attached after a valid ApplicationObject.
L2: a failed connect send zeroes the copied key, in addition to normal reply
clearing. L4: SET RADIO is advertised only with an explicit provider port.
L3: a dedicated Zune event observer copies deactivation coordinates/timestamps;
a precise Commodity SELECTDOWN filter copies original input timestamps. A
50ms event-creation correlation, excluding clicks delivered inside the panel,
distinguishes a real glyph press from programmatic focus loss. Its budget is
independent of press/release duration and later UI scheduling delay. No borrowed
message/screen pointer is retained and no graphics run on the input task.

Native Indicator/BatterySource/probe builds PASS (review36-event-build.log,
review36-battery.log, review36-probe.log). New-source strict cross syntax
Wall/Wextra/Werror PASS in review36-syntax-final-02.log.

Actual UP TCG vm-026/tcg-01 and independent verification.json PASS with six real
input phases. A second glyph press held1.1s closes; a separate real Intuition
window activates without a mouse press, the panel deactivates, and the next real
glyph click opens it. The actual production Wi-Fi worker reports allocated
wake134217728/reserved0, stays responsive without configuration and exits fully.
COMMANDS omits SET RADIO while its source is absent. CPU enumeration, explicit
battery subscriptions/capacity/lifecycle, matching/mismatched/expired/late SET,
Mute QMP click and settings-held isolation still pass. Normal QMP exit0, empty
raw host/error logs and three-second final observation are retained.

vm024 and diagnosticvm025 are retained FAIL: using the actual IDCMP Qualifier
alone failed the long-held click. The observer proved deactivation arrived at
the right coordinates with q32768 and no LEFTBUTTON; copied original Commodity
time then resolved the missing cause information in vm026. No log checker or
expected panel state was weakened. Historical CPU reports qualify enumeration
and parser/ranking logic only, not an Apple CPU-usage producer.

SMP ordering, physical RF/WPA/A2DP, Power HIDD, per-task CPU production and full
WM15 remain open. The final review verdict belongs to the reviewer.
