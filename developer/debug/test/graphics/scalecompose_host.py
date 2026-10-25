#!/usr/bin/env python3
# Copyright (C) 2026, John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License, version 1.1.
"""Exercise production presentation/input geometry against a pixel-splat oracle."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]
TEST = r'''
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "scalecompose.h"
static unsigned random_state = 0x62472891;
static unsigned next(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}
int main(void) {
    ULONG src[9 * 7], actual[39 * 31], expected[39 * 31];
    unsigned cases = 0;
    for (unsigned i = 0; i < 9 * 7; ++i) src[i] = 0xff000000 | i;
    for (unsigned run = 0; run < 20000; ++run) {
        struct ScaleComposeTransform t = {
            (LONG)(next() % 31) - 15, (LONG)(next() % 31) - 15,
            1 + next() % 4};
        struct ScaleComposeRect damage = {
            (LONG)(next() % 15) - 4, (LONG)(next() % 12) - 3,
            (LONG)(next() % 15) - 4, (LONG)(next() % 12) - 3};
        struct ScaleComposeRect clip = {
            (LONG)(next() % 47) - 4, (LONG)(next() % 39) - 4,
            (LONG)(next() % 47) - 4, (LONG)(next() % 39) - 4};
        memset(actual, 0xa7, sizeof actual);
        memcpy(expected, actual, sizeof actual);
        /* Independent forward oracle: splat each source pixel's f*f block. */
        for (LONG sy = 0; sy < 7; ++sy) for (LONG sx = 0; sx < 8; ++sx) {
            if (sx < damage.left || sx >= damage.right ||
                sy < damage.top || sy >= damage.bottom) continue;
            for (LONG dy = 0; dy < t.factor; ++dy)
            for (LONG dx = 0; dx < t.factor; ++dx) {
                LONG x = t.x + sx * t.factor + dx;
                LONG y = t.y + sy * t.factor + dy;
                if (x < 0 || x >= 37 || y < 0 || y >= 31 ||
                    x < clip.left || x >= clip.right ||
                    y < clip.top || y >= clip.bottom) continue;
                expected[y * 39 + x] = src[sy * 9 + sx];
                LONG ix, iy;
                assert(ScaleComposeInput(&t, x, y, &ix, &iy));
                assert(ix == sx && iy == sy);
            }
        }
        assert(ScaleComposePixels32(src, 8, 7, 9, actual, 37, 31, 39,
                                    &t, &damage, &clip));
        assert(memcmp(actual, expected, sizeof actual) == 0);
        /* Captured drag at the pixel just outside each top/left edge. */
        LONG ix, iy;
        assert(ScaleComposeInput(&t, t.x - 1, t.y - 1, &ix, &iy));
        assert(ix == -1 && iy == -1);
        ++cases;
    }
    struct ScaleComposeTransform t = {INT_MAX, INT_MIN, 4};
    struct ScaleComposeRect r = {0, 0, 1, 1}, p = {1, 2, 3, 4};
    assert(!ScaleComposeDamage(&t, &r, &p));
    assert(p.left == 1 && p.top == 2 && p.right == 3 && p.bottom == 4);
    LONG x = 5, y = 6;
    t.factor = 0;
    assert(!ScaleComposeInput(&t, 0, 0, &x, &y) && x == 5 && y == 6);
    t.factor = 1;
    assert(!ScaleComposeInput(&t, INT_MIN, INT_MAX, &x, &y));
    t.x = t.y = 0;
    assert(!ScaleComposePixels32(src, 8, 7, 7, actual, 37, 31, 39, &t, &r, &r));
    assert(!ScaleComposePixels32(src, 8, 7, 9, src, 8, 7, 9, &t, &r, &r));
    assert(ScaleComposeFactor(0) == 1 && ScaleComposeFactor(149) == 1);
    assert(ScaleComposeFactor(150) == 2 && ScaleComposeFactor(249) == 2);
    assert(ScaleComposeFactor(250) == 3 && ScaleComposeFactor(350) == 4);
    assert(ScaleComposeFactor(~0U) == 4);
    printf("PASS scale composition: %u oracle cases, damage/clip/pitch/input/overflow\n", cases);
}
'''
with tempfile.TemporaryDirectory(prefix="aros-scalecompose-") as directory:
    tmp = Path(directory)
    (tmp / "exec").mkdir()
    (tmp / "exec/types.h").write_text(
        "#include <stdint.h>\n"
        "typedef uint32_t ULONG; typedef int32_t LONG; typedef uint16_t UWORD;\n"
        "typedef int64_t QUAD; typedef uint64_t UQUAD; typedef uintptr_t IPTR; typedef int BOOL;\n"
        "#define TRUE 1\n#define FALSE 0\n")
    (tmp / "test.c").write_text(TEST)
    header = (ROOT / "rom/graphics/scalecompose.h").read_text()
    def build_and_run(text, negative=False):
        (tmp / "scalecompose.h").write_text(text)
        subprocess.run([os.environ.get("CC", "clang"), "-std=c99", "-Wall", "-Wextra",
                    "-Werror", "-g", "-fsanitize=address,undefined",
                    "-I" + str(tmp),
                    str(tmp / "test.c"), "-o", str(tmp / "test")], check=True)
        result = subprocess.run([str(tmp / "test")], capture_output=True, text=True)
        if negative:
            if result.returncode == 0:
                raise RuntimeError("mutated implementation escaped oracle")
        else:
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            print(result.stdout, end="")
    build_and_run(header)
    mutants = {
        "negative-drag-truncation": ("if (value % factor < 0) --q;", "/* floor correction removed */"),
        "damage-right-edge": ("logical->right * t->factor", "(logical->right - 1) * t->factor"),
        "occlusion-clip-bypass": ("if (p.left < clip->left) p.left = clip->left;", "/* clip left removed */"),
    }
    for name, (before, after) in mutants.items():
        assert header.count(before) == 1
        build_and_run(header.replace(before, after), negative=True)
        print("PASS negative control:", name)
    print("source_sha256=" + hashlib.sha256(header.encode()).hexdigest())
