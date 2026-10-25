<!-- Copyright (C) 2026 John Knipper -->
<!-- Licensed under the AROS Public License (APL) Version 1.1 -->

# Keyring engine provenance

`engine.c`, `engine.h` and `tests/` are original APL 1.1 code. The bounded
wire format, owner namespaces, explicit wiping and transactional snapshot
interface are original project work.

`vendor/argon2/` is an unmodified subset of P-H-C/phc-winner-argon2,
revision `62358ba2123abd17fccf2a108a301d4b52c01a7c`, tag `20190702`.
`vendor/argon2/SOURCE.json` records the source URL and SHA-256 of every imported
file. The complete upstream `LICENSE` includes Apache-2.0 and CC0-1.0 texts.
The Apache-2.0 option is selected for Argon2; the BLAKE2 files use the CC0-1.0
option. Original copyright and dual-license headers are preserved. The source
is compiled with `ARGON2_NO_THREADS`, using `ref.c`. Import was reviewed against
`aros-apple-silicon/docs/features/licensing/README.md` before copying.

AEAD uses the existing mbedTLS 3.6.7 ChaCha20-Poly1305 implementation and its
constant-time tag check. No cryptographic primitive is reimplemented here.
Host tests compile that same upstream implementation from the existing AROS
source archive, with its own unchanged license. The host fixture entropy
callback is deterministic; the production adapter must use entropy.resource.

Reproduce the host run with an extracted mbedTLS 3.6.7 source directory:

```sh
python3 workbench/libs/keyring/tests/run-host.py \
  --mbedtls /path/to/mbedtls-3.6.7 --out /tmp/keyring-host-run
```

The runner emits the exact compile command, diagnostics, production source
hashes, sealed vault and JSON result. ASan and UBSan halt on error. Apple ASan
has no LeakSanitizer support, so `detect_leaks=0` is explicit. The production
build omits `KR_ENGINE_TEST_UNSEALED`; that definition is confined to the host
fixture and enables its marker-positive unsealed control.
