#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License 1.1.
"""Exercise the actual screen-bar renderer with modeled font/graphics boundaries."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[4]
source = root / 'rom/intuition/scrdecorclass.c'
text = source.read_text()
# A disabled historical renderer precedes the active definition in this file.
matches = list(re.finditer(r'^IPTR ScrDecorClass__SDM_DRAW_SCREENBAR\([^\n]*\)\n\{', text, re.M))
assert matches, 'screen-bar renderer definition missing'
start = matches[-1].start()
begin = text.index('{', start)
end, depth = begin + 1, 1
while depth:
    depth += (text[end] == '{') - (text[end] == '}')
    end += 1
body = text[start:end]
fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef intptr_t IPTR;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef unsigned short UWORD;
typedef char TEXT;
typedef int BOOL;
typedef void Object;
#define TRUE 1
#define FALSE 0
#define D(x) do {} while (0)
#define USE_NEWDISPLAYBEEP 1
enum {BEEPING=1,SDF_DSB_TITLEONLY=2,JAM1=1,JAM2=2};
enum {BARDETAILPEN,BARBLOCKPEN,BARTRIMPEN};
enum {FSF_BOLD=1,FSF_ITALIC=2,FSF_UNDERLINED=4};
struct Font { void *tf_CharKern,*tf_CharSpace; int tf_XSize; };
struct RastPort { struct Font *Font; int TxSpacing,AlgoStyle,TxHeight,TxBaseline; };
struct Screen { int Width,BarHeight,BarHBorder,Flags; const char *Title; };
struct Dri { UWORD *dri_Pens; };
struct GfxBase { int unused; };
struct IntuitionBase { struct GfxBase *GfxBase; };
typedef struct { void *cl_UserData; } Class;
struct sdpDrawScreenBar { struct RastPort *sdp_RPort; struct Screen *sdp_Screen; struct Dri *sdp_Dri; int sdp_Flags; };
struct TextExtent { int width; };
#define GetPrivIBase(b) (b)
#define DRI(d) (d)
static int available,proportional,mode,drawn,fit_calls,rects;
static char pixels[512];
static void findtitlearea(struct Screen *s,LONG *left,LONG *right) { *left=0;*right=available+s->BarHBorder; }
static void getintframesizes(struct IntuitionBase *b,UWORD *x,UWORD *y) { (void)b;*x=*y=0; }
static int glyph(struct RastPort *r,char c) { return (proportional?(c=='W'?12:3):r->Font->tf_XSize)+r->TxSpacing; }
static ULONG TextFit(struct RastPort *r,const char *s,ULONG n,struct TextExtent *e,void *bounds,int direction,LONG width,LONG height) {
    (void)bounds;assert(direction==1&&height==r->TxHeight);++fit_calls;
    ULONG count=0;int used=0;
    while(count<n&&used+glyph(r,s[count])<=width)used+=glyph(r,s[count++]);
    e->width=used;return count;
}
static void SetDrMd(struct RastPort *r,int m) { (void)r;mode=m; }
static void SetAPen(struct RastPort *r,UWORD pen) { (void)r;(void)pen; }
static void SetBPen(struct RastPort *r,UWORD pen) { (void)r;(void)pen; }
static void RectFill(struct RastPort *r,int x,int y,int x2,int y2) { (void)r;(void)x;(void)y;(void)x2;(void)y2;++rects; }
static void Move(struct RastPort *r,int x,int y) { (void)r;(void)x;(void)y; }
static void Text(struct RastPort *r,const char *s,ULONG n) {
    int width=0;assert(n<sizeof(pixels));for(ULONG i=0;i<n;i++)width+=glyph(r,s[i]);
    assert((width<=available||!n)&&"text fits available title rectangle");
    memcpy(pixels,s,n);pixels[n]=0;drawn=n;
}
/* RENDERER */
static void render(int width,int font_width,int spacing,int prop,int flags,const char *title) {
    struct Font font={0};font.tf_XSize=font_width;if(prop)font.tf_CharKern=&font;
    struct RastPort rp={&font,spacing,0,12,9};struct Screen screen={4096,16,2,0,title};
    UWORD pens[]={0,1,2};struct Dri dri={pens};struct IntuitionBase base={0};Class cl={&base};
    struct sdpDrawScreenBar msg={&rp,&screen,&dri,flags};
    available=width;proportional=prop;mode=drawn=fit_calls=rects=0;memset(pixels,0,sizeof(pixels));
    assert(ScrDecorClass__SDM_DRAW_SCREENBAR(&cl,NULL,&msg));
}
int main(void) {
    render(36,6,0,0,0,"abcdefghij");assert(drawn==6&&!strcmp(pixels,"abcdef")&&"font metrics select fitting glyph count");
    render(36,12,0,0,0,"abcdefghij");assert(drawn==3);
    render(20,8,0,1,0,"WiWiWi");assert(drawn==2&&!strcmp(pixels,"Wi"));
    render(39,8,2,0,0,"abcdefghij");assert(drawn==3&&mode==JAM1);
    render(64,8,0,0,SDF_DSB_TITLEONLY,"X");
    assert(drawn==8&&!strcmp(pixels,"X       ")&&mode==JAM2&&rects==1&&"fixed path erases full previous title");
    render(64,8,0,0,SDF_DSB_TITLEONLY,NULL);assert(drawn==8&&!strcmp(pixels,"        "));
    render(0,12,0,0,0,"title");assert(!drawn&&!fit_calls);
    render(-3,12,0,0,0,"title");assert(!drawn&&!fit_calls);
    char title[301];memset(title,'x',300);title[300]=0;
    render(200,1,0,0,0,title);assert(drawn==200);
    render(2048,8,0,0,SDF_DSB_TITLEONLY,title);assert(drawn==128);
    puts("PASS: actual screen title renderer, ten font/geometry/erase cases");
}
'''
unit = fixture.replace('/* RENDERER */', body)
old_fit = body[body.index('    if (titleWidth > 0'):body.index('\n    D(bug("[SCRDECOR] %s: Title_Left')]
mutants = {
    'fixed-eight-fit': (old_fit, '    if (titleWidth > 0) titleChars=titleWidth/8;', 'font metrics select fitting glyph count'),
    'lost-erase-padding': ('        titleChars = titleWidth > 0 ? titleWidth / 8 : 0;', '', 'fixed path erases full previous title'),
    'lost-buffer-cap': ('            if (titleChars > sizeof(titleLine)) titleChars = sizeof(titleLine);', '', 'stack-buffer-overflow'),
}
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
rows = []
for name, change in [('production', None), *mutants.items()]:
    code = unit
    if change:
        old, new, cause = change
        assert code.count(old) == 1, name
        code = code.replace(old, new)
    path = out / (name + '.c')
    path.write_text(code)
    cmd = ['clang', '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-unused-parameter', '-g', '-fsanitize=address,undefined', str(path), '-o', str(out / name)]
    compiled = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    (out / (name + '-compile.log')).write_text(compiled.stdout + compiled.stderr)
    row = {'name': name, 'command': cmd, 'compile_exit': compiled.returncode, 'pass': False}
    if not compiled.returncode:
        ran = subprocess.run([str(out / name)], capture_output=True, text=True, timeout=15)
        (out / (name + '.log')).write_text(ran.stdout + ran.stderr)
        row['run_exit'] = ran.returncode
        row['pass'] = (ran.returncode != 0 and cause in ran.stderr) if change else (ran.returncode == 0 and ran.stdout.startswith('PASS:') and not ran.stderr)
    rows.append(row)
result = {'result': 'PASS' if all(r['pass'] for r in rows) else 'FAIL', 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(), 'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), 'records': rows, 'scope': 'Actual screen-bar renderer; modeled font metrics and graphics calls. No Graphics TextFit implementation, pixels, SMP or native acceptance.'}
(out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
print(result['result'])
raise SystemExit(result['result'] != 'PASS')
