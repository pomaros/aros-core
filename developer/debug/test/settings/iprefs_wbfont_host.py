#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Actual SetIPrefs screen-mode case and actual IPrefs screen-font branch, run in
the initial order of IPrefs (screen mode, then font) against a Workbench model.

The Workbench screen must carry the font preference after the initial pass:
opened later by OpenWorkBench when IPrefs found it closed, or changed live
through the same-scale SA_Scale request when an open console window keeps it
from closing. The Intuition side (OpenWorkBench, CloseWorkBench, SA_Scale) is a
model; the font transaction itself is qualified by windowframe_host.py and
preparedscale_host.py.
"""
from pathlib import Path
import subprocess
import tempfile

core = Path(__file__).resolve().parents[4]
setiprefs = (core / 'rom/intuition/setiprefs.c').read_text()
fontprefs = (core / 'workbench/c/iprefs/fontprefs.c').read_text()

def function(text, marker):
    start = text.index(marker); opening = text.index('{', start); end = opening + 1; depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[start:end]

def between(text, first, last):
    start = text.index(first)
    return text[start:text.index(last, start)]

screenmode_case = between(setiprefs, '        case IPREFS_TYPE_SCREENMODE_V37:', '        case IPREFS_TYPE_POINTER_V39:')
screenfont_case = between(fontprefs, '                        case FP_SCREENFONT:', '                        case FP_WBFONT:')
helpers = function(setiprefs, 'static BOOL ScreenModeSameRaster(') + '\n' + function(setiprefs, 'static BOOL ScreenModeScaleOnly(') + '\n'

prefix = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL; typedef uint32_t ULONG; typedef uint16_t UWORD; typedef uintptr_t IPTR;
typedef void *APTR; typedef char TEXT; typedef char *STRPTR; typedef const char *CONST_STRPTR; typedef void Object;
#define TRUE 1
#define FALSE 0
#define TAG_DONE 0
#define SA_Scale 0x80000100
#define BMA_DEPTH 4
#define MAXPUBSCREENNAME 139
#define IPREFS_TYPE_SCREENMODE_V37 2
#define FP_SCREENFONT 2
#define D(x)
#define DEBUG_SETIPREFS(x)
struct IScreenModePrefs { ULONG smp_DisplayID; UWORD smp_Width, smp_Height, smp_Depth, smp_Control; ULONG smp_Scale; };
struct TextAttr { const char *ta_Name; UWORD ta_YSize; unsigned char ta_Style, ta_Flags; };
struct TextFont { const char *name; UWORD size; };
struct ViewPort { ULONG mode; };
struct RastPort { void *BitMap; };
struct Screen { struct ViewPort ViewPort; UWORD Width, Height; struct RastPort RastPort; };
struct EasyStruct { ULONG es_StructSize, es_Flags; const char *es_Title, *es_TextFormat, *es_GadgetFormat; };
struct Process { APTR pr_WindowPtr; };
struct IntuitionBase { struct Screen *WorkBench; struct IScreenModePrefs *ScreenModePrefs; };
#define GetPrivIBase(b) (b)

static struct TextFont topaz8 = {"topaz.font", 8}, arial13 = {"arial.font", 13};
static struct TextFont *ScreenFont = &topaz8;           /* Intuition's base screen font */
static struct IScreenModePrefs defaults = {0x50000, 2560, 1600, 32, 0, 100}, *storedprefs;
static struct Screen wbscreen; static struct IntuitionBase ibase, *IntuitionBase = &ibase;
static struct TextFont *wbfont; static ULONG wbscale; static unsigned wbwindows;
static unsigned opens, wrongopens, inside_iprefs, iprefs_opens, locks, easyrequests;
static struct Process process = {(APTR)-1};

static ULONG LockIBase(ULONG n) { (void)n; assert(!locks); locks++; return 7; }
static void UnlockIBase(ULONG n) { assert(n == 7 && locks == 1); locks--; }
static void SetDisplayDefaults(struct IntuitionBase *b) { b->ScreenModePrefs = storedprefs = &defaults; }
static ULONG GetVPModeID(struct ViewPort *vp) { return vp->mode; }
static ULONG GetBitMapAttr(void *bm, ULONG attr) { (void)bm; assert(attr == BMA_DEPTH); return 32; }
static ULONG ClampScreenScale(ULONG s) { return s < 100 ? 100 : s > 400 ? 400 : s; }
static void CopyMem(const void *s, void *d, ULONG n) { memcpy(d, s, n); }
static BOOL OpenWorkBench(void)
{
    assert(!locks);
    if (ibase.WorkBench) return TRUE;
    wbscreen = (struct Screen){{defaults.smp_DisplayID}, defaults.smp_Width, defaults.smp_Height, {NULL}};
    ibase.WorkBench = &wbscreen; wbfont = ScreenFont; wbscale = ClampScreenScale(storedprefs->smp_Scale);
    opens++; wrongopens += wbfont != &arial13; iprefs_opens += inside_iprefs;
    return TRUE;
}
static BOOL CloseWorkBench(void) { assert(!locks); if (!ibase.WorkBench || wbwindows) return FALSE; ibase.WorkBench = NULL; return TRUE; }
static struct Screen *LockPubScreen(CONST_STRPTR name) { assert(!locks && !strcmp(name, "Workbench")); OpenWorkBench(); return ibase.WorkBench; }
static void UnlockPubScreen(void *n, struct Screen *s) { (void)n; assert(s == ibase.WorkBench); }
static STRPTR NextPubScreen(struct Screen *s, STRPTR name) { (void)s; if (!ibase.WorkBench) return NULL; strcpy(name, "Workbench"); return name; }
/* SetScreenScale: a same-scale request is a change when the base font differs. */
static ULONG SetAttrs(Object *o, ULONG tag, ...)
{
    va_list ap; va_start(ap, tag); IPTR scale = va_arg(ap, IPTR); assert(va_arg(ap, IPTR) == TAG_DONE); va_end(ap);
    assert(o == (Object *)ibase.WorkBench && tag == SA_Scale && !locks);
    scale = ClampScreenScale(scale);
    if (scale == wbscale && ScreenFont == wbfont) return 0;
    wbscale = scale; wbfont = ScreenFont; return 1;
}
static ULONG GetAttr(ULONG tag, Object *o, IPTR *storage) { assert(tag == SA_Scale && o == (Object *)ibase.WorkBench); *storage = wbscale; return 1; }
static ULONG EasyRequestArgs(void *w, struct EasyStruct *es, void *a, void *b) { (void)w; (void)es; (void)a; (void)b; easyrequests++; return 0; }
static ULONG ShowMessage(const char *t, const char *b, const char *g) { (void)t; (void)b; (void)g; assert(!"IPrefs raised a requester at boot"); return 0; }
static struct Process *FindTask(void *n) { (void)n; return &process; }
static void SetDefaultScreenFont(struct TextFont *f) { ScreenFont = f; }
static struct TextFont *OpenDiskFont(struct TextAttr *ta) { assert(!strcmp(ta->ta_Name, "arial.font") && ta->ta_YSize == 13); return &arial13; }
'''
wrappers = helpers + r'''
static ULONG SetIPrefsScreenMode(APTR data, ULONG length)
{
    ULONG Result = TRUE;
    ULONG lock = LockIBase(0);
    switch (IPREFS_TYPE_SCREENMODE_V37)
    {
''' + screenmode_case + r'''
    }
    UnlockIBase(lock);
    return Result;
}
static void FontPrefsScreenFont(struct TextAttr ta)
{
    struct TextFont *font;
    switch (FP_SCREENFONT)
    {
''' + screenfont_case + r'''
    }
}
'''
main = r'''
static void boot(BOOL wbopen, unsigned windows, ULONG scale)
{
    ScreenFont = &topaz8; storedprefs = NULL; ibase = (struct IntuitionBase){0}; wbfont = NULL; wbscale = 0;
    opens = wrongopens = iprefs_opens = easyrequests = 0; wbwindows = 0;
    SetDisplayDefaults(&ibase);
    if (wbopen) { OpenWorkBench(); wrongopens = opens = 0; }   /* the ROM opened it in topaz 8 */
    wbwindows = windows;                                       /* the startup Shell's console */
    inside_iprefs = 1;
    struct IScreenModePrefs prefs = defaults; prefs.smp_Scale = scale;
    assert(SetIPrefsScreenMode(&prefs, sizeof prefs));         /* "screenmode" first ... */
    FontPrefsScreenFont((struct TextAttr){"arial.font", 13, 0, 0}); /* ... then "font" */
    inside_iprefs = 0;
    OpenWorkBench();                                           /* LoadWB / Wanderer */
    assert(!locks && !easyrequests);
    if (wbfont != &arial13)
    {
        printf("Workbench screen font %s/%u after IPrefs (wanted arial.font/13)\n", wbfont->name, wbfont->size);
        assert(0);
    }
    assert(wbscale == scale && storedprefs->smp_Scale == scale && !wrongopens && iprefs_opens == (unsigned)(wbopen && !windows));
}
int main(void)
{
    for (ULONG scale = 100; scale <= 200; scale += 100)
    {
        boot(FALSE, 0, scale);  /* closed: OpenWorkBench opens it in Arial 13 */
        boot(TRUE, 0, scale);   /* open, closable: IPrefs reopens it */
        boot(TRUE, 1, scale);   /* open, held by the console: live same-scale font change */
    }
    puts("iprefs-wbfont-host PASS actual SetIPrefs screenmode + IPrefs screen font: closed/open/held Workbench x 100/200 %; Arial 13, never opened in topaz 8, opened by IPrefs only to reopen it");
    return 0;
}
'''
production = wrappers
variants = [
    ('production', production),
    ('scale-only-opens-workbench', production.replace('                if (!live)\n                    break;\n', '')),
    ('no-live-font-change', production.replace('else if (cancel)', 'else if (FALSE)')),
    ('live-font-opens-workbench', production.replace('else if (cancel)', 'else')),
]
for name, body in variants[1:]:
    assert body != production, name + ' did not apply'
with tempfile.TemporaryDirectory(prefix='iprefs-wbfont-') as temp:
    path = Path(temp); unit = path / 'fixture.c'; binary = path / 'fixture'
    for name, body in variants:
        unit.write_text(prefix + body + main)
        subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                        '-Wno-unused-parameter', '-Wno-unused-variable', '-Wno-unused-but-set-variable',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        str(unit), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], text=True, capture_output=True)
        if name == 'production':
            assert result.returncode == 0, result.stdout + result.stderr
            print(result.stdout, end='')
        else:
            assert result.returncode != 0, name + ' escaped'
            lines = (result.stdout + result.stderr).strip().splitlines()
            print('REJECTED ' + name + ': ' + (lines[0] if lines else ''))
    print('iprefs-wbfont-host mutants=%d rejected=%d' % (len(variants) - 1, len(variants) - 1))
