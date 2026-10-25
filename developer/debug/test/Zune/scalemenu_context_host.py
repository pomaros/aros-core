#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Use the actual popup title measurement and metrics in both contexts."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[4]
source=(root/'workbench/libs/muimaster/menu.c').read_text()
helpers=source[source.index('static WORD MenuMetric'):source.index('static WORD MenuBarHeight')]
constants='\n'.join(line for line in source.splitlines() if line.startswith('#define ') and any(name in line for name in ['ITEXT_EXTRA','TEXT_AMIGAKEY_SPACING']))
measure=source[source.index('    struct RastPort measure;'):]
measure=measure[:measure.index('#endif')]
support=(root/'workbench/libs/muimaster/support.c').read_text()
metric=support[support.index('LONG zune_scale_value'):support.index('LONG zune_scale(Object')]
head=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef long LONG;typedef unsigned long ULONG;typedef intptr_t IPTR;typedef short WORD;
#define MUI_MAXMAX 10000
struct TextFont {WORD tf_YSize;};struct RastPort {struct TextFont *Font;};
struct Screen {struct RastPort RastPort;WORD Width,MenuHBorder,MenuVBorder;};
struct DrawInfo {struct TextFont *dri_Font;};
struct Menu {struct Menu *NextMenu;char *MenuName;};
struct MenuHandlerData {struct Screen *scr;struct DrawInfo *dri;struct Menu *menu;
 ULONG factor,renderScale;WORD scrmousex,scrmousey,nummenubaritems,menubaritemwidth,menubaritemheight;};
struct TagItem {ULONG ti_Tag;IPTR ti_Data;};
static const char subitemindicator[]="x";
static void InitRastPort(struct RastPort *rp){memset(rp,0,sizeof(*rp));}
static void SetFont(struct RastPort *rp,struct TextFont *font){rp->Font=font;}
static WORD TextLength(struct RastPort *rp,const char *text,size_t n){(void)text;return rp->Font->tf_YSize*n;}
'''
tests=r'''
int main(void){
 struct TextFont logical={13},physical={26};struct Screen screen={{&physical},2560,2,1};
 struct DrawInfo dri={&logical};struct Menu b={NULL,"ab"},a={&b,"abc"};
 struct MenuHandlerData mhd={.scr=&screen,.dri=&dri,.menu=&a,.factor=2,.renderScale=100,.scrmousex=640,.scrmousey=128};
 struct TagItem before[4]={{0}},after[4]={{0}};
 measure_menu(&mhd,before);assert(mhd.menubaritemwidth==62&&mhd.menubaritemheight==15);
 assert(before[2].ti_Data==66&&before[3].ti_Data==32&&before[1].ti_Data==64);
 assert(MenuWidth(&mhd)==1280);
 mhd.factor=1;mhd.renderScale=200;dri.dri_Font=&physical;
 measure_menu(&mhd,after);assert(mhd.menubaritemwidth==124&&mhd.menubaritemheight==30);
 assert(after[2].ti_Data==before[2].ti_Data*2&&after[3].ti_Data==before[3].ti_Data*2);
 assert(after[1].ti_Data==128&&MenuWidth(&mhd)==2560);
 puts("scalemenu context ASan/UBSan PASS: actual popup font logical13 despite physicalscreen26, qualified26; padding/dimensions exactly2x; pointer and screen extents coherent");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='aros-scalemenu-') as directory:
    path=Path(directory)/'test.c';exe=Path(directory)/'test'
    path.write_text(head+metric+constants+'\n'+helpers+'\nstatic void measure_menu(struct MenuHandlerData *mhd,struct TagItem *win_tags){struct Menu *menu;WORD w,maxw=0;\n'+measure+'\n}\n'+tests)
    subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-g','-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(path),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
