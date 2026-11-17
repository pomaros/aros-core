# Wireless association regressions

The HOST checks compile production function bodies with controlled device or
firmware replies under AddressSanitizer and UndefinedBehaviorSanitizer. They
prove control flow and ownership, not radio operation.

- `tx-security.py`: association/key gates and actual Ethernet headers, including
  RAW callers, overflow and frame-size boundaries.
- `offload-contract.py`: WirelessManager capability negotiation, unknown or
  truncated replies, binary PSK/passphrase tags and association error reporting.
- `association-job.py`: queued option copies, pending-secret cancellation,
  resource refusal, input bounds and CONNECT after resource success. The
  resource's capability promises completed key negotiation for offloaded PSK.
- `retry-pacing.py`: delayed retries despite a permanently latched wake signal.

The first three accept `--reject REV` to require that an older implementation
fails the same behavioral assertions. A successful HOST run cannot establish
that the firmware completes authentication, that DHCP succeeds, or that a
saved network reconnects after reboot; those require separate target evidence.

`S2_GETWIRELESSCAPS` uses `Sana2WirelessCaps` in `ios2_StatData`. The caller
sets SizeAvailable; the driver returns SizeSupplied and Flags. Unsupported or
incomplete queries do not enable offload. `S2WCAP_4WAY_PSK` promises both
passphrase and binary 32-byte PSK input and CONNECT only after keys are ready.
`S2INFO_PSK` is borrowed only during S2_SETOPTIONS; asynchronous drivers copy
it and clear their copy after use or cancellation. A driver must not advertise
this contract merely because its radio accepts association commands.
