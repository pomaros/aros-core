# Composition presentation wake

Logical window damage submits one preallocated neutral input request to wake
Intuition's existing serialized presentation pass. Drawing callbacks allocate
nothing, never wait for input completion and never present directly while a
client can hold layer locks. Concurrent requests coalesce until the previous
reply is removed from its private port. Reservation precedes SendIO publication;
SendIO runs after the admission semaphore is released.

The resident Intuition instance owns the request and its input.device reference.
Its event contains no window/client pointer. Window closure therefore needs no
queued-pointer cancellation. Secret input capture can reject the neutral write;
periodic input processing remains a fallback, and subsequent damage can retry
after the rejection reply. Intuition's normal tick frequency is unchanged.

Run `python3 developer/debug/test/intuition/compositionwake_host.py --out FRESH_DIRECTORY`.
The fixture extracts production initialization and admission functions. It
injects all three allocation failures, checks64 coalesced requests, exercises
8,000 calls from eight producers with a concurrent modeled completion task,
and verifies no callback allocations or signal ownership. Negative controls
must reject reuse of a pending request and submission while admission is held.
Exec I/O and semaphores are modeled; this is not native presentation evidence.

Run `windowcomposition_host.py` for pixel clipping, hidden/exposed damage and
layer-lock ordering. Native qualification also requires an independently
observed animation with quiet input, window close during drawing, repeated
open/close and concurrent client drawing. Compare framebuffer changes rather
than relying solely on application animation callbacks. Preserve source,
library and runtime identities; a faster event callback is insufficient.
