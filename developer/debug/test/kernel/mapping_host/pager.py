#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Bounded HOST test of the actual portable pager; no native acceptance."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import signal
import subprocess
import sys
import time


def limits():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_CPU, (15, 15))
    resource.setrlimit(resource.RLIMIT_FSIZE, (16 << 20, 16 << 20))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--allow-dirty-diagnostic", action="store_true")
    args = parser.parse_args()
    out = args.output.resolve()
    here = Path(__file__).resolve().parent
    root = here.parents[4]
    pager = root / "rom/kernel/mapping/pager.c"
    header = pager.with_suffix(".h")
    test = here / "pager_test.c"
    if out == root or root in out.parents:
        raise ValueError("output must be outside production source")
    source_commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain", "--",
        "rom/kernel/mapping", "developer/debug/test/kernel/mapping_host"], cwd=root, text=True))
    if dirty and not args.allow_dirty_diagnostic:
        raise ValueError("dirty source requires diagnostic mode")
    out.mkdir(parents=True, exist_ok=False)
    inputs = (pager, header, test, Path(__file__).resolve())
    input_bytes = {p: p.read_bytes() for p in inputs}
    for p, data in input_bytes.items():
        (out / p.name).write_bytes(data)
    result = {"scope": "HOST portable pager only; no scheduler/PTE/disk proof",
              "source_commit": source_commit, "dirty_source": dirty, "verdict": "INCOMPLETE", "cases": [], "sources": {
                  str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in (pager, header, test, Path(__file__).resolve())}}
    started = time.monotonic()

    def run(label, command, expected_success=True):
        attempt = {"name": label, "argv": command}
        result["cases"].append(attempt)
        completed = subprocess.run(command, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, timeout=25,
                                   preexec_fn=limits, env={**os.environ,
                                   "ASAN_OPTIONS": "detect_leaks=" +
                                   ("0" if sys.platform == "darwin" else "1") +
                                   ":abort_on_error=1",
                                   "UBSAN_OPTIONS": "halt_on_error=1"})
        (out / (label + ".log")).write_bytes(completed.stdout)
        attempt["exit"] = completed.returncode
        attempt["pass"] = (completed.returncode == 0) == expected_success
        if not expected_success:
            expected = {
                "allocation-output-control-run": "Assertion failed: (!page.physical && !page.alias && !f->live), function failed, file ",
                "late-cancel-control-run": "Assertion failed: (actual == expected), function failed, file ",
            }[label]
            text = completed.stdout.decode(errors="replace")
            attempt["pass"] = (completed.returncode == -signal.SIGABRT and
                len(text.splitlines()) == 1 and text.startswith(expected))
            attempt["required_assertion"] = expected
        if not attempt["pass"]:
            raise RuntimeError(label + " failed; see retained log")

    def compile_and_run(name, source, extra=(), expected_success=True):
        binary = out / name
        command = [args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-pedantic", "-g", *extra, "-I", str(header.parent),
                   str(source), str(test), "-o", str(binary)]
        run(name + "-compile", command)
        run(name + "-run", [str(binary)], expected_success)

    try:
        compile_and_run("plain", pager)
        compile_and_run("sanitized", pager,
                        ("-fsanitize=address,undefined", "-fno-omit-frame-pointer"))
        original = pager.read_text()
        # The same independent assertions must reject the old output-ownership
        # bug and cancellation of a page whose last read has already returned.
        before = """if (!ops->allocate(ops->context,size,page)) {
        *page = (struct KrnPagerPage){0,NULL};
        return KPAGER_NO_MEMORY;
    }"""
        assert original.count(before) == 1
        mutant = out / "allocation-output-mutant.c"
        mutant.write_text(original.replace(before,
            "if (!ops->allocate(ops->context,size,page)) return KPAGER_NO_MEMORY;"))
        compile_and_run("allocation-output-control", mutant, expected_success=False)
        before = "if (error == KPAGER_OK && cancelled(ops))"
        assert original.count(before) == 1
        mutant = out / "late-cancel-mutant.c"
        mutant.write_text(original.replace(before, "if (0)"))
        compile_and_run("late-cancel-control", mutant, expected_success=False)
        if any(p.read_bytes() != data for p, data in input_bytes.items()):
            raise RuntimeError("source changed during replay")
        result["verdict"] = "DIAGNOSTIC_PASS" if dirty else "PASS"
    except (OSError, subprocess.TimeoutExpired, RuntimeError, AssertionError) as error:
        result["error"] = str(error)
        result["verdict"] = "FAIL"
    finally:
        result["elapsed_seconds"] = time.monotonic() - started
        (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"verdict": result["verdict"], "output": str(out)}))
    return 0 if result["verdict"] in ("PASS", "DIAGNOSTIC_PASS") else 1


if __name__ == "__main__":
    raise SystemExit(main())
