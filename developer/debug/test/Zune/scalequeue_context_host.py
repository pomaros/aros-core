#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Production redraw veto and coalesced requalification escape client draw locks."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[4]
s=(root/'workbench/libs/muimaster/classes/window.c').read_text()
def function(start):
    a=s.index(start);b=s.index('{',a);n=1;i=b+1
    while n:n+=(s[i]=='{')-(s[i]=='}');i+=1
    return s[a:i]
helpers='\n'.join(function(name) for name in ['static void CancelContextRetry(','static IPTR Window__MUIM_RequestScaleContext(','static IPTR Window__MUIM_ApplyScaleContext('])
redraw=(root/'workbench/libs/muimaster/mui_redraw.c').read_text()
redraw=redraw[redraw.index('    if (!(_flags(obj) & MADF_CANDRAW))'):redraw.index('    if (_flags(obj) & MADF_INVIRTUALGROUP)')]
head=r'''
#include <stdint.h>
#include <stdarg.h>
#include <assert.h>
#include <stdio.h>
typedef uintptr_t IPTR;typedef uint32_t ULONG;typedef int BOOL;
#define MADF_CANDRAW 1
#define MUIWF_OPENED 2
#define TRUE 1
#define FALSE 0
enum {MUIM_Application_PushMethod=1,MUIM_Application_UnpushMethod,MUIM_Window_RequestScaleContext,MUIM_Window_ApplyScaleContext,MUIM_Window_RetryRenderContext,MUIM_Window_RecalcDisplay};
struct IClass {int unused;};typedef struct Object Object;
struct MUI_RenderInfo {BOOL mri_ScaleAware;Object *mri_WindowObject;};
struct MUI_WindowData {IPTR wd_ContextRetryToken,wd_ContextRecalcToken;unsigned wd_ContextRetryBudget,wd_Flags;void *wd_RootObject;};
struct Object {struct MUI_WindowData data;struct MUI_RenderInfo mri;unsigned flags;};
#define INST_DATA(c,o) ((void)(c),&(o)->data)
#define _app(o) (o)
#define _flags(o) ((o)->flags)
#define muiRenderInfo(o) (&(o)->mri)
#define MUIMasterBase NULL
static BOOL client_layer_locked,fail_queue;static Object *queued;static unsigned pushes,recalcs,draws;
static IPTR Window__MUIM_RequestScaleContext(struct IClass*,Object*);
static IPTR Window__MUIM_ApplyScaleContext(struct IClass*,Object*);
static BOOL zune_object_scale_aware(Object *o,void *base){(void)o;(void)base;return FALSE;}
static IPTR DoMethod(Object *obj,ULONG method,...){va_list ap;va_start(ap,method);IPTR result=0;
 switch(method){
 case MUIM_Window_RequestScaleContext:result=Window__MUIM_RequestScaleContext(NULL,obj);break;
 case MUIM_Application_PushMethod:assert(va_arg(ap,IPTR)==(IPTR)obj);assert(va_arg(ap,int)==1);assert(va_arg(ap,int)==MUIM_Window_ApplyScaleContext);++pushes;if(!fail_queue){assert(!queued);queued=obj;result=(IPTR)obj;}break;
 case MUIM_Application_UnpushMethod:assert(va_arg(ap,IPTR)==(IPTR)obj);assert(va_arg(ap,IPTR)==(IPTR)obj);assert(va_arg(ap,int)==MUIM_Window_ApplyScaleContext);assert(queued==obj);queued=NULL;break;
 case MUIM_Window_RecalcDisplay:assert(!client_layer_locked);assert(va_arg(ap,IPTR)==(IPTR)obj->data.wd_RootObject);++recalcs;break;
 default:assert(0);
 }va_end(ap);return result;
}
'''
main=r'''
static void draw(Object *obj){
'''+redraw+r'''
 ++draws;
}
int main(void){
 Object obj={.data={.wd_Flags=MUIWF_OPENED,.wd_RootObject=(void*)1},.flags=MADF_CANDRAW};obj.mri.mri_ScaleAware=TRUE;obj.mri.mri_WindowObject=&obj;
 client_layer_locked=TRUE;draw(&obj);draw(&obj);assert(pushes==1&&!draws&&!recalcs&&queued==&obj);client_layer_locked=FALSE;queued=NULL;Window__MUIM_ApplyScaleContext(NULL,&obj);assert(recalcs==1&&!obj.data.wd_ContextRecalcToken);
 client_layer_locked=TRUE;draw(&obj);CancelContextRetry(&obj,&obj.data);assert(!queued&&!obj.data.wd_ContextRecalcToken);client_layer_locked=FALSE;
 fail_queue=TRUE;client_layer_locked=TRUE;draw(&obj);assert(!obj.data.wd_ContextRecalcToken&&!draws&&recalcs==1);fail_queue=FALSE;draw(&obj);assert(queued==&obj);client_layer_locked=FALSE;obj.data.wd_Flags=0;queued=NULL;Window__MUIM_ApplyScaleContext(NULL,&obj);assert(recalcs==1&&!obj.data.wd_ContextRecalcToken);
 obj.flags=0;draw(&obj);assert(!draws);puts("PASS actual redraw/queue: no synchronous reopen under draw lock, one coalesced callback, cancellation/disposal, queue fault never draws foreign pixels, closed-window callback harmless");
}
'''
text=head+helpers+'\n'+main
mutants=[('sync-from-draw','DoMethod(mri->mri_WindowObject, MUIM_Window_RequestScaleContext);','DoMethod(mri->mri_WindowObject, MUIM_Window_RecalcDisplay, (IPTR)obj->data.wd_RootObject);'),('uncoalesced','!data->wd_ContextRecalcToken && _app(obj)','_app(obj)'),('closed-callback','if (data->wd_Flags & MUIWF_OPENED)','if (1)'),('cancel-leak','if (recalc && _app(obj))','if (0 && recalc && _app(obj))')]
with tempfile.TemporaryDirectory(prefix='scalequeue-') as d:
    d=Path(d)
    for name,program in [('production',text)]+[(n,text.replace(a,b)) for n,a,b in mutants]:
        c=d/(name+'.c');exe=d/name;c.write_text(program)
        subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-o',str(exe)],check=True)
        p=subprocess.run([str(exe)],capture_output=True,text=True)
        if name=='production':assert p.returncode==0,p.stdout+p.stderr;print(p.stdout,end='')
        else:assert p.returncode!=0,name+' survived';print('REJECT mutant '+name)
