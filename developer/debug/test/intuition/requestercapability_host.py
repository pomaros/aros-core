#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Actual decorator getters, requester constructor and caller admission branches.
OS allocation/window/raster services use the requesterscale_host adapters.
Caller extraction ends at entry to the established logical constructor; its
rendering and native scheduling are outside this host test.
"""
from pathlib import Path
import re, subprocess, tempfile
root=Path(__file__).resolve().parents[4]
fixture=root/'developer/debug/test/intuition/requesterscale_host.py'
ns={'__file__':str(fixture),'__name__':'requester_capability_dependencies'}
exec(fixture.read_text().split('variants=[')[0],ns)
header=(root/'compiler/include/intuition/windecorclass.h').read_text()
assert re.search(r'#define\s+WDA_PreparedScaleWindow\s+\(WDA_Dummy\s*\+\s*5\)',header)
actual=ns['actual_function']
builtin=actual((root/'rom/intuition/windecorclass.c').read_text(),'WinDecorClass__OM_GET')
theme=actual((root/'workbench/c/Decoration/windowdecorclass.c').read_text(),'windecor_get')
frame=actual((root/'rom/intuition/windowframe.c').read_text(),'PrepareInitialWindowFrameLayout')
prefix=ns['prefix']
old=re.search(r'static BOOL GetAttr\([^\n]+',prefix).group()
prefix=prefix.replace(old,'static BOOL GetAttr(ULONG,Object,IPTR*);')
providers=r'''
typedef struct Class{IPTR cl_UserData;}Class;
typedef void*Msg;
struct windecor_data{ULONG userbuffersize;};
struct Provider{Class*actual;struct windecor_data data;};
struct opGet{ULONG opg_AttrID;IPTR*opg_Storage;};
#define WDA_UserBuffer 4
#define WDA_TrueColorOnly 3
#define INST_DATA(cl,obj) (&((struct Provider*)(obj))->data)
#define OCLASS(obj) (((struct Provider*)(obj))->actual)
static Class builtinClass,themeClass,customClass;
static struct Provider provider;
static unsigned unknown_queries,queries,legacycalls;
static IPTR DoSuperMethodA(Class*cl,Object*obj,Msg msg){(void)cl;(void)obj;(void)msg;unknown_queries++;return FALSE;}
'''
getattr=r'''
static BOOL GetAttr(ULONG a,Object o,IPTR*v){
 if(a==WDA_PreparedScaleWindow){assert(o==&provider);queries++;struct opGet message={a,v};
   if(provider.actual==&themeClass)return windecor_get(&themeClass,(Object*)&provider,&message);
   if(provider.actual==&builtinClass||provider.actual==&customClass)return WinDecorClass__OM_GET(&builtinClass,(Object*)&provider,&message);
   return FALSE;
 }
 assert(a==SA_RenderContext);*v=(IPTR)((struct Screen*)o)->current;
 if(raced_context)((struct Screen*)o)->current=raced_context;return TRUE;
}
'''
# The actual initial frame admission is independent of the capability getter.
# An unknown provider accepts ordinary legacy frames and refuses typed frames.
frameprefix=r'''
#define APTR void*
#define DRIF_DIRECTCOLOR 1
#define WDM_EXITWINDOW 1
#define WDM_PREPARESCALEWINDOW 2
struct wdpExitWindow{ULONG MethodID;BOOL truecolor;IPTR buffer;};
struct wdpInitWindow{ULONG MethodID;BOOL wdp_TrueColor;IPTR wdp_UserBuffer,wdp_ScreenUserBuffer;struct Screen*wdp_Screen;};
struct wdpPrepareScaleWindow{struct wdpInitWindow base;const struct ScreenRenderContext*context;struct TextFont*font;LONG width,height,borderTop;};
struct WindowFrameGeneration{BOOL initialized;IPTR userbuffer;ULONG bufferSize;struct{ULONG dri_Flags;struct TextFont*dri_Font;}dri;const struct ScreenRenderContext*context;};
struct FrameMetadata{BOOL ScaleAware;struct WindowFrameGeneration*FrameGeneration;};
static struct FrameMetadata metadata;
#define IW(w) (&metadata)
#undef IS
#define IS(s) (&frameScreen)
static struct{Object WinDecorObj;IPTR DecorUserBuffer;}frameScreen;
static IPTR ProviderDoMethodA(Object obj,Msg msg){(void)obj;(void)msg;return provider.actual==&builtinClass||provider.actual==&themeClass;}
'''
frame=frame.replace('DoMethodA(', 'ProviderDoMethodA(')
# Extract the real handled/return branches from both callers, retaining cleanup.
sys=(root/'rom/intuition/sysrequest_intern.c').read_text()
a=sys.index('    BOOL handled = FALSE;',sys.index('struct Window *buildsysreq_intern('))
b=sys.index('    /* create everything */',a)
sysbranch=sys[a:b]
easy=(root/'rom/intuition/buildeasyrequestargs.c').read_text()
a=easy.index('                BOOL handled;');b=easy.index('                if (LogicalSystemRequesterScreen',a)
easybranch=easy[a:b]
callers=r'''
static struct Window sentinel;
static void UnlockPubScreen(void*n,struct Screen*s){assert(!n&&s);}
static void intrequest_freelabels(STRPTR*l,struct IntuitionBase*b){(void)b;FreeVec(l);}
static struct Window*logical_caller(struct Screen*scr){struct Screen view;const struct ScreenRenderContext*ctx=NULL;assert(LogicalSystemRequesterScreen(scr,&view,&ctx,&base));assert(ctx==scr->current);assert(view.RastPort.Font==ctx->src_LogicalDrawInfo->dri_Font);legacycalls++;return &sentinel;}
static struct Window*system_caller(struct Screen*scr){
 struct Screen*lockedscr=NULL;struct Window*req=NULL;STRPTR reqtitle="Admission";
 struct IntuiText body={.IText="Hello",.FrontPen=1,.DrawMode=JAM1},neg={.IText="Cancel"};
 struct IntuiText *bodytext=&body,*postext=NULL,*negtext=&neg;
 STRPTR gadgetlabels[]={"Cancel",NULL};ULONG IDCMPFlags=0;WORD width=0,height=0;struct IntuitionBase*IntuitionBase=&base;
SYSBRANCH
 return logical_caller(scr);
}
struct EasyData{struct IntReqDims ReqDims;STRPTR RawBody;};
static struct Window*easy_caller(struct Screen*scr){
 struct Screen*lockedscr=NULL;struct Window*req=NULL;STRPTR reqtitle="Admission";ULONG IDCMP=0;struct IntuitionBase*IntuitionBase=&base;
 struct EasyData*requserdata=AllocVec(sizeof(*requserdata),0);assert(requserdata);requserdata->RawBody=AllocVec(6,0);assert(requserdata->RawBody);strcpy(requserdata->RawBody,"Hello");requserdata->ReqDims.gadgets=1;
 STRPTR*gadgetlabels=AllocVec(2*sizeof(*gadgetlabels),0);assert(gadgetlabels);gadgetlabels[0]="Cancel";
EASYBRANCH
 intrequest_freelabels(gadgetlabels,IntuitionBase);FreeVec(requserdata->RawBody);FreeVec(requserdata);
 return logical_caller(scr);
}
'''.replace('SYSBRANCH',sysbranch).replace('EASYBRANCH',easybranch)
main=r'''
int main(void){UWORD pens[]={0,1,2,3};struct Screen screen={2560,1600};struct DrawInfo logical={&screen,&f13,pens},physical;struct ScreenRenderContext context;
 providerScreen.WinDecorObj=&provider;frameScreen.WinDecorObj=&provider;
 Class*classes[]={&builtinClass,&themeClass,&customClass,NULL};
 for(ULONG scale=100;scale<=400;scale+=25){
  struct TextFont font={(13*scale+50)/100,(10*scale+50)/100};physical=(struct DrawInfo){&screen,&font,pens};context=(struct ScreenRenderContext){.src_Scale=scale,.src_PhysicalDrawInfo=&physical,.src_LogicalDrawInfo=&logical,.src_LegacyScaleFactor=scale/100,.metrics={2,1,1,1}};screen.current=&context;
  for(unsigned k=0;k<4;k++){
   provider.actual=classes[k];struct Provider saved=provider;ULONG priorallocations=allocations;IPTR answer=99;struct opGet message={WDA_PreparedScaleWindow,&answer};
   assert(WinDecorClass__OM_GET(&builtinClass,(Object*)&provider,&message));assert(answer==(k==0));
   answer=99;assert(windecor_get(&themeClass,(Object*)&provider,&message));assert(answer==(k==1));assert(!memcmp(&saved,&provider,sizeof(provider))&&priorallocations==allocations);
   struct Window probe={.WScreen=&screen};struct WindowFrameGeneration gen={.context=&context,.dri={.dri_Font=&font}};metadata.FrameGeneration=&gen;
   metadata.ScaleAware=FALSE;assert(PrepareInitialWindowFrameLayout(&probe,&base));metadata.ScaleAware=TRUE;assert(PrepareInitialWindowFrameLayout(&probe,&base)==(k<2));
   for(unsigned caller=0;caller<2;caller++){
    unsigned before=legacycalls;struct Window*w=caller?easy_caller(&screen):system_caller(&screen);
    if(k<2){assert(w&&w!=&sentinel&&legacycalls==before);assert(FreeScaledSystemRequester(w,&base));}
    else assert(w==&sentinel&&legacycalls==before+1);
    assert(!live&&!SystemRequesters);
   }
   if(k<2){
    /* Fault each actual requester allocation. Caller scaffold allocations precede
     * constructor faults for the EasyRequest branch and are never faulted here. */
    for(unsigned caller=0;caller<2;caller++)for(ULONG n=1;n<=40;n++){
     unsigned before=legacycalls;allocations=0;failat=n+(caller?3:0);struct Window*w=caller?easy_caller(&screen):system_caller(&screen);
     assert(legacycalls==before);if(w)assert(FreeScaledSystemRequester(w,&base));assert(!live&&!SystemRequesters);failat=0;
    }
    /* Supported provider with unavailable snapshot also refuses without fallback. */
    screen.current=NULL;BOOL handled=FALSE;STRPTR labels[]={"Cancel",NULL};
    assert(!OpenScaledSystemRequester(&screen,"x","x",NULL,labels,1,0,0,0,&handled,&base)&&handled);screen.current=&context;
   }else{allocations=0;BOOL handled=TRUE;STRPTR labels[]={"Cancel",NULL};assert(!OpenScaledSystemRequester(&screen,"x","x",NULL,labels,1,0,0,0,&handled,&base)&&!handled&&!allocations);}
  }
 }
 assert(queries);puts("requester capability PASS actual builtin/theme OM_GET + exact/inherited/unknown classes; actual frame admission; both caller return/cleanup branches;13 scales;2080 allocation fault-position attempts; unsupported logical / supported OOM refusal");
}
'''
production=ns['production']
variants=[('production',builtin,theme,production,True),
 ('builtin-inherited-optin',builtin.replace('OCLASS(obj) == cl','TRUE'),theme,production,False),
 ('theme-inherited-optin',builtin,theme.replace('OCLASS(obj) == cl','TRUE'),production,False),
 ('builtin-optin-lost',builtin.replace('OCLASS(obj) == cl','FALSE'),theme,production,False),
 ('theme-optin-lost',builtin,theme.replace('OCLASS(obj) == cl','FALSE'),production,False),
 ('unsupported-handled',builtin,theme,production.replace('if (!GetAttr(WDA_PreparedScaleWindow','*handled = TRUE;\n    if (!GetAttr(WDA_PreparedScaleWindow'),False),
 ('supported-oom-fallback',builtin,theme,production.replace('if (!owner) return NULL;','if (!owner) { *handled = FALSE; return NULL; }'),False),
 ('ignored-capability',builtin,theme,production.replace('if (!GetAttr(WDA_PreparedScaleWindow, IS(screen)->WinDecorObj, &value) || !value)','if (FALSE)'),False)]
with tempfile.TemporaryDirectory(prefix='requestercapability-') as directory:
 out=Path(directory);unit=out/'test.c';binary=out/'test'
 for name,b,t,p,passes in variants:
  text=prefix+providers+b+'\n'+t+'\n'+getattr+ns['inputprefix'].replace('typedef struct Class{IPTR cl_UserData;}Class;','')+frameprefix+frame+'\n'+ns['handler_prefix']+p+callers+main
  unit.write_text(text)
  subprocess.run(['clang','-std=c11','-g','-O1','-Wno-incompatible-pointer-types','-Wno-unused-function','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(unit),'-o',str(binary)],check=True)
  result=subprocess.run([str(binary)],capture_output=True,text=True)
  assert (result.returncode==0)==passes,(name,result.stdout,result.stderr)
  print(result.stdout if passes else 'REJECTED '+name,end='' if passes else '\n')
