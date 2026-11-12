# Shared VirtIO-GPU integration tests

These are explicit native targets, not automatic PCI tests at boot. They
cover the experimental `hidd.virtiogpu.transport` service (see
`rom/hidds/virtiogfx/include/virtiogpu.h`), the VirGL client bound to it and
the Mesa provider lifetime. Record guest, SDK, source and host identities;
use the managed pool and collect output and return codes. A successful build
does not establish runtime success.

## Owner state machine (no device)

- `test-virtio-completion` compiles the owner's `transport.c` against a
  synthetic used ring in ordinary memory and drives `gpu_reap` and the owned
  state transitions: a completion that has not arrived, a valid late
  completion, a malformed completion, a complete fenced host rejection, a
  late detach, unauthorized envelopes and owner disposal while backing is
  still referenced. It is a state-machine regression of the owner code. It
  does not exercise a PCI device, a timer or an interrupt, so it is not
  evidence about a real host hang, a lost interrupt or the 1 second deadline.
  No wider destructive device tests exist in this series.

## Live owner (desktop on virtiogfx, VirGL-capable host)

- `test-virtio-shared`: two independent services, central IDs, backing
  ownership, size bounds, a rejected truncated context-create and a
  succeeding display query afterwards.
- `test-virtio-owned`: attached backing cannot be freed, a peer cannot
  destroy another service's context or attach another service's memory, an
  unallocated number is not a resource ID. Its resources never touch scanout
  1 and are detached and unreferenced before any assertion fails.

## GL client and Mesa provider lifetime

- `test-virgl-gl` (`virglglprobe <renderer> [modes]`): pixel, texture,
  blend, depth and stencil checks against the expected renderer name
  (`virgl`, or `softpipe` on a display without a Gallium driver). Modes:
  `single` one context, otherwise three recreations; `quick` omits the
  viewing pause; `multi` a second simultaneous context closed in both orders;
  `share` three contexts on one provider (`GLA_ShareScreen` and
  `GLA_ShareContext`) destroyed creator-first and creator-last, each followed
  by a fresh context; `pins` prints and checks the `virgl.hidd` open count
  before and after; `failcontext=0` requests an impossible version and
  expects a clean refusal.
- `test-virgl-pair` (`virglpaircheck path/to/virglglprobe log-prefix
  [reverse]`): two processes with live contexts, closed in either order
  through named ports, then provider pins, Mesa and PosixC reference counts
  compared with a held baseline. Readiness has a deadline; the external VM
  harness must also bound a child that never exits.
- `test-virgl-run`: retained CRT/open-only process diagnostic without GL
  rendering.

### Allocation-failure control (test build only)

The regular Mesa library contains no fault injection. For the early-exit
paths of `glACreateContext`, build the test variant with
`gmake mesa3dgl-library MESA3DGL_FAULT_INJECTION=1`; it produces
`mesa3dgl<build>fault.library` in its own object directory and leaves the
regular library and `SYS/GL.default` untouched. On the guest, make that
library reachable (for example `Assign LIBS: RAM:fault ADD`), select it with
`SetEnv SYS/GL mesa3dgl<build>fault`, then run the probe once per point with
`SetEnv MESA3DGL_FAULT screen|stmanager|shared|framebuffer`. Each run must
report `FAIL: context` with return code 10 and, with `pins`, an unchanged
`virgl.hidd` open count; `test-virtio-shared` afterwards and the owner's
last-client line on the serial log show no leaked owner state. Unset the
variable and restore `SYS/GL` before any further test. Do not ship or
install the fault variant.
