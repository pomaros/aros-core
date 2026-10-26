# Pipe-name reservation regression

```sh
python3 -B developer/debug/test/crt/pipe_name_host/run.py --output /absolute/new-output
```

Eight threads call the production name allocator for 80,000 distinct names.
Each name must preserve a full-width process identifier and its independently
reserved unsigned serial without truncation. Exact mutations that truncate the
identifier or reuse a serial must fail at runtime. ASan/UBSan runs use temporary
executables; the output retains diagnostics and input hashes.

This checks name allocation only. Descriptor reservation, DOS handler behavior,
process identity reuse and real guest concurrency need their own evidence.
