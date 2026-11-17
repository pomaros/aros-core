#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER. AROS Public License (APL) Version 1.1.
"""HOST checksum vectors, incremental boundaries, bootstrap adapter and file errors."""
import ctypes
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[5]
with tempfile.TemporaryDirectory(prefix="aros-sha256-") as temporary:
    work = Path(temporary)
    (work / "include/aros").mkdir(parents=True)
    shutil.copy(ROOT / "compiler/include/aros/sha256.h", work / "include/aros")
    algorithm = ROOT / "compiler/alib/sha256.c"
    common = ["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-I" + str(work / "include")]
    wrapper = work / "wrapper.c"
    wrapper.write_text("""#include <aros/sha256.h>
void chunks(const unsigned char *data, size_t size, size_t step, unsigned char *digest) {
    struct AROSSHA256Context context;
    AROSSHA256Init(&context);
    while (size) {
        size_t n = size < step ? size : step;
        AROSSHA256Update(&context, data, n);
        size -= n; data += n;
    }
    AROSSHA256Final(&context, digest);
}
""")
    library = work / "sha.dylib"
    subprocess.run(common + ["-shared", "-fPIC", str(ROOT / "arch/aarch64-apple/bootstrap/stage2_sha256.c"), str(wrapper), "-o", str(library)], check=True)
    lib = ctypes.CDLL(str(library))
    for name in ("AROSSHA256", "AROSAppleStage2SHA256"):
        getattr(lib, name).argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p]
    lib.chunks.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_size_t, ctypes.c_void_p]
    checks = 0
    for size in (0, 1, 3, 55, 56, 63, 64, 65, 119, 120, 127, 128, 129, 65535, 65536, 65537, 1000000):
        data = bytes((i * 31 + 7) & 255 for i in range(size))
        expected = hashlib.sha256(data).digest()
        for name in ("AROSSHA256", "AROSAppleStage2SHA256"):
            digest = ctypes.create_string_buffer(32)
            getattr(lib, name)(data, size, digest)
            assert digest.raw == expected, (name, size)
            checks += 1
        for step in (1, 7, 55, 64, 65, 65536):
            digest = ctypes.create_string_buffer(32)
            lib.chunks(data, size, step, digest)
            assert digest.raw == expected, (size, step)
            checks += 1
    command = work / "SHA256Sum"
    subprocess.run(common + [str(ROOT / "workbench/c/SHA256Sum.c"), str(algorithm), "-o", str(command)], check=True)
    def run(args, expected):
        global checks
        result = subprocess.run([str(command)] + args, cwd=work, capture_output=True, text=True)
        assert result.returncode == expected, (args, result.returncode, result.stdout, result.stderr)
        checks += 1
        return result
    (work / "empty").write_bytes(b"")
    (work / "a file").write_bytes(b"abc")
    with (work / "large").open("wb") as file:
        file.truncate(129 * 1024 * 1024 + 3)
    digest = hashlib.sha256()
    with (work / "large").open("rb") as file:
        while chunk := file.read(65536): digest.update(chunk)
    result = run(["empty", "a file", "large"], 0)
    assert digest.hexdigest() + "  large" in result.stdout
    manifest = work / "manifest"
    manifest.write_text(result.stdout)
    assert "verified 3 files" in run(["--check", "manifest"], 0).stdout
    for content in ("", result.stdout[:-1], "0" * 64 + "  a file\n", "f" * 64 + "  missing\n", "x" * 9000 + "\n", "z" * 64 + "  empty\n", "0" * 64 + "  \n"):
        manifest.write_text(content)
        run(["--check", "manifest"], 20)
    run(["missing"], 20)
    run(["--check", "missing"], 20)
    run(["--check"], 20)
    run([], 20)
    run(["."], 20)
    run(["bad\nname"], 20)
    print(f"SHA256 HOST PASS: {checks} checks; largest file 129 MiB + 3 bytes")
