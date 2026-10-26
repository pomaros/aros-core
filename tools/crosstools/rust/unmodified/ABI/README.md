# Native network ABI qualification

`network-abi.py` compiles constant records from the installed AROS SDK and the
actual `libc` rlib produced by `../build.py`. It extracts `.aros_abi` from both
AArch64 objects with `llvm-objcopy`; no guest execution is required.

```sh
python3 network-abi.py \
  --sdk /absolute/path/to/Developer \
  --crosstools /absolute/path/to/aros-crosstools \
  --std-build /absolute/path/to/qualified-std-build \
  --output /absolute/path/to/new-artifact-directory
```

The std build must contain `result.json` with `std_compile: PASS`, its `rustc`
wrapper, the JSON target, and exactly one `core` and `libc` rlib in
`sysroot/lib/rustlib/aarch64-unknown-aros/lib`. The output must be a new directory
outside all three input trees. The tool records commands, diagnostics, generated
C/Rust sources, objects, extracted bytes, and a JSON comparison. It fingerprints
the consumed SDK headers and std/tool inputs and verifies them after compilation.

The poll native contract header is copied from the source tree (or
`--native-header`) into private output, allowing an ABI check before SDK
installation without modifying shared headers.

The records cover sizes, alignment, member offsets and member sizes for socket
addresses, resolver records, scatter/gather messages, ancillary message headers,
linger, timeout and pollfd records, plus nfds_t/short widths, poll event bits,
socket options, protocol identifiers and ioctl
values. `MSG_NOSIGNAL` is an optional capability: absence in both SDK and Rust is
reported as unsupported. A Rust value with no native counterpart fails.

Exit status 1 means a direct ABI mismatch; 2 means input validation or compilation
failed. Exit status 0 means `DIRECT_ABI_MATCH_BRIDGE_UNQUALIFIED`, not runtime
acceptance. `addrinfo` belongs to the explicit resolver conversion boundary and
its mismatches are reported separately. The comparison never qualifies that
conversion or any networking behavior on target hardware.

Pass `--application-libc-rlib /path/to/liblibc-<hash>.rlib` to compare the actual
application store crate (for example libc 0.2.190) instead of std's internal
libc. The wrapper, core and target identity still come from `--std-build`; the
selected application rlib and its dependency path are fingerprinted separately.
Use `--extended-c-api` for application bindings that expose POSIX.1-2008 and
BSD extensions together. It selects the SDK's `_GNU_SOURCE` profile in the
recorded C command; that profile enables POSIX.1-2008, XSI 700 and BSD names. A hidden C declaration is not an absent SDK capability;
qualify the feature profile exposed by the application bindings.

`libc-exports.py --std-build BUILD --application-libc-rlib RLIB
--crosstools TOOLS --output OUTPUT` extracts all ELF members of both libc
archives and links them explicitly into one relocatable object. This forces
collisions that ordinary lazy archive extraction could hide. It rejects Rust
crate definitions of the unavailable C interfaces and, with `--binary FILE`,
requires exactly one native provider for the probed application calls.

The native AArch64 declarations include `pthread_condattr_t.cond_clock` from
`pthread.h`. Its attribute storage and field offsets must match the SDK; a
smaller declaration would let native initialization overwrite the caller's
allocation. File metadata, time types and pthread attributes require exact
layout checks when updating the SDK. The mutex, condition, rwlock and semaphore
facades use overallocated aligned storage for pointer-only calls; sufficient
storage does not establish compatibility for embedding those types in C structs
or passing arrays whose stride is the native type size.

The layout gate also compares `pthread_condattr_t`, both field offsets/sizes,
`clockid_t` and the realtime/monotonic clock constants against the actual SDK.
A stale std libc without the condition-clock field must fail qualification;
matching socket layouts alone cannot establish thread ABI agreement.
