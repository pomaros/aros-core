#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""The task crash policy of the Apple kernel (task_crash_policy.h) on the host.

Drives the actual header, unmodified, through:
  - the decision: the one safe state suspends; every single unsafe input
    (kill switch, EL1 or SVC or SError vector, runtime proof, interrupt
    depth, scheduler not live, no task, no ETask, task not running, IRQ
    exclusion, own or list lock held, no context) halts with its own
    verdict; Forbid()/Disable() nesting are no inputs at all;
  - the fault description for the ESRs of real crashes (the gen51
    input.device data abort, a branch to NULL, a write to a read-only page,
    a breakpoint, an undefined instruction);
  - the placement of addresses in a module map built here from a real
    module ELF (gfx.hidd): the gen51 ELR and LR come back as
    gfx.hidd .text+0x13500 BM__Hidd_BitMap__CopyMemBox32+0xa4 and
    .text+0x2c3c GFXHIDD__Hidd_Gfx__CopyBox+..., and every function symbol
    of the module is found at offset 0 and +4 against an independent parse;
  - the frame-pointer walk over a fake stack: a good chain, a cycle, a
    frame leaving the stack, a misaligned frame, an unreadable word and
    the depth bound.
Then mutants of the header (each removing one guard) must be rejected.
No Exec, no target: the kernel glue (task_crash.c) is compiled for AArch64
by the run's compile_objects.py.
"""
from pathlib import Path
import argparse, json, struct, subprocess, sys, tempfile

ROOT = Path(__file__).resolve().parents[4]
HEADER = ROOT / 'arch/aarch64-apple/kernel/task_crash_policy.h'
INCLUDE = ROOT / 'arch/aarch64-apple/include'
# The gfx.hidd of generation 51's bundle, the module of the gen51 crash.
DEFAULT_MODULE = Path('/Users/jkn/Source/aros-project/aros-apple-silicon/'
                      'run/display-scale/gen51-crash/gfx.hidd')
GEN51_TEXT = 0xb1bb44348   # gfx.hidd .text in the gen51 crash (run/display-scale/gen51-crash)


def elf_functions(path):
    data = path.read_bytes()
    shoff = struct.unpack_from('<Q', data, 0x28)[0]
    shnum, shstrndx = struct.unpack_from('<HH', data, 0x3c)
    sh = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + i * 64) for i in range(shnum)]
    strtab = sh[shstrndx][4]
    secname = lambda off: data[strtab + off:data.index(b'\0', strtab + off)].decode()
    text = next(i for i, s in enumerate(sh) if secname(s[0]) == '.text')
    funcs = []
    for s in sh:
        if s[1] != 2:
            continue
        names = sh[s[6]][4]
        for k in range(s[5] // 24):
            nm, info, _o, shn, val, size = struct.unpack_from('<IBBHQQ', data, s[4] + k * 24)
            if info & 15 == 2 and shn == text:
                name = data[names + nm:data.index(b'\0', names + nm)].decode()
                if name:
                    funcs.append((val, size, name))
    funcs.sort(key=lambda f: f[0])
    return sh[text][5], funcs


def c_string(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


FIXTURE = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "task_crash_policy.h"

static int failures;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL "); printf(__VA_ARGS__); printf("\n"); ++failures; } } while (0)

static struct AROSAppleCrashState safe(void)
{
    struct AROSAppleCrashState s = {0};
    s.vector = 8; s.esr = UINT64_C(0x92000007); s.scheduler_live = 1;
    s.have_task = 1; s.task_is_etask = 1; s.task_state = 2; s.context_saveable = 1;
    return s;
}

static void decision(void)
{
    struct AROSAppleCrashState s = safe();
    CHECK(AROSAppleCrashDecide(&s) == AROS_APPLE_CRASH_SUSPEND, "safe state not suspended");
#define FLIP(field, value, verdict) do { s = safe(); s.field = value; \
    CHECK(AROSAppleCrashDecide(&s) == verdict, "%s=%llu gave %u, want %u", #field, \
        (unsigned long long)(value), AROSAppleCrashDecide(&s), (unsigned)verdict); } while (0)
    FLIP(kill_switch, 1, AROS_APPLE_CRASH_HALT_KILL_SWITCH);
    FLIP(vector, 4, AROS_APPLE_CRASH_HALT_NOT_TASK);      /* sync EL1h */
    FLIP(vector, 0, AROS_APPLE_CRASH_HALT_NOT_TASK);      /* sync EL1t */
    FLIP(vector, 11, AROS_APPLE_CRASH_HALT_NOT_TASK);     /* SError EL0 */
    FLIP(vector, 12, AROS_APPLE_CRASH_HALT_NOT_TASK);     /* AArch32 */
    FLIP(esr, UINT64_C(0x56000000), AROS_APPLE_CRASH_HALT_NOT_TASK); /* SVC64 */
    FLIP(esr, UINT64_C(0xbe000000), AROS_APPLE_CRASH_HALT_NOT_TASK); /* SError class */
    FLIP(proof_active, 1, AROS_APPLE_CRASH_HALT_PROOF);
    FLIP(interrupt_depth, 1, AROS_APPLE_CRASH_HALT_INTERRUPT);
    FLIP(scheduler_live, 0, AROS_APPLE_CRASH_HALT_EARLY);
    FLIP(have_task, 0, AROS_APPLE_CRASH_HALT_NO_TASK);
    FLIP(task_is_etask, 0, AROS_APPLE_CRASH_HALT_NO_TASK);
    FLIP(task_state, 4, AROS_APPLE_CRASH_HALT_TASK_STATE);
    FLIP(task_state, 6, AROS_APPLE_CRASH_HALT_TASK_STATE);
    FLIP(irq_mask_depth, 1, AROS_APPLE_CRASH_HALT_IRQ_EXCLUSION);
    FLIP(task_lock_held, 1, AROS_APPLE_CRASH_HALT_LOCK_HELD);
    FLIP(list_lock_held, 1, AROS_APPLE_CRASH_HALT_LOCK_HELD);
    FLIP(context_saveable, 0, AROS_APPLE_CRASH_HALT_NO_CONTEXT);
    /* Every EL0 fault class that is a task's own suspends. */
    { static const unsigned ec[] = {0x00,0x07,0x0d,0x0e,0x18,0x1c,0x20,0x22,0x24,0x26,0x2c,0x30,0x32,0x34,0x3c};
      for (unsigned i = 0; i < sizeof(ec)/sizeof(ec[0]); ++i) {
          s = safe(); s.esr = (uint64_t)ec[i] << 26;
          CHECK(AROSAppleCrashDecide(&s) == AROS_APPLE_CRASH_SUSPEND, "ec %#x not suspended", ec[i]); } }
    CHECK(AROSAppleCrashTrapNumber(UINT64_C(0x92000007)) == 2, "data abort trap");
    CHECK(AROSAppleCrashTrapNumber(UINT64_C(0x82000006)) == 3, "instruction abort trap");
    CHECK(AROSAppleCrashTrapNumber(UINT64_C(0xf2000001)) == 4, "brk trap");
    CHECK(AROSAppleCrashTrapNumber(UINT64_C(0x62000000)) == 8, "sysreg trap");
    for (unsigned v = 0; v < AROS_APPLE_CRASH_VERDICTS; ++v)
        CHECK(strcmp(AROSAppleCrashVerdictName(v), "?") != 0, "verdict %u unnamed", v);
}

static void describe(uint64_t esr, uint64_t far, uint64_t elr, const char *want)
{
    char out[AROS_APPLE_CRASH_FAULT_BYTES];
    struct AROSAppleCrashText t;
    crash_text_init(&t, out, sizeof(out));
    AROSAppleCrashDescribe(&t, esr, far, elr);
    CHECK(strncmp(out, want, strlen(want)) == 0, "esr %#llx: '%s' does not start with '%s'",
        (unsigned long long)esr, out, want);
    CHECK(strlen(out) < sizeof(out), "description overflow");
}

static void descriptions(void)
{
    describe(UINT64_C(0x92000007), UINT64_C(0x804d2ee9c), 0, "data read from unmapped 0x804d2ee9c, level 3 [ec=0x24 fsc=0x7 wnr=0");
    describe(UINT64_C(0x92000046), UINT64_C(0x804d2ee9c), 0, "data write to unmapped 0x804d2ee9c, level 2");
    describe(UINT64_C(0x82000006), 0x10, 0x10, "instruction fetch at NULL+0x10 (branch to 0x10)");
    describe(UINT64_C(0x8200000f), UINT64_C(0x40001000), 0x40001000, "instruction fetch from non-executable 0x40001000");
    describe(UINT64_C(0x9200004f), UINT64_C(0x1f000), 0, "data write: permission fault on 0x1f000 (read-only or no-access), level 3");
    describe(UINT64_C(0x92000006), 0x10, 0, "data read through NULL+0x10, unmapped, level 2");
    describe(UINT64_C(0x93850007), UINT64_C(0x804d2ee9c), 0, "data read from unmapped 0x804d2ee9c, level 3 [ec=0x24 fsc=0x7 wnr=0 size=4 reg=x5");
    describe(UINT64_C(0xf2000123), 0, 0, "breakpoint brk #0x123");
    describe(UINT64_C(0x02000000), 0, 0x4000, "undefined instruction at 0x4000");
    describe(UINT64_C(0x8a000000), 0x1003, 0, "misaligned PC 0x1003");
    describe(UINT64_C(0x92000021), UINT64_C(0x40001001), 0, "data read: misaligned 0x40001001");
    /* A tiny buffer never overflows. */
    { char small[8]; struct AROSAppleCrashText t; crash_text_init(&t, small, sizeof(small));
      AROSAppleCrashDescribe(&t, UINT64_C(0x92000007), UINT64_C(0x804d2ee9c), 0);
      CHECK(strlen(small) == 7, "small buffer length %zu", strlen(small)); }
}

@MAP@

static void place(uint64_t address, const char *module, const char *symbol, uint64_t symbol_offset,
    uint64_t offset)
{
    struct AROSAppleCrashPlace p;
    int found = AROSAppleCrashPlaceModule(&map, address, &p);
    CHECK(found == (module != NULL), "%#llx found=%d", (unsigned long long)address, found);
    if (!found || module == NULL) return;
    CHECK(strcmp(p.module, module) == 0 && strcmp(p.section, ".text") == 0 && p.offset == offset,
        "%#llx placed %s %s+%#llx", (unsigned long long)address, p.module, p.section,
        (unsigned long long)p.offset);
    CHECK(strcmp(p.symbol, symbol) == 0 && p.symbol_offset == symbol_offset,
        "%#llx symbol %s+%#llx, want %s+%#llx", (unsigned long long)address, p.symbol,
        (unsigned long long)p.symbol_offset, symbol, (unsigned long long)symbol_offset);
}

static void placements(void)
{
    char line[200]; struct AROSAppleCrashText t; struct AROSAppleCrashPlace p;
    place(TEXT + 0x13500, "gfx.hidd", "BM__Hidd_BitMap__CopyMemBox32", 0xa4, 0x13500);
    AROSAppleCrashPlaceModule(&map, TEXT + 0x13500, &p);
    crash_text_init(&t, line, sizeof(line)); AROSAppleCrashFormatPlace(&t, &p);
    CHECK(strcmp(line, "gfx.hidd .text+0x13500 BM__Hidd_BitMap__CopyMemBox32+0xa4") == 0, "format '%s'", line);
    AROSAppleCrashPlaceModule(&map, TEXT + 0x2c3c, &p);
    CHECK(strcmp(p.symbol, "GFXHIDD__Hidd_Gfx__CopyBox") == 0, "gen51 LR in %s", p.symbol);
    for (unsigned i = 0; i < sizeof(expect) / sizeof(expect[0]); ++i) {
        place(TEXT + expect[i].offset, "gfx.hidd", expect[i].name, 0, expect[i].offset);
        if (expect[i].size > 4)
            place(TEXT + expect[i].offset + 4, "gfx.hidd", expect[i].name, 4, expect[i].offset + 4);
    }
    /* Past a sized symbol, in the gap before the next: no symbol. */
    { static const struct AROSAppleModuleSymbol gs[] = {{0x100, 8, 0}, {0x200, 0, 2}};
      static const struct AROSAppleModuleCode gc[] = {{0x5000, 0x400, 0, 2, 0, 1, 0, "gap", ".text"}};
      struct AROSAppleModuleMap gm = {AROS_APPLE_MODULE_MAP_VERSION, sizeof(gm), 1, 2, 4, 0, gc, gs, "a\0b"};
      CHECK(AROSAppleCrashPlaceModule(&gm, 0x5104, &p) && strcmp(p.symbol, "a") == 0 && p.symbol_offset == 4, "inside sized symbol");
      CHECK(AROSAppleCrashPlaceModule(&gm, 0x5180, &p) && p.symbol[0] == 0 && p.offset == 0x180, "gap took %s", p.symbol);
      CHECK(AROSAppleCrashPlaceModule(&gm, 0x5300, &p) && strcmp(p.symbol, "b") == 0 && p.symbol_offset == 0x100, "unsized symbol");
      CHECK(AROSAppleCrashPlaceModule(&gm, 0x5010, &p) && p.symbol[0] == 0, "before the first symbol"); }
    place(TEXT - 1, NULL, NULL, 0, 0);
    place(TEXT + TEXT_SIZE, NULL, NULL, 0, 0);
    place(0x10, NULL, NULL, 0, 0);
    /* Not-a-map and a corrupt map are refused, never followed. */
    { struct AROSAppleModuleMap bad = map; bad.version = 7;
      CHECK(!AROSAppleCrashPlaceModule(&bad, TEXT + 0x13500, &p) && p.module[0] == 0, "bad version followed");
      CHECK(!AROSAppleCrashPlaceModule(NULL, TEXT, &p), "NULL map followed");
      bad = map; bad.code_count = AROS_APPLE_MODULE_MAP_MAX_CODE + 1;
      CHECK(!AROSAppleCrashPlaceModule(&bad, TEXT, &p), "oversized map followed");
      struct AROSAppleModuleCode c = code[1]; bad = map; bad.code = &c; bad.code_count = 1;
      c.symbol_first = map.symbol_count - 2; c.symbol_count = 5;
      CHECK(AROSAppleCrashPlaceModule(&bad, TEXT + 0x13500, &p) && p.symbol[0] == 0, "symbol range beyond the table followed"); }
}

/* Fake memory: a 4 KiB stack at 0x10000, 8-byte words. */
static uint64_t mem[512];
static int unreadable = -1;
static int fake_read(void *ctx, uint64_t address, uint64_t *value)
{
    (void)ctx;
    /* Word-aligned reads succeed: the walk's own 8-byte rule is tested. */
    if (address < 0x10000 || address + 8 > 0x11000 || (address & 3)) return 0;
    if ((int)((address - 0x10000) / 8) == unreadable) return 0;
    memcpy(value, (const char *)mem + (address - 0x10000), 8);
    return 1;
}
static void frame(uint64_t at, uint64_t next, uint64_t ret) { mem[(at - 0x10000) / 8] = next; mem[(at - 0x10000) / 8 + 1] = ret; }

static void walks(void)
{
    uint64_t r[8]; uint32_t n;
    memset(mem, 0, sizeof(mem));
    frame(0x10100, 0x10200, 0xa1); frame(0x10200, 0x10300, 0xa2); frame(0x10300, 0, 0xa3);
    n = AROSAppleCrashWalk(0x10100, 0x10000, 0x11000, fake_read, NULL, r, 8);
    CHECK(n == 3 && r[0] == 0xa1 && r[1] == 0xa2 && r[2] == 0xa3, "good chain gave %u", n);
    frame(0x10300, 0x10100, 0xa3);          /* cycle back down */
    n = AROSAppleCrashWalk(0x10100, 0x10000, 0x11000, fake_read, NULL, r, 8);
    CHECK(n == 3, "cycle gave %u", n);
    frame(0x10300, 0x10300, 0xa3);          /* self loop */
    n = AROSAppleCrashWalk(0x10100, 0x10000, 0x11000, fake_read, NULL, r, 8);
    CHECK(n == 3, "self loop gave %u", n);
    frame(0x10300, 0x20000, 0xa3);          /* leaves the stack */
    n = AROSAppleCrashWalk(0x10100, 0x10000, 0x11000, fake_read, NULL, r, 8);
    CHECK(n == 3, "frame outside the stack gave %u", n);
    n = AROSAppleCrashWalk(0x10100, 0x10000, 0x10300, fake_read, NULL, r, 8);
    CHECK(n == 2, "bounds below the frame gave %u", n);
    frame(0x10300, 0x10404, 0xa3);          /* misaligned, with a plausible frame there */
    { uint64_t v[2] = {0x10500, 0xdead}; memcpy((char *)mem + 0x404, v, sizeof(v)); }
    n = AROSAppleCrashWalk(0x10100, 0x10000, 0x11000, fake_read, NULL, r, 8);
    CHECK(n == 3, "misaligned frame gave %u", n);
    frame(0x10300, 0, 0xa3); unreadable = (0x10208 - 0x10000) / 8;
    n = AROSAppleCrashWalk(0x10100, 0x10000, 0x11000, fake_read, NULL, r, 8);
    CHECK(n == 1, "unreadable return gave %u", n); unreadable = -1;
    for (unsigned i = 0; i < 20; ++i) frame(0x10100 + 0x20 * i, 0x10100 + 0x20 * (i + 1), 0xb0 + i);
    n = AROSAppleCrashWalk(0x10100, 0x10000, 0x11000, fake_read, NULL, r, 8);
    CHECK(n == 8 && r[7] == 0xb7, "depth bound gave %u", n);
    n = AROSAppleCrashWalk(0x10100, 0x11000, 0x10000, fake_read, NULL, r, 8);
    CHECK(n == 0, "inverted bounds gave %u", n);
    n = AROSAppleCrashWalk(0, 0x10000, 0x11000, fake_read, NULL, r, 8);
    CHECK(n == 0, "NULL frame pointer gave %u", n);
}

int main(void)
{
    decision(); descriptions(); placements(); walks();
    if (failures) { printf("FAIL %d checks\n", failures); return 1; }
    printf("PASS taskcrash decision, descriptions, %u placements, walks\n", (unsigned)(sizeof(expect) / sizeof(expect[0])));
    return 0;
}
'''

MUTANTS = [
    ('no-kill-switch', 'if (s->kill_switch)\n        return AROS_APPLE_CRASH_HALT_KILL_SWITCH;', ''),
    ('any-vector', 's->vector != CRASH_VECTOR_SYNC_EL0 || ', ''),
    ('svc-suspends', 'ec == CRASH_EC_SVC64 ||', ''),
    ('serror-suspends', ' ||\n            ec == CRASH_EC_SVC32 || ec == CRASH_EC_SERROR)', ' ||\n            ec == CRASH_EC_SVC32)'),
    ('no-proof', 'if (s->proof_active)\n        return AROS_APPLE_CRASH_HALT_PROOF;', ''),
    ('no-interrupt-depth', 'if (s->interrupt_depth != 0)', 'if (0)'),
    ('no-early', 'if (!s->scheduler_live)', 'if (0)'),
    ('no-etask', '!s->have_task || !s->task_is_etask', '!s->have_task'),
    ('any-state', 'if (s->task_state != CRASH_TS_RUN)', 'if (0)'),
    ('no-irq-exclusion', 'if (s->irq_mask_depth != 0)', 'if (0)'),
    ('no-list-lock', 's->task_lock_held || s->list_lock_held', 's->task_lock_held'),
    ('no-task-lock', 's->task_lock_held || s->list_lock_held', 's->list_lock_held'),
    ('no-context', 'if (!s->context_saveable)', 'if (0)'),
    ('wnr-inverted', '(iss & 0x40U) ? "data write" : "data read"', '(iss & 0x40U) ? "data read" : "data write"'),
    ('null-page-ignored', 'if (far < CRASH_NULL_PAGE) {\n            crash_text_str(t, "instruction', 'if (0) {\n            crash_text_str(t, "instruction'),
    ('search-off-by-one', 'if (s[mid].offset <= p->offset)', 'if (s[mid].offset < p->offset)'),
    ('symbol-size-ignored', '(hit->size == 0 || into < hit->size) &&', ''),
    ('code-end-inclusive', 'address - c->start < c->size', 'address - c->start <= c->size'),
    ('map-unchecked', 'if (!crash_map_valid(map))\n        return 0;', 'if (map == 0)\n        return 0;'),
    ('symbol-range-unchecked', 'code->symbol_count <= map->symbol_count - code->symbol_first', '1'),
    ('walk-no-monotonic', '(previous != 0 && fp <= previous)', '(previous != 0 && 0)'),
    ('walk-allows-equal', '(previous != 0 && fp <= previous)', '(previous != 0 && fp < previous)'),
    ('walk-no-upper', 'fp > upper - 16U ||', ''),
    ('walk-no-align', '(fp & 7U) != 0 || ', ''),
    ('walk-unchecked-read', 'if (!read(context, fp, &next) || !read(context, fp + 8U, &ret))\n            break;',
     'read(context, fp, &next); read(context, fp + 8U, &ret);'),
    ('trap-bus-error', 'case CRASH_EC_DABORT_LOWER:\n        return 2;', 'case CRASH_EC_DABORT_LOWER:\n        return 3;'),
    ('text-overflow', 't->length + 1U >= t->capacity', 't->length >= t->capacity'),
]


def build(work, header_text, map_source, cc):
    (work / 'task_crash_policy.h').write_text(header_text)
    (work / 'fixture.c').write_text(FIXTURE.replace('@MAP@', map_source))
    exe = work / 'fixture'
    p = subprocess.run([cc, '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        '-I', str(work), '-I', str(INCLUDE), str(work / 'fixture.c'), '-o', str(exe)],
                       capture_output=True, text=True)
    if p.returncode:
        return 'compile', p.stdout + p.stderr
    r = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
    return ('pass' if r.returncode == 0 else 'fail'), r.stdout + r.stderr


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--module', type=Path, default=DEFAULT_MODULE, help='gfx.hidd ELF')
    ap.add_argument('--mutations', action='store_true')
    ap.add_argument('--out', type=Path)
    ap.add_argument('--cc', default='cc')
    a = ap.parse_args()
    size, funcs = elf_functions(a.module)
    names = sorted({f[2] for f in funcs})
    # Distinct offsets only for the per-symbol expectation: aliases share one.
    by_offset = {}
    for off, sz, name in funcs:
        by_offset.setdefault(off, (sz, name))
    strings, index = [], {}
    pos = 0
    for n in names:
        index[n] = pos
        strings.append(n)
        pos += len(n) + 1
    syms = ',\n'.join('{%#x,%#x,%d}' % (o, s, index[n]) for o, s, n in funcs)
    blob = '"' + ''.join(n.replace('\\', '\\\\').replace('"', '\\"') + '\\0' for n in strings) + '"'
    expect = []
    for off in sorted(by_offset):
        # The lookup returns the last symbol at an offset; take that one.
        last = [f for f in funcs if f[0] == off][-1]
        expect.append('{%#x,%#x,%s}' % (off, last[1], c_string(last[2])))
    map_source = '''
#define TEXT UINT64_C(%#x)
#define TEXT_SIZE UINT64_C(%#x)
static const struct AROSAppleModuleSymbol symbols[] = {%s};
static const char strings[] = %s;
static const struct AROSAppleModuleCode code[] = {
    {TEXT - 0x100000, 0x1000, 0, 0, 0, 1, 0, "kernel.resource", ".text"},
    {TEXT, TEXT_SIZE, 0, %d, 1, 1, 0, "gfx.hidd", ".text"},
};
static const struct AROSAppleModuleMap map = {AROS_APPLE_MODULE_MAP_VERSION, sizeof(struct AROSAppleModuleMap),
    2, %d, sizeof(strings), 0, code, symbols, strings};
static const struct { uint32_t offset, size; const char *name; } expect[] = {%s};
''' % (GEN51_TEXT, size, syms, blob, len(funcs), len(funcs), ',\n'.join(expect))
    header = HEADER.read_text()
    results = {'module': str(a.module), 'functions': len(funcs), 'header': str(HEADER)}
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        verdict, out = build(work, header, map_source, a.cc)
        print(out.strip().splitlines()[-1] if out.strip() else verdict)
        results['baseline'] = {'verdict': verdict, 'output': out[-4000:]}
        ok = verdict == 'pass'
        if a.mutations:
            results['mutants'] = []
            for name, old, new in MUTANTS:
                if old not in header:
                    print('FAIL mutant %s: pattern not in the header' % name)
                    ok = False
                    continue
                v, o = build(work, header.replace(old, new, 1), map_source, a.cc)
                rejected = v == 'fail'  # a runtime check caught it, not the compiler
                results['mutants'].append({'name': name, 'verdict': v, 'rejected': rejected,
                                           'evidence': (o.strip().splitlines() or [''])[0][:300]})
                print(('REJECT' if rejected else 'FAIL survived'), name, '-', (o.strip().splitlines() or [''])[0][:160])
                ok &= rejected
    results['status'] = 'PASS' if ok else 'FAIL'
    if a.out:
        a.out.mkdir(parents=True, exist_ok=True)
        (a.out / 'taskcrash_host.json').write_text(json.dumps(results, indent=2) + '\n')
    print(results['status'])
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
