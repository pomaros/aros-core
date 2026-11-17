# Streaming SHA-256 checks

`C:SHA256Sum FILE...` hashes files with a fixed 64 KiB read buffer. Output is
64 lowercase hexadecimal digits, two spaces and the filename. Files with a
newline in their name are refused. `C:SHA256Sum --check MANIFEST` verifies this
format; filenames may contain spaces. Each record must end in a newline.
Malformed, empty, truncated or unreadable manifests, missing files, read errors
and mismatches return 20. Success prints the number of verified files.

The generic `aros/sha256.h` incremental API lives in amiga.lib. The freestanding
bootstrap adapter includes the same implementation without OS dependencies.
It supports one-shot hashing and arbitrary incremental chunk boundaries.

Run `python3 developer/debug/test/dos/sha256sum_host/run.py` from the source tree.
The HOST check compares both APIs and the bootstrap adapter with Python hashlib
across block/padding boundaries and incremental chunk sizes. It runs the actual
command on empty, short and 129 MiB files, and rejects mismatches, missing files,
malformed and truncated records, and read failures. This is HOST evidence;
compiled guest execution and filesystem persistence require their own artifacts.
