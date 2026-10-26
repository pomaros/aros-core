#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Compile production walker, exercise host cases and a bounded EL1 QEMU run."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

p = argparse.ArgumentParser()
p.add_argument("--output", type=Path, required=True)
p.add_argument("--page-bits", type=int, choices=(12, 14), default=12)
args = p.parse_args()
out = args.output.resolve()
here = Path(__file__).resolve().parent
core = here.parents[3]
if out == core or core in out.parents:
    p.error("output must be outside the source checkout")
if out.exists() or args.output.is_symlink():
    p.error("output must be a new directory")
engine = core / "arch/aarch64-all/kernel"
clang = os.environ.get("CLANG", shutil.which("clang"))
lld = os.environ.get("LD_LLD", shutil.which("ld.lld"))
qemu = os.environ.get("QEMU", shutil.which("qemu-system-aarch64"))
if not all((clang, lld, qemu)):
    raise SystemExit("clang, ld.lld and qemu-system-aarch64 are required")
out.mkdir(parents=True)

def run(command):
    subprocess.run([str(x) for x in command], check=True, timeout=60)

flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", "-I", engine]
run([clang, *flags, "-fsanitize=address,undefined", here / "host.c",
     here / "stack_host.c", here / "fault_host.c", engine / "guarded_stack.c",
     engine / "setprotection.c", "-o", out / "host"])
host = subprocess.run([str(out / "host")], capture_output=True, text=True,
                      timeout=30, check=True)
(out / "host.log").write_text(host.stdout + host.stderr)
print(host.stdout, end="")
target = ["--target=aarch64-none-elf", "-ffreestanding", "-fno-builtin",
          "-fno-stack-protector", "-mgeneral-regs-only", f"-DPAGE_BITS={args.page_bits}"]
for source, name in [(here / "qemu.c", "qemu"), (here / "entry.S", "entry"),
                     (engine / "setprotection.c", "engine"),
                     (engine / "guarded_stack.c", "stack")]:
    run([clang, *flags, *target, "-c", source, "-o", out / (name + ".o")])
run([lld, "-T", here / "image.ld", out / "entry.o", out / "qemu.o",
     out / "engine.o", out / "stack.o", "-o", out / "fixture.elf"])
cpu = "cortex-a72" if args.page_bits == 12 else "max"
command = [qemu, "-M", "virt", "-cpu", cpu, "-m", "2048", "-smp", "1",
           "-nographic", "-monitor", "none", "-semihosting-config",
           "enable=on,target=native", "-kernel", str(out / "fixture.elf"),
           "-d", "guest_errors,unimp", "-D", str(out / "qemu.trace")]
try:
    result = subprocess.run(command, capture_output=True, text=True, timeout=20)
except subprocess.TimeoutExpired as e:
    (out / "serial.log").write_bytes((e.stdout or b"") + (e.stderr or b""))
    (out / "result.json").write_text(json.dumps({"pass": False, "reason": "timeout"}))
    raise SystemExit("QEMU timed out: FAIL")
serial = result.stdout + result.stderr
trace = (out / "qemu.trace").read_text()
(out / "serial.log").write_text(serial)
marker = ("AARCH64-PROTECTION QEMU PASS guard-fault=1 restore=1 "
          "rx-exec=1 ro-fault=1 wx-rejected=1")
passed = (result.returncode == 0 and marker in serial and
          "control no-guard: access completed" in serial and
          not re.search(r"\b(ERROR|FAIL|PANIC|CRASH|FATAL)\b", serial) and
          not trace.strip())
(out / "result.json").write_text(json.dumps({"pass": passed,
    "exit_code": result.returncode, "command": command,
    "page_bits": args.page_bits,
    "source_sha256": {str(path.relative_to(core)):
        hashlib.sha256(path.read_bytes()).hexdigest() for path in
        [engine / "setprotection.c", engine / "protection.h",
         engine / "guarded_stack.c", engine / "guarded_stack.h",
         engine / "guard_fault.h", here / "fault_host.c", here / "host.c",
         here / "stack_host.c",
         here / "qemu.c", here / "entry.S", here / "image.ld", here / "run.py"]},
    "scope": "generic EL1 single-CPU fixture; not AROS MP1 acceptance"}, indent=2) + "\n")
print(serial, end="")
if not passed:
    raise SystemExit("QEMU strict verdict: FAIL")
