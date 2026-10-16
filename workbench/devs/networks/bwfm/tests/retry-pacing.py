#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Exercise production retry pacing with a permanently latched wake signal."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / 'bwfm_dev.c').read_text()
start = source.index('static void wait_autojoin_retry(')
end = source.index('\n}\n', start) + 3
function = source[start:end]
prelude = r'''
#include <assert.h>
typedef unsigned long ULONG;
struct bwfm_unit { int autojoin_retry, online, joined; };
static unsigned ticks, calls, action;
static void Delay(ULONG n) { assert(n == 5); ticks += n; }
static void do_associate(struct bwfm_unit *u) {
    calls++;
    if (action == 1) u->autojoin_retry = 0;
    if (action == 2) u->online = 0;
    if (action == 3) u->joined = 1;
}
'''
checks = r'''
int main(void) {
    struct bwfm_unit u = {1, 1, 0};
    /* A pending/self-generated signal cannot accelerate repeated failures. */
    for (unsigned retry = 0; retry < 4; retry++) wait_autojoin_retry(&u);
    assert(ticks == 1000 && calls == 200);
    for (action = 1; action <= 3; action++) {
        u = (struct bwfm_unit){1, 1, 0}; ticks = calls = 0;
        wait_autojoin_retry(&u);
        assert(ticks == 5 && calls == 1);
        wait_autojoin_retry(&u);
        assert(ticks == 5 && calls == 1);
    }
    return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    c = Path(d) / 'retry.c'
    exe = Path(d) / 'retry'
    c.write_text(prelude + function + checks)
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: repeated failures wait five seconds; explicit association, offline and joined cancel within 100 ms')
