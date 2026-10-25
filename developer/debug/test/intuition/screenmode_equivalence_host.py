#!/usr/bin/env python3
# Copyright (C) 2026, John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License, version 1.1.
"""Compile the production SetIPrefs equivalence helpers for a host regression."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

source = Path(__file__).resolve().parents[4] / 'rom/intuition/setiprefs.c'
text = source.read_text()

def extract(name):
    start = text.index('static BOOL ' + name + '(')
    body = text.index('{', start)
    depth = 1
    end = body + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

program = r'''
#include <assert.h>
#include <stdint.h>
typedef int BOOL;
#define FALSE 0
#define TRUE 1
#define NULL ((void *)0)
struct IScreenModePrefs {
    uint32_t smp_DisplayID;
    uint16_t smp_Width, smp_Height, smp_Depth, smp_Control, smp_Scale;
};
'''
program += extract('ScreenModeSameRaster') + '\n' + extract('ScreenModeScaleOnly')
program += r'''
int main(void)
{
    struct IScreenModePrefs stored = {0x50000, 1024, 768, 24, 0, 100};
    struct IScreenModePrefs actual = {0x60000, 1280, 800, 32, 0, 100};
    struct IScreenModePrefs incoming = actual;
    incoming.smp_Scale = 150;
    assert(ScreenModeScaleOnly(&stored, &incoming, &actual));
    assert(!ScreenModeScaleOnly(&stored, &incoming, NULL));
    incoming = stored;
    incoming.smp_Scale = 200;
    assert(ScreenModeScaleOnly(&stored, &incoming, NULL));
    assert(ScreenModeScaleOnly(&stored, &incoming, &actual));

    /* A driver may change depth alone, dimensions alone, or its mode ID. */
    actual = stored; actual.smp_Depth = 32; incoming = actual;
    assert(ScreenModeScaleOnly(&stored, &incoming, &actual));
    actual = stored; actual.smp_Width = 2560; actual.smp_Height = 1600;
    incoming = actual;
    assert(ScreenModeScaleOnly(&stored, &incoming, &actual));
    actual = stored; actual.smp_DisplayID = 0x60000; incoming = actual;
    assert(ScreenModeScaleOnly(&stored, &incoming, &actual));

    /* A new raster which matches neither request nor effective mode reopens. */
    actual = stored; actual.smp_Depth = 32;
    incoming = actual; incoming.smp_DisplayID++;
    assert(!ScreenModeScaleOnly(&stored, &incoming, &actual));
    incoming = actual; incoming.smp_Width++;
    assert(!ScreenModeScaleOnly(&stored, &incoming, &actual));
    incoming = actual; incoming.smp_Height++;
    assert(!ScreenModeScaleOnly(&stored, &incoming, &actual));
    incoming = actual; incoming.smp_Depth = 16;
    assert(!ScreenModeScaleOnly(&stored, &incoming, &actual));

    /* Scale-only updates preserve both directions of autoscroll policy. */
    incoming = actual; incoming.smp_Control = 1;
    assert(!ScreenModeScaleOnly(&stored, &incoming, &actual));
    stored.smp_Control = actual.smp_Control = 1; incoming = actual;
    assert(ScreenModeScaleOnly(&stored, &incoming, &actual));
    incoming.smp_Control = 0;
    assert(!ScreenModeScaleOnly(&stored, &incoming, &actual));
    actual.smp_Control = 0; incoming = actual;
    assert(!ScreenModeScaleOnly(&stored, &incoming, &actual));
    /* Alternate Settings' nominal request and the CLI's actual raster. */
    stored = (struct IScreenModePrefs){0x50000, 1280, 800, 8, 0, 100};
    actual = stored; actual.smp_Depth = 32;
    incoming = stored; incoming.smp_Scale = 125;
    assert(ScreenModeScaleOnly(&stored, &incoming, &actual));
    stored.smp_Scale = incoming.smp_Scale;
    incoming = actual; incoming.smp_Scale = 150;
    assert(ScreenModeScaleOnly(&stored, &incoming, &actual));
    stored.smp_Scale = incoming.smp_Scale;
    assert(stored.smp_Depth == 8);
    incoming = stored; incoming.smp_Scale = 200;
    assert(ScreenModeScaleOnly(&stored, &incoming, &actual));
    stored.smp_Scale = incoming.smp_Scale;
    assert(stored.smp_Depth == 8 && stored.smp_Scale == 200);
    incoming.smp_Control = 1;
    assert(!ScreenModeScaleOnly(&stored, &incoming, &actual));
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='aros-screenmode-equivalence-') as tmp:
    c = Path(tmp) / 'check.c'
    exe = Path(tmp) / 'check'
    c.write_text(program)
    subprocess.run([*shlex.split(os.environ.get('HOST_CC', 'cc')),
                    '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print(f'screenmode equivalence: PASS {program.count("assert(")} production-C checks (ASan/UBSan)')
