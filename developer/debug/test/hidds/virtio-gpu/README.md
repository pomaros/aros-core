# Shared VirtIO-GPU integration tests

These are explicit native targets, not automatic PCI tests at boot.

- `test-virtio-completion`: bounded synthetic queue memory exercises the actual owner implementation's late completion, malformed completion and retained-backing states. It does not touch a PCI device and is not hardware timeout evidence.
- `test-virtio-shared`: requires the desktop to use virtiogfx on a VirGL-capable host. Tests independent services, central IDs, backing ownership, size bounds, a rejected context-create command and a succeeding display query.
- `test-virgl-gl`: pixel/depth/stencil and finite context lifecycle checks. Run `virglglprobe virgl single` for one context, or omit `single` for recreation. The expected renderer argument is required.
- `test-virgl-pair`: runs the probe in two processes and checks survivor/close ordering through named ports.
- `test-virgl-run`: finite sequential process launcher.

Record guest, SDK, source and host identities. Use the managed pool and collect output/return codes. A successful build does not establish runtime success.
