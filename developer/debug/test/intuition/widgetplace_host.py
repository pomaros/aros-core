#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""The actual native widget arrangement (widgetplace.h) on real panels.

For each panel and every scale 100..400 % with the screen bar it carries, the
Energy, status and Magnifier slots lie inside the logical screen below the
bar, never overlap, keep the column left of Energy and Magnifier below the
status widget, leave at least two fifths of the screen width to the desktop,
and Energy keeps its 772-unit page plus frame wherever that three-fifths
share holds it next to the minimum column. 2560x1600 at 100 % is the design
arrangement. The placement record keeps an automatic widget automatic, takes
a user move or size as the user's geometry and returns it unchanged through
100 -> 200 -> 100. Mutants of the layout and of the record are rejected.
No window system: Intuition's fit of the slot is qualified by
nativewidgets_host.py and magnifierscale_host.py.
"""
from pathlib import Path
import argparse, hashlib, json, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[4]
HEADER = 'arch/aarch64-apple/include/widgetplace.h'

PREFIX = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef int32_t LONG;typedef uint32_t ULONG;typedef int16_t WORD;typedef uint16_t UWORD;typedef int BOOL;
#define TRUE 1
#define FALSE 0
struct Screen{WORD Width,Height;};
struct ScreenRenderContext{UWORD src_Scale;ULONG src_PhysicalBarHeight;};
struct Window{WORD LeftEdge,TopEdge,Width,Height;};
'''
MAIN = r'''
static void fail(const char*what,int w,int h,unsigned scale){printf("FAIL %s on %dx%d at %u%%\n",what,w,h,scale);abort();}
static BOOL apart(const struct WidgetRect*a,const struct WidgetRect*b){
 return a->Left+a->Width<=b->Left||b->Left+b->Width<=a->Left||a->Top+a->Height<=b->Top||b->Top+b->Height<=a->Top;}
static unsigned cases;
static void arrangement(int pw,int ph,unsigned scale,ULONG bar){
 struct Screen screen={(WORD)pw,(WORD)ph};struct ScreenRenderContext c={(UWORD)scale,bar};struct WidgetRect r[3];
 LONG lw=pw*100/(LONG)scale,lh=ph*100/(LONG)scale,top=((LONG)bar*100+scale-1)/scale+2;
 for(ULONG s=0;s<3;s++)if(!WidgetScreenSlot(&screen,&c,s,&r[s]))fail("no slot",pw,ph,scale);
 for(int s=0;s<3;s++){if(r[s].Left<0||r[s].Top<top||r[s].Width<=0||r[s].Height<=0||r[s].Left+r[s].Width>lw||r[s].Top+r[s].Height>lh)fail("slot outside the screen or over the bar",pw,ph,scale);}
 if(!apart(&r[0],&r[1])||!apart(&r[0],&r[2])||!apart(&r[1],&r[2]))fail("slots overlap",pw,ph,scale);
 if(r[WIDGET_SLOT_ALIVE].Left+r[WIDGET_SLOT_ALIVE].Width+WIDGET_GAP!=r[WIDGET_SLOT_ENERGY].Left||r[WIDGET_SLOT_ENERGY].Left+r[WIDGET_SLOT_ENERGY].Width+WIDGET_GAP!=lw)fail("column not against Energy, or Energy not against the right edge",pw,ph,scale);
 if(r[WIDGET_SLOT_MAGNIFIER].Left!=r[WIDGET_SLOT_ALIVE].Left||r[WIDGET_SLOT_MAGNIFIER].Width!=r[WIDGET_SLOT_ALIVE].Width||r[WIDGET_SLOT_MAGNIFIER].Top!=r[WIDGET_SLOT_ALIVE].Top+r[WIDGET_SLOT_ALIVE].Height+WIDGET_GAP)fail("Magnifier not below the status widget",pw,ph,scale);
 LONG share=lw-2*WIDGET_GAP<lw*3/5?lw-2*WIDGET_GAP:lw*3/5;
 if(r[WIDGET_SLOT_ALIVE].Left<lw-WIDGET_GAP-share-WIDGET_GAP)fail("widgets wider than three fifths of the screen",pw,ph,scale);
 if(share>=WIDGET_ENERGY_WIDTH+WIDGET_COLUMN_MINIMUM&&r[WIDGET_SLOT_ENERGY].Width!=WIDGET_ENERGY_WIDTH)fail("Energy narrower than its page although the share holds it",pw,ph,scale);
 if(r[WIDGET_SLOT_ALIVE].Width>WIDGET_COLUMN_WIDTH||r[WIDGET_SLOT_ALIVE].Height>WIDGET_ALIVE_HEIGHT||r[WIDGET_SLOT_MAGNIFIER].Height>WIDGET_MAGNIFIER_HEIGHT)fail("slot beyond its design size",pw,ph,scale);
 if(lh-top>=WIDGET_ALIVE_HEIGHT+WIDGET_GAP+WIDGET_MAGNIFIER_HEIGHT&&(r[WIDGET_SLOT_ALIVE].Height!=WIDGET_ALIVE_HEIGHT||r[WIDGET_SLOT_MAGNIFIER].Height!=WIDGET_MAGNIFIER_HEIGHT))fail("column shrank although the screen holds it",pw,ph,scale);
 if(r[WIDGET_SLOT_ENERGY].Top+r[WIDGET_SLOT_ENERGY].Height!=lh)fail("Energy does not reach the bottom",pw,ph,scale);
 cases++;}
static void record(void){
 struct Screen screen={2560,1600};struct ScreenRenderContext c100={100,18},c200={200,36};struct WidgetPlace p;struct WidgetRect r,q;
 WidgetPlaceAutomatic(&p);assert(WidgetPlaceRect(&p,&screen,&c100,WIDGET_SLOT_ENERGY,&r));
 struct Window w={(WORD)r.Left,(WORD)r.Top,(WORD)r.Width,(WORD)r.Height};WidgetPlaceOpened(&p,&w);
 WidgetPlaceChanged(&p,&w,100);if(!p.Automatic){puts("FAIL an unchanged window became the user's");abort();}
 assert(WidgetPlaceRect(&p,&screen,&c200,WIDGET_SLOT_ENERGY,&q)&&q.Left==844&&q.Width==428);
 struct Window moved={500,300,700,900};WidgetPlaceChanged(&p,&moved,100);
 if(p.Automatic){puts("FAIL a user move was not taken as the user's geometry");abort();}
 struct WidgetPlace next;WidgetPlaceCopy(&next,&p);assert(WidgetPlaceRect(&next,&screen,&c200,WIDGET_SLOT_ENERGY,&q));
 if(q.Left!=500||q.Top!=300||q.Width!=700||q.Height!=900){puts("FAIL the user's geometry was replaced by the slot at 200%");abort();}
 /* Intuition fits it at 200 %; the next open is recorded, no user change. */
 struct Window fitted={500,300,1400,1600-300*2};WidgetPlaceOpened(&next,&fitted);WidgetPlaceChanged(&next,&fitted,200);
 struct WidgetPlace back;WidgetPlaceCopy(&back,&next);assert(WidgetPlaceRect(&back,&screen,&c100,WIDGET_SLOT_ENERGY,&q));
 if(q.Left!=500||q.Top!=300||q.Width!=700||q.Height!=900){puts("FAIL the user's geometry did not survive 100 -> 200 -> 100");abort();}
 /* A move at 200 % is in that scale's pixels. */
 struct Window again={1000,100,1400,1000};WidgetPlaceChanged(&back,&again,200);assert(back.Logical.Left==500&&back.Logical.Top==50&&back.Logical.Width==700&&back.Logical.Height==500);
 cases+=4;}
int main(void){
 /* The design arrangement: the M1 panel at 100 % with the 18-row Ice bar. */
 {struct Screen screen={2560,1600};struct ScreenRenderContext c={100,18};struct WidgetRect a,m,e;
  assert(WidgetScreenSlot(&screen,&c,WIDGET_SLOT_ALIVE,&a)&&WidgetScreenSlot(&screen,&c,WIDGET_SLOT_MAGNIFIER,&m)&&WidgetScreenSlot(&screen,&c,WIDGET_SLOT_ENERGY,&e));
  if(a.Left!=1120||a.Top!=20||a.Width!=632||a.Height!=523||m.Left!=1120||m.Top!=551||m.Width!=632||m.Height!=545||e.Left!=1760||e.Top!=20||e.Width!=792||e.Height!=1580)
  {printf("FAIL design arrangement: status %ld,%ld %ldx%ld Magnifier %ld,%ld %ldx%ld Energy %ld,%ld %ldx%ld\n",(long)a.Left,(long)a.Top,(long)a.Width,(long)a.Height,(long)m.Left,(long)m.Top,(long)m.Width,(long)m.Height,(long)e.Left,(long)e.Top,(long)e.Width,(long)e.Height);abort();}
  c=(struct ScreenRenderContext){200,36};assert(WidgetScreenSlot(&screen,&c,WIDGET_SLOT_ALIVE,&a)&&WidgetScreenSlot(&screen,&c,WIDGET_SLOT_ENERGY,&e));
  /* 1280 logical units: the widgets take 768, the desktop and Shell keep 512. */
  if(e.Left!=844||e.Width!=428||a.Left!=496||a.Width!=340){printf("FAIL the M1 at 200%%: status %ld %ld Energy %ld %ld\n",(long)a.Left,(long)a.Width,(long)e.Left,(long)e.Width);abort();}}
 static const int panels[][2]={{2560,1600},{2560,1664},{3024,1964},{3456,2234},{1920,1200},{1440,900},{2880,1800}};
 for(unsigned p=0;p<sizeof panels/sizeof panels[0];p++)for(unsigned scale=100;scale<=400;scale+=25)
  for(ULONG theme=16;theme<=28;theme+=12)arrangement(panels[p][0],panels[p][1],scale,(theme*scale+50)/100);
 record();
 printf("PASS actual widgetplace.h: design arrangement and M1 200%%, %u panel/scale/bar arrangements and placement-record cases\n",cases);return 0;}
'''

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--mutations', action='store_true'); ap.add_argument('--output', type=Path); args = ap.parse_args()
    header = (ROOT / HEADER).read_text()
    mutants = {
        'no-gap': header.replace('r->Left = width - 2 * WIDGET_GAP - energy - column;', 'r->Left = width - WIDGET_GAP - energy - column;'),
        'column-never-shrinks': header.replace('    if (energy + column > room) {', '    if (FALSE) {'),
        'energy-shrinks-first': header.replace('        column = room - energy;\n        if (column < WIDGET_COLUMN_MINIMUM) {', '        {'),
        'heights-never-shrink': header.replace('    if (alive + WIDGET_GAP + magnifier > tall) {', '    if (FALSE) {'),
        'whole-width': header.replace('    if (room > width * WIDGET_SHARE_NUMERATOR / WIDGET_SHARE_DENOMINATOR)\n', '    if (FALSE)\n'),
        'over-the-bar': header.replace('/ scale + 2, r);', '/ scale - 4, r);'),
        'move-ignored': header.replace('    p->Automatic = FALSE;\n    p->Logical.Left = ((LONG)', '    return;\n    p->Logical.Left = ((LONG)'),
        'user-geometry-replaced': header.replace('    if (p->Automatic)\n        return WidgetScreenSlot', '    if (TRUE)\n        return WidgetScreenSlot'),
        'opened-not-recorded': header.replace('    p->Left = w->LeftEdge;\n    p->Top = w->TopEdge;\n    p->Width = w->Width;\n    p->Height = w->Height;\n}', '    (void)p; (void)w;\n}'),
    }
    with tempfile.TemporaryDirectory(prefix='widgetplace-') as tmp:
        out = args.output or Path(tmp); out.mkdir(parents=True, exist_ok=True); records = []
        variants = [('production', header)] + (list(mutants.items()) if args.mutations else [])
        for name, text in variants:
            assert name == 'production' or text != header, name
            c = out / (name + '.c'); exe = out / name; c.write_text(PREFIX + text + MAIN)
            built = subprocess.run(['clang', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                '-fsanitize=address,undefined', '-fno-sanitize-recover=all', str(c), '-o', str(exe)], capture_output=True, text=True)
            if built.returncode: raise RuntimeError(name + ':\n' + built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True); (out / (name + '.log')).write_text(run.stdout + run.stderr)
            if name == 'production':
                if run.returncode: raise RuntimeError(run.stdout + run.stderr)
                print(run.stdout.strip())
            elif not run.returncode: raise RuntimeError('mutant escaped: ' + name)
            else: print('REJECT %s: %s' % (name, (run.stdout.strip().splitlines() or ['abort'])[-1]))
            records.append({'name': name, 'returncode': run.returncode, 'unit_sha256': hashlib.sha256(c.read_bytes()).hexdigest()})
        (out / 'results.json').write_text(json.dumps({'header_sha256': hashlib.sha256(header.encode()).hexdigest(), 'cases': records}, indent=2) + '\n')

if __name__ == '__main__': main()
