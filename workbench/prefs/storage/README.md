<!-- Copyright (C) 2026 John KNIPPER <code@jkn.me>. -->
<!-- Distributed under the AROS Public License, version 1.1. -->

# Storage

`StorageEditor.mcc` is the read-only Storage page in Settings and the editor
used by `SYS:Prefs/Storage`. Rescan volumes copies mounted volume names from
the DOS list. Selecting a volume or Refresh space reads DOS `Info()` and shows
capacity and available space in MiB, plus read-only/validating status. Missing
volumes and unavailable space information have explicit states.

The DOS list lock is released before any filesystem packet. Space queries are
synchronous, like DOS `Info`; an unresponsive filesystem can delay the query.
No automatic filesystem scan runs on a timer. This page does not modify volumes
or manage AFS+ snapshots. It has no preferences file, and Use, Save and Test are
unavailable.
