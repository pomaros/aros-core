<!-- Copyright (C) 2026 John KNIPPER. -->
<!-- SPDX-License-Identifier: APL-1.1 -->

# Virtio GPU command completion

```sh
python3 developer/debug/test/hidd/virtiogfx_completion_host/run.py --output /tmp/virtiogfx-completion
```

Choose a new output directory. The fixture extracts the actual command, reap,
request and presentation functions, replacing AArch64 barriers with host atomic
fences. Exec signaling, timers, queue publication and DMA memory are modeled;
ASan/UBSan check the host execution. Compiler and execution deadlines are bounded.

Cases cover completion without an IRQ, completion coincident with timer expiry,
spurious IRQs, an outstanding command retaining its DMA storage, late completion
followed by a full-screen repaint, transient signal shortage retaining damage,
invalid response ID/length/type/flags/fence, and queue index wraparound. Seven
mutations must compile and fail at runtime. Inspect failure logs to distinguish
assertions for the intended defect from unrelated host failures.

This test does not exercise the IRQ callback, real DMA/cache coherency, close or
reset, or cross-CPU waiter/signal synchronization. The transport's existing
local Disable/Enable protection is not evidence of an SMP lifetime guarantee.
Full module builds and VM runtime qualification remain separate from this test.
