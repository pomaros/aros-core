# Classic metadata values and lookup ownership

```sh
python3 -B developer/debug/test/crt/stat_metadata_host/run.py --mutations --output /absolute/new-output
```

The fixture executes the actual narrow/wide field fillers, shared path lookup
and typed entry points. DOS examination, allocation, locks, path joining, hash
and protection/identity conversion are modeled. Ordinary off_t is deliberately
32 bits in this fixture; the wide metadata result must preserve values above
4 GiB. This model checks the conversion and does not qualify a 32-bit target ABI.

Cases cover wide filling directly and through path lookup, root and default
metadata success, every allocation failure in a relative lookup, provider
failure, path-join failure and exhausted enumeration. Each case checks resources
and current-directory restoration. Four exact mutations must fail at runtime:
narrowing the wide size, losing the root success result, choosing the narrow
filler for a wide path and leaking the input-path allocation.

ASan/UBSan executables are temporary; source identities and logs remain in the
output. Actual filesystem behavior, final-link/assign resolution, object identity
and hash correctness require their own evidence. The shared lookup retains the
existing native resolver behavior while owning allocation and success handling.
