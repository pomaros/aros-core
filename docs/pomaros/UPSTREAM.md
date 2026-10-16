# Repository ownership and upstream maintenance

`pomaros/aros-core` owns the shared operating-system sources. The product repositories own macOS front ends, host packaging and distribution tooling. Native-only hardware drivers and unpublished native integration belong in the separate private native development repositories. Do not copy private repository history into this public fork.

## Git remotes

A clone uses `origin` for this public fork and `upstream` for official AROS:

```sh
git clone https://github.com/pomaros/aros-core.git
cd aros-core
git remote add upstream https://github.com/aros-development-team/AROS.git
git remote set-url --push upstream disabled://public-aros-push
git fetch upstream
```

`main` is the reviewed PomAROS source branch. Official AROS uses `master`. The public fork retains official ancestry; its initial curated changes are based on upstream commit `38ba2794c1815c84f5f8d6303aacaec04af6ecf2`. This baseline preserves the previously qualified common source base. It does not claim to include every later upstream change.

## Receive upstream changes

Fetch upstream, create an integration branch from `main`, and merge `upstream/master`. Resolve changes by subsystem, preserving the Hosted x18 reservation and unrestricted guest ABI. Build both core configurations and qualify both VM backends before updating product locks. Merge the reviewed integration branch into `main`; retain published history instead of force-pushing a rewritten branch.

Update all three `CORE.lock` files together to the same accepted core commit. QEMU also pins the VM tooling revision in `TOOLS.lock`. Source updates do not authorize relabeling an existing binary or overwriting its build manifest.

## Send changes upstream

Create a focused branch from official `upstream/master` and apply only the change being proposed. Keep platform support separate from unrelated generic fixes, retain licences and authorship, and follow upstream's [contribution guidance](../../CONTRIBUTING.md). Test the targets affected by that change. Open a pull request from this fork only when that publication is authorized.

After upstream accepts a change, receive it through the next integration merge. Resolve duplicate changes there and keep the upstream pull-request reference with the corresponding source history. Never merge the complete PomAROS branch into an upstream pull request.

## Private native development

Private native work can consume this public branch through a dedicated fetch remote. Review generic improvements before bringing them back as focused public commits. Do not merge or push private native branches to the public fork. Shared source ownership does not require publishing native-only drivers or private integration history.
