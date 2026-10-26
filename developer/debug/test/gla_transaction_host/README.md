# GLA transaction HOST checks

This suite extracts the actual native target selection, preparation, cleanup and
commit functions, and `glASetRast`, from an explicitly selected source tree.
AROS bitmap, layer and rastport allocation calls are HOST facades with deterministic
failure injection and ownership checks. Mesa framebuffer resources have independent
identities and pixel sentinels. This does not exercise native GPU, scheduling or ABI.

The source tree must be pinned in the result. A work in progress run is diagnostic
only; the delivery run must name the exact reviewed production commit.

The separate EGL fixture exercises actual backend callbacks with actual Mesa
`_eglBindContext` reference behavior: native refusal, logical refusal, successful
replacement and last-reference destruction. The previous two-task context/getter
oracles remain independently required.

The native drawable fixture uses actual `glACreateDrawable`,
`glADestroyDrawable`, `glAMakeCurrentDrawables`, and `glADestroyContext` bodies.
It checks the provider survives its creator, rejects another screen or visual
before calling the tracker, and preserves distinct draw/read pixels across
contexts sharing a screen. Context creation cleanup is checked using its exact
`error_out` tail and the reachable state after framebuffer allocation fails;
this is not a test of the entire creation function.

Each command has a 25 second wall timeout, a 15 second CPU limit, a 16 MiB
per-file limit and no core dumps. Compilation uses `-Wall -Wextra -Werror
-pedantic`, with one declared exception for AROS `{ TAG_DONE }` initializers
whose trailing zero C guarantees. ASan and UBSan are enabled in sanitizer runs.
LeakSanitizer is disabled on Darwin, where it is unsupported; allocation ledgers
and explicit reference/destruction counts remain asserted.

Pass an output directory outside the production source, `--source` with the
exact candidate worktree and `--ref` with its commit. `run.py` also requires
`--mesa-source` pointing to the admitted Mesa 26 tree. Dirty production source
requires `--allow-dirty-diagnostic` and can never produce a final PASS receipt.
`run.py --egl-access-only --admission-mode {access,surface,config}` isolates
the pre-admission cases. `native_run.py` checks provider lifetime, cleanup order, shared-provider binding,
and rejection of foreign providers and each visual field.

Geometry checks reject empty surfaces, out-of-bounds margins and pointer-sized
tags that would truncate when passed to graphics APIs. Refusal preserves the
previous target and its pixels. EGL admission checks require ownership and config
refusal before any native call; native refusal restores logical references,
including objects whose application references have already been released.
