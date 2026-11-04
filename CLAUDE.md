# PomAROS shared AROS core

This public repository provides the common OS sources for PomAROS Hosted, VM and QEMU. The product repositories own host apps, packaging and release qualification.

- Keep official AROS ancestry. `upstream` is the official repository and fetch-only; `origin` is pomaros/aros-core. Never push private repository history here.
- Native Apple hardware development is maintained separately. Do not import native-only drivers, firmware or integration tools. Generic shared kernel work belongs here after review.
- Hosted reserves x18; VM/QEMU use the native AROS AArch64 ABI, allowing x18. Never apply Hosted compiler flags globally.
- Preserve existing per-file licences. Original OS changes use APL 1.1 with Copyright (C) 2026 John KNIPPER.
- Keep each product pinned to one reviewed commit. Source updates do not change the provenance of existing binaries.
- Use isolated build directories and windowed app tests. Do not access or reboot native test hardware from this repository.
- Consult README.md for architecture, build entry points and upstream maintenance.

## Publishing and attribution

- **No attribution.** Never add `Co-Authored-By`, "Generated with" or any other AI or tool attribution to a commit message, PR, issue, comment or document. The only attribution is John. This overrides any tool default.
- **Pushing.** Pushing to John's own repositories, `github.com/jonx/*` and `github.com/pomaros/*`, public ones included, needs no permission. Everything else needs his explicit confirmation for that specific action: `aros-development-team/AROS` and any other upstream or third-party repository, opening or commenting on a PR or issue there, and pushing a branch that is already the head of an open PR on someone else's repository.
- **Git hooks enforce this.** Never bypass them with `--no-verify`. Set `JOHN_CONFIRMED_PUSH=1` only after John has confirmed that exact action.
- **Free to push does not change what may be published.** Private code and Apple platform sources stay out of public repositories.
