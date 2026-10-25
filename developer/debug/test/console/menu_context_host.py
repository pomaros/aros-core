#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Actual CON detached preparation and explicit Intuition menu installation."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[4]
def block(text,marker):
    a=text.index(marker);b=text.index('{',a);end=b+1;depth=1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[a:end]
con=(root/'rom/filesys/console_handler/con_handler.c').read_text(errors='surrogateescape')
api=(root/'rom/intuition/setmenustripcontext.c').read_text()
classic=(root/'rom/intuition/setmenustrip.c').read_text()
actual=block(api,'static BOOL MenuStripContextDimensions(')+'\n'+block(classic,'void Characterize(struct Menu *menu)\n{')+'\n'
body=api.split('    AROS_LIBFUNC_INIT',1)[1].split('    AROS_LIBFUNC_EXIT',1)[0]
actual+='static BOOL SetMenuStripContext(struct Window *window,struct Menu *menu,const struct ScreenRenderContext *context,BOOL logical){'+body+'}\n'
actual+=block(con,'static BOOL con_install_menu(')
prefix=r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
typedef int BOOL;typedef int32_t LONG;typedef uint32_t ULONG;typedef uint16_t UWORD;typedef uintptr_t IPTR;typedef void *APTR;typedef char *STRPTR;typedef void Object;
#define TRUE 1
#define FALSE 0
#define TAG_DONE 0
#define GTVI_RenderContext 1
#define GTVI_Logical 2
#define GTMN_NewLookMenus 3
#define SA_RenderContext 164
#define NM_TITLE 1
#define NM_ITEM 2
#define NM_SUB 3
#define NM_END 0
#define NM_BARLABEL ((STRPTR)-1)
#define NM_ITEMDISABLED 1
#define WFLG_CLOSEGADGET 1
#define HASSUBITEM 0x8000
#define SANITY_CHECKR(w,r) if(!(w))return(r);
enum {MEN_CONSOLE_ABOUT,MEN_CONSOLE_CLOSE,MEN_CONSOLE_CLIP0,MEN_CONSOLE_CLIP1,MEN_CONSOLE_CLIP2,MEN_CONSOLE_CLIP3,MEN_CONSOLE_COPY,MEN_CONSOLE_PASTE};
struct TagItem {ULONG ti_Tag;IPTR ti_Data;};
struct MenuItem {struct MenuItem *NextItem,*SubItem;int16_t LeftEdge,TopEdge,Width,Height;UWORD Flags;};
struct Menu {struct Menu *NextMenu;struct MenuItem *FirstItem;int16_t JazzX,JazzY,BeatX,BeatY;};
struct Screen;
struct DrawInfo {struct Screen *dri_Screen;};
struct ScreenRenderContext {UWORD src_Scale,src_LegacyScaleFactor,src_LogicalMenuHBorder,src_LogicalMenuVBorder,src_PhysicalMenuHBorder,src_PhysicalMenuVBorder;struct DrawInfo *src_LogicalDrawInfo,*src_PhysicalDrawInfo;};
struct Screen {const struct ScreenRenderContext *current;};
struct Window {struct Screen *WScreen;struct Menu *MenuStrip;const struct ScreenRenderContext *MenuRenderContext,*ExpectedRenderContext;BOOL MenuRenderLogical;};
struct filehandle {struct Window *window,*otherwindow;struct Menu *winmenu;struct{ULONG Flags;}nw;};
struct NewMenu {int type;STRPTR label;STRPTR command;UWORD flags;LONG mutual;APTR user;};
struct IntuitionBase {int MenuLock;};static struct IntuitionBase base;static struct IntuitionBase *IntuitionBase=&base;
#define IW(w) (w)
#define GetPrivIBase(b) (b)
static int locked,queries,allocations,live,failpoint,ensure_failure,clear_calls,freed;
static BOOL wantedlogical;static const struct ScreenRenderContext *sampled,*raced;
static struct Window *subject;
struct VI {const struct ScreenRenderContext *context;BOOL logical;};
static BOOL GetAttr(ULONG attr,Object *object,IPTR *value){assert(attr==SA_RenderContext&&!locked);queries++;*value=(IPTR)((struct Screen*)object)->current;return TRUE;}
static void *allocate(size_t size){if(++allocations==failpoint)return NULL;void *p=calloc(1,size);if(p)live++;return p;}
static void release(void *p){if(p){live--;free(p);}}
static APTR GetVisualInfoA(struct Screen *screen,struct TagItem *tags){assert(tags[0].ti_Tag==GTVI_RenderContext&&tags[1].ti_Tag==GTVI_Logical);struct VI *vi=allocate(sizeof *vi);if(!vi)return NULL;vi->context=(void*)tags[0].ti_Data;vi->logical=tags[1].ti_Data;assert(vi->context==sampled&&vi->logical==wantedlogical);assert(vi->context->src_LogicalDrawInfo->dri_Screen==screen);screen->current=raced;return vi;}
static void FreeVisualInfo(APTR vi){release(vi);}
static struct Menu *CreateMenusA(struct NewMenu *template,struct TagItem *tags){assert(template[0].type==NM_TITLE&&tags[0].ti_Tag==GTMN_NewLookMenus);struct Menu *menu=allocate(sizeof *menu);if(!menu)return NULL;menu->FirstItem=allocate(sizeof *menu->FirstItem);if(!menu->FirstItem){release(menu);return NULL;}menu->FirstItem->Width=80;menu->FirstItem->Height=16;return menu;}
static void FreeMenus(struct Menu *menu){if(menu){freed++;assert(subject->MenuStrip!=menu);release(menu->FirstItem);release(menu);}}
static BOOL LayoutMenusA(struct Menu *menu,APTR info,struct TagItem *tags){(void)tags;struct VI *vi=info;assert(vi->context==sampled&&subject->WScreen->current==raced);if(failpoint==-1)return FALSE;menu->FirstItem->Width=vi->logical?80:160;return TRUE;}
static BOOL EnsureDefaultMenuHandler(struct IntuitionBase *b){assert(b==IntuitionBase&&!locked);return !ensure_failure;}
static void ObtainSemaphore(int *lock){assert(lock==&base.MenuLock&&!locked);locked=1;}
static void ReleaseSemaphore(int *lock){assert(lock==&base.MenuLock&&locked);locked=0;}
'''
suffix=r'''
static void run_case(struct Window *window,const struct ScreenRenderContext *source,const struct ScreenRenderContext *destination,BOOL borrowed,int fail){
    BOOL logical=TRUE;
    queries=0;
    struct filehandle fh={.window=window,.nw.Flags=WFLG_CLOSEGADGET};
    failpoint=0;
    struct Menu *old=allocate(sizeof *old);old->FirstItem=allocate(sizeof *old->FirstItem);
    old->FirstItem->Width=80;old->FirstItem->Height=16;
    window->MenuStrip=old;window->MenuRenderContext=source;window->MenuRenderLogical=logical;
    struct ScreenRenderContext const *expected108=window->ExpectedRenderContext;
    /* Application replacement remains installed without reading/replacing it. */
    struct Menu foreign={0};fh.winmenu=&foreign;
    assert(con_install_menu(&fh));assert(window->MenuStrip==old&&!queries);
    fh.winmenu=NULL;
    assert(con_install_menu(&fh));assert(window->MenuStrip==old&&!queries);
    fh.winmenu=old; /* Replace only an actually handler-owned strip. */
    if(borrowed)fh.otherwindow=window;
    sampled=source;raced=destination;wantedlogical=logical;window->WScreen->current=source;
    allocations=queries=freed=0;failpoint=fail;ensure_failure=fail==-2;
    BOOL ok=con_install_menu(&fh);
    assert(queries==1&&window->ExpectedRenderContext==expected108&&!locked&&!clear_calls);
    if(fail){assert(!ok&&window->MenuStrip==old&&window->MenuRenderContext==source&&fh.winmenu==old&&live==2);window->MenuStrip=NULL;FreeMenus(old);assert(!live);}
    else{
        assert(freed==1);
        assert(ok&&window->MenuStrip==fh.winmenu&&window->MenuRenderContext==source&&window->MenuRenderLogical==logical);
        assert(window->MenuStrip->JazzX==-(logical?source->src_LogicalMenuHBorder:source->src_PhysicalMenuHBorder));
        window->MenuStrip=NULL;FreeMenus(fh.winmenu);assert(!live);
    }
}
int main(void){
    struct Screen screen={0},second={0};struct DrawInfo di={&screen},foreign={&second};
    struct ScreenRenderContext a={200,2,2,1,4,2,&di,&di},b={300,3,2,1,6,3,&di,&di},bad=a;
    struct Window window={.WScreen=&screen,.ExpectedRenderContext=&a};subject=&window;
    struct Window untouched={.WScreen=&second,.MenuRenderContext=&a};
    for(int borrowed=0;borrowed<2;borrowed++)for(int failure=-2;failure<=3;failure++)run_case(&window,&a,&b,borrowed,failure);
    assert(untouched.MenuRenderContext==&a);
    /* Initial handler-owned creation has neither an old strip nor fh owner. */
    struct filehandle initial={.window=&window};window.MenuStrip=NULL;
    allocations=queries=freed=0;failpoint=ensure_failure=0;
    sampled=&a;raced=&b;wantedlogical=TRUE;screen.current=&a;
    assert(con_install_menu(&initial)&&queries==1&&freed==0);
    assert(window.MenuStrip==initial.winmenu&&window.MenuRenderContext==&a);
    window.MenuStrip=NULL;FreeMenus(initial.winmenu);assert(!live);
    struct Menu marker={0};window.MenuStrip=&marker;bad.src_PhysicalDrawInfo=&foreign;
    assert(!SetMenuStripContext(&window,NULL,&bad,FALSE)&&window.MenuStrip==&marker);
    puts("console-menu-context-host PASS exact-layout-install-generation=1 source-A/current-B=1 independent-logical-domain=1 stable-window-and-Expected108=1 detached-failure-ownership=1 other-screen=1 foreign-menu=1 owner-rejection=1");
}
'''
with tempfile.TemporaryDirectory(prefix='console-menu-context-') as temporary:
    path=Path(temporary);unit=path/'fixture.c';binary=path/'fixture'
    variants=[('production',actual,True),
        ('source-resample',actual.replace('IW(window)->MenuRenderContext = context;', 'IW(window)->MenuRenderContext = window->WScreen->current;'),False),
        ('window-domain',actual.replace('IW(window)->MenuRenderLogical = !!logical;', 'IW(window)->MenuRenderLogical = FALSE;'),False),
        ('live-borders',actual.replace('context->src_LogicalMenuHBorder :', 'raced->src_PhysicalMenuHBorder :'),False)]
    for name,body,success in variants:
        unit.write_text(prefix+body+suffix)
        subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-missing-field-initializers','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(unit),'-o',str(binary)],check=True)
        r=subprocess.run([str(binary)],capture_output=True,text=True)
        if success:assert r.returncode==0,r.stderr;print(r.stdout,end='')
        else:assert r.returncode!=0,name+' escaped'
    print('console-menu-context-host mutants=3 rejected=3')
