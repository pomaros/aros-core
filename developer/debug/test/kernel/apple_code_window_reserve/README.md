<!-- Copyright (C) 2026 John KNIPPER <code@jkn.me>. -->
<!-- Licensed under the AROS Public License (APL), Version 1.1. -->

# MEMF_RESERVE on the running system

`reserve_probe.c` builds `ReserveProbe`, a program for the running system:

- `KrnAllocPages(NULL, 1 GiB, MEMF_PUBLIC | MEMF_RESERVE)` reserves code-window
  pages and costs no memory;
- `KrnSetProtection` with access commits the pages it covers, cleared, and
  they keep what is written; removing all access gives them back, and a later
  commit returns cleared pages;
- `KrnFreePages` gives everything back. It prints `RESERVE result=PASS`.

`ReserveProbe FAULT` reads a page never committed: the task must crash with
the kernel's report of a data read from an unmapped window address.
