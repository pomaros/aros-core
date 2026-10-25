#!/usr/bin/env python3
"""Production Application base selection, including the probe's old mismatch."""
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
core = here.parents[3]
source = (core / 'workbench/libs/muimaster/classes/application.c').read_text()
start = source.index('    data->app_SingleTask =\n')
body = source[start:source.index('    if (!data->app_Base)\n', start)]
probe = (here / 'bootundoprobe.c').read_text()
assert 'MUIA_Application_Base,(IPTR)"SET5OBSERVER",\n        MUIA_Application_SingleTask,TRUE,' in probe
fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uintptr_t IPTR;
typedef char *STRPTR;
typedef int BOOL;
typedef long LONG;
typedef struct Object { BOOL app_SingleTask; STRPTR app_Base; } Object;
struct Tags { int single; const char *base; };
struct Msg { struct Tags *ops_AttrList; };
#define TRUE 1
#define FALSE 0
#define MUIA_Application_SingleTask 1
#define MUIA_Application_Base 2
#define OM_DISPOSE 3
#define MUIM_Application_PushMethod 4
#define MUIM_Set 5
#define MUIA_Application_DoubleStart 6
static int pushed,disposed,locked;
static const char *collision;
static struct { int ZuneSemaphore; } master;
#define MUIMB(x) (&master)
static void *MUIMasterBase;
static IPTR GetTagData(int key,IPTR fallback,struct Tags *tags) {
    (void)fallback;return key==1?(IPTR)tags->single:(IPTR)tags->base;
}
static STRPTR StrDup(const char *s) { return s?strdup(s):NULL; }
static Object other;
static Object *find_application_by_base(void *cl,Object *obj,const char *s) {
    (void)cl;(void)obj;return collision&&!strcmp(collision,s)?&other:NULL;
}
static void CoerceMethod(void *cl,Object *o,int method) {
    (void)cl;(void)o;(void)method;disposed++;
}
static void DoMethod(Object *o,int method,...) {(void)o;(void)method;pushed++;}
static void ObtainSemaphore(int *s) {(void)s;locked++;}
static void ReleaseSemaphore(int *s) {(void)s;locked--;}
static int setup(Object *data,struct Msg *msg) {
    void *cl=NULL;Object *obj=data;
    BODY
    return data->app_Base!=NULL;
}
int main(int argc,char **argv) {
    (void)argv;
    Object data={0};struct Tags tags={argc==1,"SET5OBSERVER"};struct Msg msg={&tags};
    assert(setup(&data,&msg));
    if(strcmp(data.app_Base,"SET5OBSERVER")) {free(data.app_Base);return 20;}
    free(data.app_Base);
    tags.single=0;assert(setup(&data,&msg));assert(!strcmp(data.app_Base,"SET5OBSERVER.1"));free(data.app_Base);
    collision="SET5OBSERVER.1";assert(setup(&data,&msg));assert(!strcmp(data.app_Base,"SET5OBSERVER.2"));free(data.app_Base);
    tags.single=1;collision="SET5OBSERVER";assert(!setup(&data,&msg));assert(pushed==1&&locked==0);
    collision=NULL;tags.base="unsafe/path";assert(!setup(&data,&msg));assert(disposed==1);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='application-base-') as tmp:
    tmp = Path(tmp)
    c, exe = tmp/'fixture.c', tmp/'fixture'
    c.write_text(fixture.replace('BODY', body))
    subprocess.run(['clang','-Wall','-Wextra','-Wno-unused-variable',
                    '-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
    assert subprocess.run([str(exe),'legacy-default']).returncode == 20
    print('production Application base PASS: fixed SingleTask identity, default suffix, collision increment, duplicate refusal, invalid namespace')
    print('old probe default-suffix/fixed-path mismatch REJECTED')
