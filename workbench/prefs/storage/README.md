<!-- Copyright (C) 2026 John KNIPPER <code@jkn.me>. -->
<!-- Distributed under the AROS Public License, version 1.1. -->

# Storage

`StorageEditor.mcc` is the Storage page in Settings and the editor used by
`SYS:Prefs/Storage`. Rescan volumes copies mounted volume names from the DOS
list. Selecting a volume or Refresh storage reads DOS `Info()` for capacity,
available space and read-only/validating state. The handler's `CAPABILITIES`,
`HEALTH` and `INFO_JSON` reports supply 64-bit capacity, space available after
reservations, recorded device and corruption faults, checkpoint generation,
pending cleanup and the volume identity. Unsupported handlers display an
explicit diagnostics-unavailable state. An oversized or incomplete JSON report
supplies no identity.

The snapshot list uses the handler's bounded pagination. Delete selected
snapshot holds a lock on the selected mounted volume while confirming its ID,
sends one deletion request and
refreshes the inventory. Historical access requires the handler's explicit
backup authorization policy; a refusal is shown with its DOS error. Open
readers prevent deletion until they close. A list reaching the 8192-entry
UI bound reports that the inventory is incomplete.

The DOS list lock is released before every filesystem packet. Queries are
synchronous, like DOS `Info`; an unresponsive filesystem can delay the query.
No automatic filesystem scan runs on a timer. The page has no preferences
file, and Use, Save and Test are unavailable.

Run `python3 workbench/prefs/storage/test_report.py` from the core checkout.
The production report formatter and snapshot packet helpers are exercised
with a synthetic transport under ASan/UBSan: 64-bit capacity, health faults,
unsupported reports, partial JSON, paged snapshots, malformed continuations,
selected deletion and access refusal.
