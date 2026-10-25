#!/usr/bin/env python3
"""Actual probe selector: reject Group OM_GET forwarding proxies."""
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
source = (here / "bootundoprobe.c").read_text()
start = source.index("static Object *editor_in(")
selector = source[start:source.index("/* Bounded diagnostic", start)]
fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef uintptr_t IPTR;
typedef void *APTR;
typedef struct Class { int id; } Class;
typedef struct Object Object;
struct List { Object *lh_Head; };
struct Object { Class *cl; const char *path; struct List children; Object *next; };
#define OCLASS(o) ((o)->cl)
#define MUIA_PrefsEditor_Path 1
#define MUIA_Group_ChildList 2
/* Group__OM_GET, group.c714: unsupported path forwarded to child. */
static int GetAttr(int attr,Object *o,IPTR *storage) {
    if(attr==MUIA_Group_ChildList) { *storage=(IPTR)&o->children; return 1; }
    if(o->path) { *storage=(IPTR)o->path; return 1; }
    for(Object *c=o->children.lh_Head;c;c=c->next)
        if(GetAttr(attr,c,storage)) return 1;
    return 0;
}
static Object *NextObject(APTR *state) {
    Object *o=*state; if(o)*state=o->next; return o;
}
SELECTOR
int main(void) {
    Class group={0}, input={1}, asl={2}, zune={3}, absent={4};
    Object editors[3]={{&input,"SYS/input.prefs",{0},0},
        {&asl,"SYS/Asl.prefs",{0},0},{&zune,"Zune/global.prefs",{0},0}};
    Object wrapper={&group,0,{0},0}, root={&group,0,{&wrapper},0};
    const char *paths[]={"SYS/input.prefs","SYS/Asl.prefs","Zune/global.prefs"};
    Object *captured[3];
    for(int i=0;i<3;i++) {
        wrapper.children.lh_Head=&editors[i];
        IPTR proxy=0; assert(GetAttr(MUIA_PrefsEditor_Path,&root,&proxy));
        assert(strcmp((char*)proxy,paths[i])==0);
        captured[i]=editor_in(&root,editors[i].cl,paths[i]);
        if(captured[i]!=&editors[i]) return 20+i;
        assert(editor_in(&root,&absent,paths[i])==0);
        assert(editor_in(&root,editors[i].cl,"wrong/path")==0);
    }
    assert(captured[0]!=captured[1]&&captured[1]!=captured[2]);
    assert(strcmp(captured[0]->path,paths[0])==0);
    wrapper.children.lh_Head=0;
    assert(editor_in(&root,&input,paths[0])==0);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="bootundo-editor-") as tmp:
    tmp = Path(tmp)
    for legacy in (False, True):
        body = selector
        if legacy:
            body = body.replace(
                'OCLASS(o) == expected && GetAttr(MUIA_PrefsEditor_Path, o, &path) && path &&\n'
                '        strcmp((const char *)path, expected_path) == 0',
                'GetAttr(MUIA_PrefsEditor_Path, o, &path) && path')
            assert body != selector
        c = tmp / ('legacy.c' if legacy else 'actual.c')
        exe = c.with_suffix('')
        c.write_text(fixture.replace('SELECTOR', body))
        subprocess.run(['clang','-Wall','-Wextra','-Wno-unused-parameter',
                        '-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
        result = subprocess.run([str(exe)])
        assert (result.returncode != 0) if legacy else (result.returncode == 0)
        print('legacy forwarding-proxy selector REJECTED' if legacy else
              'actual class/path selector PASS: 3 distinct cached editors, wrong class/path and absent editor')
