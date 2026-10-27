<!-- Copyright (C) 2026 John KNIPPER. -->
<!-- SPDX-License-Identifier: APL-1.1 -->

# Real full-volume error and recovery probe

`filesystem_full_volume_probe(directory)` is linked by a product's disposable
filesystem runner. Pass an existing test directory on a freshly created 64 MiB
volume. It can write up to 128 MiB while seeking a real ENOSPC result; never use
a system or user-data volume. The probe neither mounts a volume nor selects one
through a fixed assign. Paths exceeding its buffers are refused.

It requires a real failed write with ENOSPC after successful writes, removes the
filler, and requires a subsequent create/write/close/delete to succeed. It does
not inject errno or accept an unrelated failure as a full-volume result. Files
are created exclusively; cleanup only removes files created by this invocation.
A runner must discard the fixture afterward even when cleanup fails.

The caller must preserve normal guest/compiler/runtime identity and verify the
result. A translation-unit compile is preparation, not filesystem acceptance.
