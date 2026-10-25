#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""When the native widgets open at boot (widgetstart.h), against the kernel's
desktop acceptance.

The kernel launcher accepts the desktop only while Wanderer's backdrop is the
newest window of the Workbench screen (coldstart_proof.c run_wanderer, which
compares the backdrop with screen->FirstWindow); only then does it open the
Shell and the status widget. A widget window opened between the backdrop and
that acceptance is the newest window, the launcher returns to the loader and
the boot freezes behind the painted desktop (generation 54 on the M1).

The actual WidgetWaitForDesktop runs against a timeline of that boot: the
screen and backdrop appear, the kernel samples the newest window once, the
status widget follows its acceptance. Swept over start, screen and acceptance
times, the widget must never be the newest window at the kernel's sample, must
open within one poll of the status widget, must lock the screen once and
release LockIBase before it sleeps; without a status widget it still opens
after its timeout. Source checks pin the premises: the kernel status widget's
title, the launcher order (acceptance, Shell, status widget) and both widgets
using the wait. The timeline is a model of the kernel's check, not the check
itself. Mutants of the wait and of its callers are rejected.
"""
from pathlib import Path
import argparse, hashlib, json, re, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[4]
HEADER = 'arch/aarch64-apple/include/widgetstart.h'

PREFIX = r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef int32_t LONG;typedef uint32_t ULONG;typedef unsigned char UBYTE;typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define SIGBREAKF_CTRL_C (1UL<<12)
struct Window{struct Window*NextWindow;UBYTE*Title;};
struct Screen{struct Window*FirstWindow;};
static double now,screen_at,kernel_at,alive_at;
static int ibase_locked,visitors,backdrop_open,alive_open,kernel_state;/* 0 pending, 1 accepted, 2 gave up */
static ULONG signals;
static struct Screen wb;
static struct Window backdrop,alive_window={NULL,(UBYTE*)"AROS alive - arrows/wheel scroll"},widget_window={NULL,(UBYTE*)"Energy"};
static void push(struct Window*w){w->NextWindow=wb.FirstWindow;wb.FirstWindow=w;}
static void advance(double to){
 if(!backdrop_open&&to>=screen_at){push(&backdrop);backdrop_open=1;}
 if(backdrop_open&&kernel_state==0&&to>=kernel_at)kernel_state=wb.FirstWindow==&backdrop?1:2;
 if(kernel_state==1&&!alive_open&&to>=alive_at){push(&alive_window);alive_open=1;}}
static struct Screen*LockPubScreen(const char*name){(void)name;assert(!ibase_locked);advance(now);if(now<screen_at)return NULL;visitors++;return &wb;}
static ULONG LockIBase(ULONG x){(void)x;if(ibase_locked){puts("FAIL LockIBase nested");abort();}ibase_locked=1;return 0x1b;}
static void UnlockIBase(ULONG l){assert(l==0x1b&&ibase_locked);ibase_locked=0;}
static ULONG SetSignal(ULONG a,ULONG b){(void)a;(void)b;return signals;}
static void Delay(LONG ticks){if(ibase_locked){puts("FAIL sleeps with LockIBase held");abort();}now+=ticks/50.0;advance(now);}
'''
MAIN = r'''
static unsigned cases;
static struct Screen*run(double start,double screen,double accept,int status,ULONG sig){
 now=start;screen_at=screen;kernel_at=screen+accept;alive_at=status?kernel_at+0.25:INFINITY;
 ibase_locked=visitors=backdrop_open=alive_open=kernel_state=0;signals=sig;wb.FirstWindow=NULL;
 backdrop.NextWindow=alive_window.NextWindow=widget_window.NextWindow=NULL;
 struct Screen*s=WidgetWaitForDesktop(60);
 advance(now);if(s)push(&widget_window);advance(1e9);cases++;return s;}
static void fail(const char*what,double start,double screen,double accept){
 printf("FAIL %s (start %.2f s, screen %.2f s, acceptance +%.2f s)\n",what,start,screen,accept);abort();}
int main(void){
 /* The freeze first: no timeline may leave a widget the newest window at
  * the kernel's sample. */
 for(double start=0.05;start<3.0;start+=0.45)
  for(double screen=0.11;screen<6.0;screen+=0.37)
   for(double accept=0.013;accept<3.0;accept+=0.131){
    run(start,screen,accept,1,0);
    if(kernel_state!=1)fail("a widget window was the newest at the kernel's sample: the launcher gives up and the boot freezes",start,screen,accept);}
 for(double start=0.05;start<3.0;start+=0.45)
  for(double screen=0.11;screen<6.0;screen+=0.37)
   for(double accept=0.013;accept<3.0;accept+=0.131){
    struct Screen*s=run(start,screen,accept,1,0);
    if(s!=&wb)fail("no screen",start,screen,accept);
    if(visitors!=1)fail("the screen was locked more than once",start,screen,accept);
    if(ibase_locked)fail("LockIBase still held",start,screen,accept);
    if(now<alive_at||now>(start>alive_at?start:alive_at)+1.01)fail("the widget did not open within one poll of the status widget",start,screen,accept);}
 /* No status widget (a bounded tree): the widget still opens at its timeout. */
 {struct Screen*s=run(0.05,1.0,0.5,0,0);
  if(s!=&wb||visitors!=1||now<59.0||now>61.0){printf("FAIL no status widget: screen %p visitors %d at %.2f s\n",(void*)s,visitors,now);abort();}}
 /* CTRL-C ends the wait at once. */
 {struct Screen*s=run(0.05,0.01,0.5,0,SIGBREAKF_CTRL_C);if(s!=&wb||now>0.06){puts("FAIL CTRL-C did not end the wait");abort();}}
 printf("PASS actual widgetstart.h: %u boot timelines, never the newest window at the kernel's sample, one poll after the status widget, timeout and CTRL-C\n",cases);return 0;}
'''


def source_checks(root, energy, magnifier, kernel):
    m = re.search(r'\{ WA_Title, \(IPTR\)"([^"]*)" \}', kernel[kernel.index('static struct AliveView *prepare_alive_view'):])
    if not m or not m.group(1).startswith('AROS alive'):
        return 'the kernel status widget title does not start with WIDGET_STATUS_TITLE'
    launcher = kernel[kernel.index('static void wanderer_launcher_entry'):]
    if not re.search(r'run_wanderer\(proof, sys_lock\)\)\s*\{\s*proof->detail = 0;\s*proof->status = 1;\s*open_shell_and_hold\(proof\);', launcher):
        return 'the launcher no longer opens the hold only after accepting the desktop'
    hold = kernel[kernel.index('static void open_shell_and_hold'):]
    if not (0 <= hold.index('Open("CON:') < hold.index('alive = open_alive_window();')):
        return 'the status widget no longer follows the Shell'
    for name, text in (('Energy', energy), ('Magnifier', magnifier)):
        if '#include "../include/widgetstart.h"' not in text or text.count('v.screen = WidgetWaitForDesktop(WAIT_SECONDS);') != 1:
            return name + ' does not wait with WidgetWaitForDesktop'
        if 'LockPubScreen(' in text:
            return name + ' locks the Workbench screen itself'
    return None


GEN54_WAIT = '''static struct Screen *wait_for_desktop(void)
{
    struct Screen *screen = NULL;
    int second;

    for (second = 0; second < WAIT_SECONDS; ++second) {
        screen = LockPubScreen("Workbench");
        if (screen)
            return screen;
        if (SetSignal(0, 0) & SIGBREAKF_CTRL_C)
            break;
        Delay(50);
    }
    return screen;
}
'''


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--source', type=Path, default=ROOT); ap.add_argument('--mutations', action='store_true'); ap.add_argument('--output', type=Path); args = ap.parse_args()
    root = args.source
    header = (root / HEADER).read_text()
    energy = (root / 'arch/aarch64-apple/c/Energy.c').read_text()
    magnifier = (root / 'arch/aarch64-apple/c/Magnifier.c').read_text()
    kernel = (root / 'arch/aarch64-apple/kernel/coldstart_proof.c').read_text()
    call = 'v.screen = WidgetWaitForDesktop(WAIT_SECONDS);'
    mutants = {
        'open-at-screen': (header.replace('BOOL ready = WidgetStatusOpen(screen->FirstWindow);', 'BOOL ready = TRUE;'), energy, magnifier),
        'title-differs': (header.replace('#define WIDGET_STATUS_TITLE "AROS alive"', '#define WIDGET_STATUS_TITLE "AROS Alive"'), energy, magnifier),
        'exact-title': (header.replace('        if (!*want)\n', '        if (!*want && !*title)\n'), energy, magnifier),
        'ibase-kept': (header.replace('            UnlockIBase(lock);\n', '            (void)lock;\n'), energy, magnifier),
        'relock-each-poll': (header.replace('        if (!screen)\n            screen = LockPubScreen', '        screen = LockPubScreen'), energy, magnifier),
        'energy-gen54-wait': (header, energy.replace(call, 'v.screen = wait_for_desktop();').replace('/* ---- main', GEN54_WAIT + '\n/* ---- main', 1), magnifier),
        'magnifier-gen54-wait': (header, energy, magnifier.replace(call, 'v.screen = wait_for_desktop();').replace('struct view {', GEN54_WAIT + '\nstruct view {', 1)),
        'alive-retitled': (header, energy, magnifier, kernel.replace('"AROS alive - arrows/wheel scroll"', '"Status - arrows/wheel scroll"')),
    }
    with tempfile.TemporaryDirectory(prefix='widgetstart-') as tmp:
        out = args.output or Path(tmp); out.mkdir(parents=True, exist_ok=True); records = []
        variants = [('production', (header, energy, magnifier, kernel))] + (list(mutants.items()) if args.mutations else [])
        for name, texts in variants:
            h, e, mg = texts[:3]; k = texts[3] if len(texts) > 3 else kernel
            assert name == 'production' or (h, e, mg, k) != (header, energy, magnifier, kernel), name
            problem = source_checks(root, e, mg, k)
            c = out / (name + '.c'); exe = out / name; c.write_text(PREFIX + h + MAIN)
            if problem:
                rc, text = 1, 'FAIL ' + problem
            else:
                built = subprocess.run(['clang', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all', str(c), '-o', str(exe)], capture_output=True, text=True)
                if built.returncode: raise RuntimeError(name + ':\n' + built.stderr)
                run = subprocess.run([str(exe)], capture_output=True, text=True); rc, text = run.returncode, run.stdout + run.stderr
            (out / (name + '.log')).write_text(text)
            if name == 'production':
                if rc: raise RuntimeError(text)
                print(text.strip())
            elif not rc: raise RuntimeError('mutant escaped: ' + name)
            else: print('REJECT %s: %s' % (name, (text.strip().splitlines() or ['abort'])[-1]))
            records.append({'name': name, 'returncode': rc, 'unit_sha256': hashlib.sha256(c.read_bytes()).hexdigest()})
        (out / 'results.json').write_text(json.dumps({'header_sha256': hashlib.sha256(header.encode()).hexdigest(),
            'energy_sha256': hashlib.sha256(energy.encode()).hexdigest(), 'magnifier_sha256': hashlib.sha256(magnifier.encode()).hexdigest(),
            'kernel_sha256': hashlib.sha256(kernel.encode()).hexdigest(), 'cases': records}, indent=2) + '\n')

if __name__ == '__main__': main()
