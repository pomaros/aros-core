# Atomic file-open outcome

`OpenWithInfo(name, MODE_NEWFILE, &information)` (DOS 50.88, vector232) creates
or truncates an exclusively opened regular file. On success the result is
OPENINFO_CREATED or OPENINFO_TRUNCATED; on failure it returns BNULL and IoErr.
The information field is zeroed after valid argument admission, and published
only on success. Other modes, null name and null output are rejected.

The DOS service sends ACTION_FINDOUTPUT_INFO(1038), with FINDOUTPUT's FH, parent
lock and BSTR arguments. Res1 carries the outcome, or zero plus Res2 error.
There is no existence probe and no fallback to ordinary FINDOUTPUT: unsupported
handlers cannot silently lose the outcome. FAT determines the result inside its
serialized open operation while holding the exclusive file lock. The existing
Open API and its packet numbers remain available. The task path resolver routes
the new operation through the same shared context provider. Soft links are
returned as ERROR_IS_SOFT_LINK; this API does not resolve them.

Run `python3 run.py --output /absolute/fresh/path`. This extracts production DOS
packet-helper and FAT open code, exercises outcome propagation, allocation,
sharing/type/protection/storage failures and malformed-handler cleanup under
HOST ASan/UBSan. IPC/storage are mocked: this does not qualify disk persistence,
packet dispatch, DOS library vectors or native execution. Build and native
filesystem tests must establish those separately.
