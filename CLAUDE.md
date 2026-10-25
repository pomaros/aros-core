# PomAROS shared AROS core

This public repository provides the common OS sources for PomAROS Hosted, VM and QEMU. The product repositories own host apps, packaging and release qualification.

- Keep official AROS ancestry. `upstream` is the official repository and fetch-only; `origin` is pomaros/aros-core. Never push private repository history here.
- Native Apple hardware development is maintained separately. Do not import native-only drivers, firmware or integration tools. Generic shared kernel work belongs here after review.
- Hosted reserves x18; VM/QEMU use the native AROS AArch64 ABI, allowing x18. Never apply Hosted compiler flags globally.
- Preserve existing per-file licences. Original OS changes use APL 1.1 with Copyright (C) 2026 John KNIPPER.
- Keep each product pinned to one reviewed commit. Source updates do not change the provenance of existing binaries.
- Use isolated build directories and windowed app tests. Do not access or reboot native test hardware from this repository.
- Consult README.md for architecture, build entry points and upstream maintenance.
