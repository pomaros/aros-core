#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Run exact-source target fault oracles; HOST only, never GPU qualification."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import signal
import shutil
import subprocess
import sys
import time


def braced(text, start):
    opening = text.index("{", start)
    depth, cursor = 1, opening + 1
    while depth:
        if text[cursor] == "{":
            depth += 1
        elif text[cursor] == "}":
            depth -= 1
        cursor += 1
    return text[opening:cursor]


def routine(text, name, signature=None):
    found = re.search(r"^\s*(?:BOOL|VOID|void|GLADrawable)\s+" + name + r"\(", text, re.M)
    if not found:
        raise ValueError("missing actual function " + name)
    opening = text.index("\n{", found.start()) + 1
    header = text[found.start():opening].strip() if signature is None else signature
    return header + "\n" + braced(text, opening)


def limits():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_CPU, (15, 15))
    resource.setrlimit(resource.RLIMIT_FSIZE, (16 << 20, 16 << 20))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--ref", required=True)
    parser.add_argument("--mesa-source", type=Path, required=True)
    parser.add_argument("--allow-dirty-diagnostic", action="store_true")
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--egl-access-only", action="store_true")
    parser.add_argument("--admission-mode", choices=("access","surface","config"), default="access")
    args = parser.parse_args()
    root = args.source.resolve()
    out = args.output.resolve()
    if out == root or root in out.parents:
        raise ValueError("output must be outside production source")
    out.mkdir(parents=True, exist_ok=False)
    here = Path(__file__).resolve().parent
    paths = [root / "workbench/libs/mesa" / name for name in
             ("mesa3dgl_support.c", "mesa3dgl_types.h", "mesa3dgl_gallium.c", "mesa3dgl_glasetrast.c")]
    fixture = here / "target.c"
    result = {"schema":"GLA_TRANSACTION_HOST_V1", "result":"INCOMPLETE",
              "scope":"actual-source HOST facades; no native ABI/GPU proof",
              "source_ref":args.ref, "sources":{}, "cases":[]}
    result["started_at"]=datetime.now(timezone.utc).isoformat()
    compiler=Path(shutil.which(args.cc) or args.cc).resolve()
    result["compiler"]={"path":str(compiler),"sha256":hashlib.sha256(compiler.read_bytes()).hexdigest()}
    start = time.monotonic()
    cache={}

    def snapshot(path):
        if path not in cache:
            data=path.read_bytes(); cache[path]=data
            result["sources"][str(path)]=hashlib.sha256(data).hexdigest()
            source_dir=out/"source"; source_dir.mkdir(exist_ok=True)
            (source_dir/path.name).write_bytes(data)
        return cache[path].decode()

    def run(name, command, token=None):
        record = {"name":name, "argv":command}
        result["cases"].append(record)
        completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   timeout=25, preexec_fn=limits, env={**os.environ,
                                   "ASAN_OPTIONS":"detect_leaks="+("0" if sys.platform=="darwin" else "1")+":abort_on_error=1",
                                   "UBSAN_OPTIONS":"halt_on_error=1"})
        (out / (name + ".log")).write_bytes(completed.stdout)
        record["exit"] = completed.returncode
        record["pass"] = completed.returncode == 0 if token is None else (
            completed.returncode == -signal.SIGABRT and token.encode() in completed.stdout)
        if token:
            record["required_assertion"] = token
        if not record["pass"]:
            raise RuntimeError(name + " failed; retained log")

    try:
        actual = subprocess.run(["git", "rev-parse", "HEAD"], cwd=root, check=True,
                                capture_output=True, text=True, timeout=10).stdout.strip()
        expected = subprocess.run(["git", "rev-parse", args.ref], cwd=root, check=True,
                                  capture_output=True, text=True, timeout=10).stdout.strip()
        if actual != expected:
            raise ValueError("source HEAD differs from requested ref")
        status = subprocess.run(["git", "status", "--porcelain", "--", "workbench/libs/mesa", "workbench/libs/egl", "workbench/libs/gl"],
                                cwd=root, check=True, capture_output=True, text=True, timeout=10).stdout
        result["dirty_source"] = bool(status)
        if status and not args.allow_dirty_diagnostic:
            raise ValueError("uncommitted production source requires diagnostic mode")
        result["source_commit"] = actual
        for path in paths + [fixture, Path(__file__).resolve()]:
            snapshot(path)
        support, types, gallium, setter = [snapshot(path) for path in paths]
        structs = []
        for name in ("mesa3dgl_drawable", "mesa3dgl_context"):
            at = types.index("struct " + name + "\n")
            structs.append("struct " + name + "\n" + braced(types, at) + ";")
        bodies = [routine(support, name) for name in (
            "MESA3DGLSelectRastPort", "MESA3DGLStandardInit",
            "MESA3DGLRecalculateBufferWidthHeight", "MESA3DGLFreeTarget",
            "MESA3DGLPrepareTarget", "MESA3DGLCommitTarget", "MESA3DGLVisualAccepts")]
        bodies.append(routine(gallium, "MESA3DGLCheckAndUpdateBufferSize"))
        bodies.append(routine(setter, "glASetRast", "BOOL glASetRast(GLAContext ctx, struct TagItem *tagList)"))
        source = snapshot(fixture).replace("/* STRUCTS */", "\n".join(structs)).replace("/* SOURCE */", "\n\n".join(bodies))

        def check(label, text, sanitized, geometry=False, token=None, argument=None):
            generated = out / (label + ".c")
            generated.write_text(text)
            binary = out / label
            flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if sanitized else []
            # AROS TagItem lists use { TAG_DONE }; its trailing zero is
            # guaranteed by C. Preserve the actual body instead of rewriting it.
            run(label + "-compile", [args.cc,"-std=c11","-Wall","-Wextra","-Werror",
                "-Wno-missing-field-initializers","-pedantic","-g",*flags,str(generated),"-o",str(binary)])
            run(label + "-run", [str(binary)] + (["geometry"] if geometry else []) + ([argument] if argument else []), token)
        if not args.egl_access_only:
            check("plain", source, False)
            check("sanitized", source, True)
        # Semantic negative control for the precise original destructive-order bug.
        needle = "if (!MESA3DGLPrepareTarget(&staged, tagList))"
        assert source.count(needle) == 1
        if not args.egl_access_only:
            check("destructive-order-mutant", source.replace(needle,
                  "MESA3DGLFreeTarget(&_ctx->own);\n    " + needle), True,
                  token="memcmp(&ctx->own, saved, sizeof(*saved)) == 0")
        backend = root / "workbench/libs/egl/egl_arosmesa.c"
        mesa = args.mesa_source.resolve() / "src/egl/main/eglcontext.c"
        egl_fixture = here / "egl.c"
        for path in (backend, mesa, egl_fixture):
            snapshot(path)
        egl = snapshot(backend)

        def body_for(text, name, signature):
            pos = text.index(name + "(")
            opening = text.index("\n{", pos) + 1
            return signature + "\n" + braced(text, opening)

        headers = (
            ("egl_arosmesa_context", "static struct egl_arosmesa_context *egl_arosmesa_context(_EGLContext *ctx)"),
            ("egl_arosmesa_surface", "static struct egl_arosmesa_surface *egl_arosmesa_surface(_EGLSurface *surf)"),
            ("free_context", "static void free_context(_EGLContext *ctx)"),
            ("free_surface", "static void free_surface(_EGLSurface *surf)"),
            ("drawable_of", "static GLADrawable drawable_of(_EGLSurface *surf)"),
            ("egl_arosmesa_destroycontext", "static EGLBoolean egl_arosmesa_destroycontext(_EGLDisplay *dpy, _EGLContext *ctx)"),
            ("egl_arosmesa_destroysurface", "static EGLBoolean egl_arosmesa_destroysurface(_EGLDisplay *disp, _EGLSurface *surf)"),
            ("egl_arosmesa_makecurrent", "static EGLBoolean egl_arosmesa_makecurrent(_EGLDisplay *disp, _EGLSurface *dsurf, _EGLSurface *rsurf, _EGLContext *ctx)"),
        )
        egl_structs = []
        for name in ("egl_arosmesa_context", "egl_arosmesa_surface"):
            at = egl.index("struct " + name + "\n")
            egl_structs.append("struct " + name + "\n" + braced(egl, at) + ";")
        egl_source = snapshot(egl_fixture).replace("/* STRUCTS */", "\n".join(egl_structs))
        mesa_routines = (
            ("_eglBindContextToThread", "static _EGLContext *_eglBindContextToThread(_EGLContext *ctx, _EGLThreadInfo *t)"),
            ("_eglCheckMakeCurrent", "static EGLBoolean _eglCheckMakeCurrent(_EGLContext *ctx, _EGLSurface *draw, _EGLSurface *read)"),
            ("_eglBindContext", "static EGLBoolean _eglBindContext(_EGLContext *ctx, _EGLSurface *draw, _EGLSurface *read, _EGLContext **old_ctx, _EGLSurface **old_draw, _EGLSurface **old_read)"),
        )
        egl_source = egl_source.replace("/* MESA_BIND */", "\n\n".join(body_for(snapshot(mesa), name, signature) for name, signature in mesa_routines))
        egl_source = egl_source.replace("/* BACKEND */", "\n\n".join(body_for(egl, name, signature) for name, signature in headers))
        if args.egl_access_only:
            check("egl-owner-admission", egl_source, True, argument=args.admission_mode)
        else:
            check("egl-plain", egl_source, False)
            check("egl-sanitized", egl_source, True)
        rollback = "if (!bound)"
        assert egl_source.count(rollback) == 1
        if not args.egl_access_only:
            check("egl-no-rollback-mutant", egl_source.replace(rollback, "if (0 && !bound)"), True,
                  token="thread.CurrentContext==&a->base && native_current==a->amesactx")
            check("geometry", source, True, geometry=True)
        result["result"] = "DIAGNOSTIC_PASS" if status else "PASS"
        if any(path.read_bytes()!=data for path,data in cache.items()):
            raise RuntimeError("source changed during run; saved snapshots retain exact tested bytes")
    except (OSError, ValueError, AssertionError, subprocess.SubprocessError, RuntimeError) as error:
        result["result"] = "FAIL"
        result["error"] = str(error)
    finally:
        result["elapsed_seconds"] = time.monotonic() - start
        (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"result":result["result"],"output":str(out)}))
    return 0 if result["result"] in ("PASS", "DIAGNOSTIC_PASS") else 1


if __name__ == "__main__":
    raise SystemExit(main())
