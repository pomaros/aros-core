#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Exercise actual AROS EGL task-context callbacks with HOST Mesa/GL shims."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import signal
import shutil
import subprocess
import sys
import time


def braced(text, start):
    opening = text.index("{", start)
    depth = 1
    cursor = opening + 1
    while depth:
        if text[cursor] == "{":
            depth += 1
        elif text[cursor] == "}":
            depth -= 1
        cursor += 1
    return text[start:cursor]


def functions(source):
    result = []
    for name in ("egl_arosmesa_context", "egl_arosmesa_surface", "drawable_of",
                 "egl_arosmesa_makecurrent", "egl_arosmesa_swapbuffers"):
        position = source.index(name + "(")
        start = source.rfind("\nstatic", 0, position) + 1
        assert start > 0
        result.append(braced(source, start))
    return "\n\n".join(result)


def structs(source):
    return "\n\n".join(braced(source, source.index("struct " + name + "\n")) + ";"
                         for name in ("egl_arosmesa_context", "egl_arosmesa_surface"))


def limits():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_CPU, (15, 15))
    resource.setrlimit(resource.RLIMIT_FSIZE, (16 << 20, 16 << 20))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--ref", required=True)
    parser.add_argument("--allow-dirty-diagnostic", action="store_true")
    parser.add_argument("--cc", default="clang")
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = args.source.resolve()
    relative = "workbench/libs/egl/egl_arosmesa.c"
    backend = root / relative
    getter = root / "workbench/libs/mesa/mesa3dgl_glagetcurrentcontext.c"
    creator = root / "workbench/libs/mesa/mesa3dgl_glacreatecontext.c"
    fixture = here / "test.c"
    out = args.output.resolve()
    if out == root or root in out.parents:
        raise ValueError("output must be outside production source")
    out.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    result = {"verdict": "INCOMPLETE", "scope": "HOST actual EGL callbacks; "
              "Mesa resource/native GL calls shimmed; no native ABI/GPU/TLS proof",
              "sources": {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in (backend, getter, creator, fixture,
                                    Path(__file__).resolve())},
              "cases": []}

    def run(name, command, success=True, failure_token=None):
        record = {"name": name, "argv": command}
        result["cases"].append(record)
        completed = subprocess.run(command, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, timeout=25,
                                   preexec_fn=limits, env={**os.environ,
                                   "ASAN_OPTIONS": "detect_leaks=" +
                                   ("0" if sys.platform == "darwin" else "1") +
                                   ":abort_on_error=1",
                                   "UBSAN_OPTIONS": "halt_on_error=1"})
        (out / (name + ".log")).write_bytes(completed.stdout)
        record["exit"] = completed.returncode
        record["pass"] = completed.returncode == 0 if success else (
            completed.returncode == -signal.SIGABRT and failure_token is not None
            and failure_token.encode() in completed.stdout)
        if failure_token:
            record["required_assertion"] = failure_token
        if not record["pass"]:
            raise RuntimeError(name + " failed; see retained log")

    cache = {p: p.read_bytes() for p in (backend, getter, creator, fixture, Path(__file__).resolve())}
    snapshots = out / "source"
    snapshots.mkdir()
    for p, data in cache.items():
        (snapshots / p.name).write_bytes(data)
    compiler = Path(shutil.which(args.cc) or args.cc).resolve()
    result["compiler"] = {"path": str(compiler), "sha256": hashlib.sha256(compiler.read_bytes()).hexdigest()}
    owner = "ctx->st->frontend_context = ctx;"
    assert creator.read_text().count(owner) == 1

    def native_getter(text):
        # AROS function annotations precede the actual brace-delimited body.
        return "GLAContext glAGetCurrentContext(void)\n" + braced(text, text.index("\n{\n") + 1)

    def check(label, text, sanitized, success, native_source=None, token=None):
        generated = out / (label + ".c")
        generated.write_text(fixture.read_text().replace("/* DRIVER_STRUCTS */", structs(text))
                             .replace("/* DRIVER_FUNCTIONS */", functions(text))
                             .replace("/* NATIVE_OWNER */", owner)
                             .replace("/* NATIVE_GETTER */", native_getter(
                                 getter.read_text() if native_source is None else native_source)))
        binary = out / label
        flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if sanitized else []
        run(label + "-compile", [args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                                "-pedantic", "-g", "-pthread", *flags,
                                str(generated), "-o", str(binary)])
        run(label + "-run", [str(binary)], success,
            None if success else token)

    try:
        head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
        expected = subprocess.check_output(["git", "rev-parse", args.ref], cwd=root, text=True).strip()
        if head != expected:
            raise ValueError("source HEAD differs from selected ref")
        dirty = subprocess.check_output(["git", "status", "--porcelain", "--",
            "workbench/libs/egl", "workbench/libs/mesa", "developer/debug/test/egl_context_host"],
            cwd=root, text=True)
        result.update(source_commit=head, dirty_source=bool(dirty))
        if dirty and not args.allow_dirty_diagnostic:
            raise ValueError("dirty source requires diagnostic mode")
        current_source = backend.read_text()
        check("plain", current_source, False, True)
        check("sanitized", current_source, True, True)
        swap = "_EGLContext *current = _eglGetCurrentContext();"
        assert current_source.count(swap) == 1
        check("shared-display-context-control", current_source.replace(swap,
              "_EGLContext *current = last_bound;"), True, False,
              token="egl_arosmesa_swapbuffers(&display, &surface_a.base)")
        owner_return = "return current ? (GLAContext)current->frontend_context : NULL;"
        getter_source = getter.read_text()
        assert getter_source.count(owner_return) == 1
        check("internal-context-control", current_source, True, False,
              native_source=getter_source.replace(owner_return,
                  "return current ? (GLAContext)current->ctx : NULL;"),
              token="glAGetCurrentContext() == &native_a")
        if any(p.read_bytes() != data for p, data in cache.items()):
            raise RuntimeError("source changed during test")
        result["verdict"] = "DIAGNOSTIC_PASS" if dirty else "PASS"
    except (OSError, ValueError, AssertionError, subprocess.SubprocessError,
            RuntimeError) as error:
        result["verdict"] = "FAIL"
        result["error"] = str(error)
    finally:
        result["elapsed_seconds"] = time.monotonic() - started
        (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"verdict": result["verdict"], "output": str(out)}))
    return 0 if result["verdict"] in ("PASS", "DIAGNOSTIC_PASS") else 1


if __name__ == "__main__":
    raise SystemExit(main())
