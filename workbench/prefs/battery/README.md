<!-- Copyright (C) 2026 John KNIPPER <code@jkn.me>. -->
<!-- Distributed under the AROS Public License, version 1.1. -->

# Battery

`BatteryEditor.mcc` is the read-only Battery page in Settings and the editor
used by `SYS:Prefs/Battery`. It reads `power.library` version 1 and displays
charge, mains/battery, charging, current, capacity and the library's time
estimate. Unknown fields, stale samples and a missing provider are reported as
unavailable; a virtual machine never acquires simulated battery values.

The page subscribes to source events while set up and replies to every borrowed
message. A two-second MUI timer reads the published sample to detect staleness
and current-only changes, which source notifications do not report. Cleanup
removes handlers, unregisters the subscription, drains its port and closes the
library. Refresh reads the same published sample. No page operation calls SMC.

This page does not configure charge limits or low-battery actions and does not
list CPU consumers. Those controls require their service interfaces. It has no
preferences file, and Use, Save and Test are unavailable.
