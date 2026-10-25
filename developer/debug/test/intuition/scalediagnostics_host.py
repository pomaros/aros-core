#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License 1.1.
"""Actual shared scale attempts, CLI main and Settings failure/cancel handlers.

SetAttrs/GetAttr, Settings storage and MUI delivery are explicit OS adapters.
The diagnostic ABI is extracted from the actual public header. Engine geometry
payload coupling is supplied separately by the engine fixture; no OS run claimed.
"""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
ROOT=Path(__file__).resolve().parents[4]
def function(text,name):
 m=re.search(r'(?:static\s+)?(?:int|BOOL|void|IPTR|LONG|const char \*)\s*'+name+r'\s*\([^;]*?\)\s*\{',text);assert m,name
 i=m.end();depth=1
 while depth:depth+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[m.start():i]
def contract():
 text=(ROOT/'compiler/include/intuition/screens.h').read_text(encoding='latin1')
 defs='\n'.join(l.split('/*')[0] for l in text.splitlines() if re.match(r'#define\s+(SA_Dummy|SA_Scale)\s',l))
 return defs+'\n'+text[text.index('#define SA_ScaleResult'):text.index('#define SA_SCALE_MIN')]
def shared():
 import sys
 sys.path.insert(0,str(ROOT/'developer/debug/test/intuition'))
 from scaleresult_fixture import detail_header
 return detail_header(ROOT)+'\n'.join(l for l in (ROOT/'workbench/include/scaleresult.h').read_text().splitlines() if not l.startswith('#include'))+'\n'
PREFIX=r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <assert.h>
typedef uintptr_t IPTR;typedef uint32_t ULONG;typedef int32_t LONG;typedef uint16_t UWORD;typedef uint8_t UBYTE;typedef int BOOL;typedef void Object;typedef void*APTR;typedef const char*CONST_STRPTR;typedef char*STRPTR;typedef void*Msg;typedef void Class;
#define TRUE 1
#define FALSE 0
#define TAG_USER 0x80000000U
#define TAG_DONE 0
#define AROS_UFH3(r,n,a,b,c) r n(a,b,c)
#define AROS_UFHA(t,n,r) t n
#define AROS_USERFUNC_INIT
#define AROS_USERFUNC_EXIT
#define AROS_LONG2BE(x) (x)
#define AROS_WORD2BE(x) (x)
#define MEMF_PUBLIC 1
#define RETURN_OK 0
#define RETURN_ERROR 10
#define RETURN_FAIL 20
#define SETTING_OK 0
#define SETTING_ERROR_NO_FILE 3
#define SETTING_LAYER_USER 1
#define SETTING_Layer 1
#define ID_PRHD 1
#define ID_SCRM 2
#define PHV_CURRENT 0
#define AUTOSCROLL 1
#define SMF_AUTOSCROLL 1
#define BMA_DEPTH 1
struct Library{UWORD lib_Version,lib_Revision;};static struct Library intuition={50,20},settings_library={1,0};static struct Library*IntuitionBase=&intuition;
struct Screen{UWORD Width,Height;ULONG Flags;struct{void*BitMap;}RastPort;int ViewPort;};static struct Screen live_screen={2560,1600};
static IPTR live_scale;static BOOL missing_screen,bad_readback;static unsigned pub_locks,sets,saves,notices,beeps;
static unsigned mode,fail_call;static LONG save_error;static const char*action;static BOOL temporary;
static char output[8192],notice[1024],notice_title[100];
struct TagItem{ULONG ti_Tag;IPTR ti_Data;};struct ScreenModePrefs{ULONG smp_Scale,smp_DisplayID;UWORD smp_Width,smp_Height,smp_Depth,smp_Control;};static struct ScreenModePrefs screenmodeprefs;
struct PrefHeader{UBYTE ph_Version;};struct SettingChunk{ULONG sc_ID,sc_Size;void*sc_Data;};struct SettingReplaceMessage{struct SettingChunk*srm_Chunks;ULONG srm_Count,srm_Capacity;};
struct Hook{void*h_Entry,*h_Data;};typedef void*HOOKFUNC;
static struct Screen *LockPubScreen(STRPTR name){assert(!strcmp(name,"Workbench"));if(missing_screen)return NULL;pub_locks++;return &live_screen;}
static void UnlockPubScreen(void*x,struct Screen*s){assert(s==&live_screen&&pub_locks);pub_locks--;}
static BOOL GetAttr(ULONG tag,Object*s,IPTR*value){assert(tag==SA_Scale&&s==(Object*)&live_screen);if(bad_readback)return FALSE;*value=live_scale;return TRUE;}
static void apply_payload(struct ScreenScaleResult*r,IPTR wanted){
 for(unsigned i=0;i<sizeof(*r);i++)assert(!((UBYTE*)r)[i]);
 r->ssr_RequestedScale=wanted;r->ssr_AppliedScale=live_scale;
 if(mode==4){live_scale=wanted;memset(r,0,sizeof(*r));return;}
 if(sets==fail_call||mode==1){
  r->ssr_Status=SSR_FAILED;r->ssr_Reason=SSR_REASON_GEOMETRY;r->ssr_Stage=SSR_STAGE_GEOMETRY;r->ssr_Domain=SSR_DOMAIN_LOGICAL;r->ssr_Axis=SSR_AXIS_HEIGHT;
  r->ssr_Window=(IPTR)0xdead1234;r->ssr_MinWidth=100;r->ssr_MinHeight=1580;r->ssr_MaxWidth=1280;r->ssr_MaxHeight=800;r->ssr_AvailableWidth=1280;r->ssr_AvailableHeight=800;
  strcpy((char*)r->ssr_WindowTitle,sets==2?"Energy restore":"Energy");return;
 }
 r->ssr_Status=live_scale==wanted?SSR_UNCHANGED:SSR_SUCCESS;live_scale=wanted;r->ssr_AppliedScale=wanted;
}
static IPTR SetAttrs(Object*s,ULONG first,...){
 assert(s==(Object*)&live_screen&&pub_locks);sets++;va_list ap;va_start(ap,first);ULONG tag=first;IPTR wanted=0;struct ScreenScaleResult*r=NULL;
 while(tag!=TAG_DONE){IPTR value=va_arg(ap,IPTR);if(tag==SA_Scale)wanted=value;else{assert(tag==SA_ScaleResult);r=(void*)value;}tag=va_arg(ap,ULONG);}va_end(ap);
 assert(r&&wanted);apply_payload(r,wanted);return TRUE;
}
static void append(const char*text){assert(strlen(output)+strlen(text)<sizeof(output));strcat(output,text);}
static void PutStr(const char*text){append(text);}
static void Printf(const char*format,...){
 va_list ap;va_start(ap,format);char text[1200];
 if(!strcmp(format,"DisplayScale: %s\n")||!strcmp(format,"DisplayScale: rollback not confirmed: %s\n"))snprintf(text,sizeof(text),format,va_arg(ap,char*));
 else if(!strcmp(format,"DisplayScale: save failed (settings error %ld).\n"))snprintf(text,sizeof(text),format,(long)va_arg(ap,LONG));
 else if(!strcmp(format,"DisplayScale: live scale restored to %ld%%.\n"))snprintf(text,sizeof(text),format,(long)va_arg(ap,LONG));
 else if(!strcmp(format,"DisplayScale: screen=%s scale=%ld%% %s\n")){const char*n=va_arg(ap,char*);LONG value=va_arg(ap,LONG);snprintf(text,sizeof(text),format,n,(long)value,va_arg(ap,char*));}
 else assert(!"untested CLI output path");va_end(ap);append(text);
}
struct RDArgs{int unused;};static struct RDArgs rd;
static struct RDArgs*ReadArgs(const char*template,IPTR*args,void*x){args[0]=(IPTR)action;args[1]=0;args[2]=temporary;return &rd;}
static void FreeArgs(struct RDArgs*p){assert(p==&rd);}
static LONG IoErr(void){return 0;}static void PrintFault(LONG e,const char*n){assert(0);}
#define Stricmp strcasecmp
static struct Library*OpenLibrary(const char*n,ULONG version){assert(!strcmp(n,"settings.library"));return &settings_library;}
static void CloseLibrary(struct Library*l){assert(l==&settings_library);}
static LONG SetSettingA(const char*n,const char*v,struct TagItem*t){assert(pub_locks);saves++;return save_error;}
static LONG ReplaceSettingsA(const char*n,struct Hook*h,struct TagItem*t){assert(0);return 0;}
static LONG GetSettingA(const char*n,char*v,size_t size,struct TagItem*t){assert(0);return 0;}
static ULONG GetVPModeID(void*v){return 0;}static ULONG GetBitMapAttr(void*b,ULONG a){return 32;}
static void*AllocVec(size_t n,ULONG flags){assert(0);return NULL;}static void FreeVec(void*p){assert(0);}
'''
UI=r'''
/* global/render model MUI's muiGlobalInfo/muiRenderInfo: NULL render info is
 * a Settings page removed by OM_REMMEMBER (Cleanup), both NULL a page never
 * connected. _app/_win abort when used without them, as Zune would fault. */
struct UIObject{IPTR values[4];void*global,*render;};static struct UIObject gear,editor;static int application,window,global_info,render_info;static Object*notice_app,*notice_win;
struct SMEditor_DATA{Object*scale;IPTR initial_live_scale;BOOL initial_live_captured;struct ScaleAttempt pending_attempt;BOOL gesture_held,pending_failure;};static struct SMEditor_DATA editor_data;
#define SETUP_INST_DATA struct SMEditor_DATA*data=&editor_data
#define MUIA_Numeric_Value 0
#define MUIA_PrefsEditor_Changed 1
#define MUIA_PrefsEditor_Testing 2
#define MADF_DRAWOBJECT 1
#define XGET(o,a) (((struct UIObject*)(o))->values[a])
#define NNSET(o,a,v) do{assert(!pub_locks);XGET(o,a)=(v);}while(0)
#define SET NNSET
#define set NNSET
#define muiGlobalInfo(o) (((struct UIObject*)(o))->global)
#define muiRenderInfo(o) (((struct UIObject*)(o))->render)
#define _app(o) (assert(muiGlobalInfo(o)),(Object*)&application)
#define _win(o) (assert(muiRenderInfo(o)),(Object*)&window)
static void MUI_Redraw(Object*o,ULONG flags){assert(!pub_locks);}
static void DisplayBeep(void*s){assert(!s&&!pub_locks);beeps++;}
static IPTR MUI_RequestA(Object*app,Object*win,ULONG flags,const char*title,const char*buttons,const char*format,IPTR*args){assert(!pub_locks&&!strcmp(format,"%s"));notices++;notice_app=app;notice_win=win;snprintf(notice,sizeof(notice),"%s",(char*)args[0]);snprintf(notice_title,sizeof(notice_title),"%s",title);return 1;}
static IPTR DoSuperMethodA(Class*c,Object*o,Msg msg){return TRUE;}
'''
DRIVER=r'''
static void reset(void){live_scale=100;mode=fail_call=sets=saves=notices=beeps=pub_locks=0;missing_screen=bad_readback=FALSE;save_error=0;intuition.lib_Version=50;intuition.lib_Revision=20;action="200";temporary=FALSE;output[0]=notice[0]=notice_title[0]=0;SettingsBase=NULL;memset(&gear,0,sizeof(gear));memset(&editor,0,sizeof(editor));editor.global=&global_info;editor.render=&render_info;notice_app=notice_win=NULL;editor_data=(struct SMEditor_DATA){.scale=&gear,.initial_live_scale=100,.initial_live_captured=TRUE};}
static IPTR held_true=TRUE,held_false=FALSE;
int main(void){
 reset();mode=1;assert(display_scale_main()==RETURN_FAIL);assert(!saves&&sets==1&&!pub_locks);assert(strstr(output,"Energy")&&strstr(output,"dead1234")&&strstr(output,"1580")&&strstr(output,"800 logical pixels")&&strstr(output,"Current scale: 100%"));
 reset();save_error=5;assert(display_scale_main()==RETURN_FAIL);assert(saves==1&&sets==2&&live_scale==100&&!pub_locks);assert(strstr(output,"save failed")&&strstr(output,"restored to 100%"));
 reset();save_error=5;fail_call=2;assert(display_scale_main()==RETURN_FAIL);assert(saves==1&&sets==2&&live_scale==200&&!pub_locks);assert(strstr(output,"rollback not confirmed")&&strstr(output,"Energy restore")&&strstr(output,"Current scale: 200%")&&!strstr(output,"restored to"));
 reset();intuition.lib_Revision=19;assert(display_scale_main()==RETURN_FAIL);assert(!sets&&!saves&&live_scale==100&&strstr(output,"50.20")&&strstr(output,"no change was requested"));
 reset();mode=1;action="100";assert(display_scale_main()==RETURN_FAIL);assert(!saves&&live_scale==100);
 reset();mode=4;assert(display_scale_main()==RETURN_FAIL);assert(sets==1&&!saves&&live_scale==200&&strstr(output,"did not report SA_ScaleResult"));
 reset();assert(display_scale_main()==RETURN_OK);assert(sets==1&&saves==1&&live_scale==200&&strstr(output,"saved"));
 reset();temporary=TRUE;assert(display_scale_main()==RETURN_OK);assert(sets==1&&!saves&&live_scale==200&&strstr(output,"temporary"));
 reset();action="100";assert(display_scale_main()==RETURN_OK);assert(sets==1&&saves==1&&live_scale==100);
 reset();mode=1;gear.values[0]=4;struct Hook hook={.h_Data=&editor_data};ScaleChanged(&hook,&editor,NULL);assert(sets==1&&notices==1&&!pub_locks&&gear.values[0]==0&&!editor.values[1]&&!editor.values[2]);assert(strstr(notice,"1580")&&strstr(notice,"800 logical pixels")&&strstr(notice,"dead1234")&&!beeps&&notice_app==(Object*)&application&&notice_win==(Object*)&window);
 reset();gear.values[0]=4;ScaleChanged(&hook,&editor,NULL);assert(live_scale==200&&!notices&&editor.values[1]&&editor.values[2]);
 reset();mode=1;live_scale=200;editor.values[1]=editor.values[2]=TRUE;assert(!SMEditor__MUIM_PrefsEditor_Cancel(NULL,&editor,NULL));assert(live_scale==200&&gear.values[0]==4&&editor.values[1]&&editor.values[2]&&notices==1&&!saves&&!pub_locks);assert(strstr(notice_title,"Restore")&&strstr(notice,"Current scale: 200%"));
 reset();live_scale=200;editor.values[1]=editor.values[2]=TRUE;assert(SMEditor__MUIM_PrefsEditor_Cancel(NULL,&editor,NULL));assert(live_scale==100&&!editor.values[1]&&!editor.values[2]&&!notices&&!saves);
 reset();live_scale=175;editor_data.initial_live_captured=FALSE;editor.values[2]=TRUE;assert(!SMEditor__MUIM_PrefsEditor_Cancel(NULL,&editor,NULL));assert(notices==1&&!sets&&gear.values[0]==3&&strstr(notice,"not captured"));
 reset();mode=1;live_scale=200;screenmodeprefs.smp_Scale=100;assert(!SMEditor__MUIM_PrefsEditor_Revert(NULL,&editor,NULL));assert(gear.values[0]==4&&editor.values[1]&&editor.values[2]&&notices==1&&!pub_locks);
 reset();intuition.lib_Revision=19;gear.values[0]=4;ScaleChanged(&hook,&editor,NULL);assert(!sets&&notices==1&&gear.values[0]==0&&strstr(notice,"50.20"));
 reset();LockPubScreen("Workbench");struct ScaleAttempt attempt;memset(&attempt,0x5a,sizeof(attempt));ScaleAttemptApply(&live_screen,200,&attempt);assert(ScaleAttemptSucceeded(&attempt));mode=1;ScaleAttemptApply(&live_screen,100,&attempt);assert(!ScaleAttemptSucceeded(&attempt)&&attempt.result.ssr_Reason==SSR_REASON_GEOMETRY);mode=0;ScaleAttemptApply(&live_screen,200,&attempt);assert(ScaleAttemptSucceeded(&attempt)&&attempt.result.ssr_Reason==SSR_REASON_NONE&&!attempt.result.ssr_WindowTitle[0]);
 UnlockPubScreen(NULL,&live_screen);char text[SCALE_DIAGNOSTIC_SIZE];attempt.result.ssr_Status=SSR_FAILED;attempt.result.ssr_Reason=SSR_REASON_PREPARE_FAILED;attempt.result.ssr_Stage=SSR_STAGE_POINTER;ScaleAttemptFormat(&attempt,text,sizeof(text));assert(strstr(text,"Pointer preparation")&&!strstr(text,"memory"));
 attempt.result.ssr_Reason=SSR_REASON_NO_MEMORY;attempt.result.ssr_Stage=SSR_STAGE_FONT;ScaleAttemptFormat(&attempt,text,sizeof(text));assert(strstr(text,"Font preparation")&&strstr(text,"memory allocation"));
 attempt.result.ssr_Reason=SSR_REASON_GEOMETRY;attempt.result.ssr_Domain=SSR_DOMAIN_LOGICAL;attempt.result.ssr_Axis=SSR_AXIS_HEIGHT;attempt.result.ssr_MinHeight=1580;attempt.result.ssr_AvailableHeight=800;attempt.result.ssr_Window=(IPTR)0xdead5678;strcpy((char*)attempt.result.ssr_WindowTitle,"Energy%\n\t\"test");ScaleAttemptFormat(&attempt,text,sizeof(text));assert(strstr(text,"Energy%   test")&&strstr(text,"dead5678")&&!strchr(text,'\n')&&!strchr(text,'\t'));
 attempt.result.ssr_WindowTitle[0]=0;ScaleAttemptFormat(&attempt,text,sizeof(text));assert(strstr(text,"Window 0xdead5678"));
 memset(attempt.result.ssr_WindowTitle,'X',sizeof(attempt.result.ssr_WindowTitle));ScaleAttemptFormat(&attempt,text,sizeof(text));assert(strlen(text)<sizeof(text));
 attempt.result.ssr_WindowTitle[0]=0;attempt.result.ssr_Window=(IPTR)0xdead1234;attempt.result.ssr_Axis=SSR_AXIS_WIDTH;attempt.result.ssr_Domain=SSR_DOMAIN_PHYSICAL;attempt.result.ssr_MinWidth=2000;attempt.result.ssr_AvailableWidth=1280;ScaleAttemptFormat(&attempt,text,sizeof(text));assert(strstr(text,"minimum width of 2000")&&strstr(text,"1280 physical pixels"));
 attempt.result.ssr_Domain=SSR_DOMAIN_NONE;attempt.result.ssr_Reason=SSR_REASON_INVALID_CONTEXT;attempt.result.ssr_Stage=SSR_STAGE_GEOMETRY;ScaleAttemptFormat(&attempt,text,sizeof(text));assert(strstr(text,"geometry is invalid or out of range")&&!strstr(text,"memory"));
 for(unsigned stage=SSR_STAGE_VALIDATE;stage<=SSR_STAGE_COMMIT;stage++){attempt.result.ssr_Reason=SSR_REASON_PREPARE_FAILED;attempt.result.ssr_Stage=stage;ScaleAttemptFormat(&attempt,text,sizeof(text));assert(strstr(text,ScaleResultStage(stage)));}
 reset();bad_readback=TRUE;gear.values[0]=4;ScaleChanged(&hook,&editor,NULL);assert(!notices&&live_scale==200&&editor.values[2]);/* Explicit applied-scale receipt remains authoritative. */
 strcpy((char*)attempt.result.ssr_WindowTitle,"Energy");attempt.result.ssr_Window=(IPTR)0x1111;char duplicate[SCALE_DIAGNOSTIC_SIZE];ScaleAttemptFormat(&attempt,text,sizeof(text));attempt.result.ssr_Window=(IPTR)0x2222;ScaleAttemptFormat(&attempt,duplicate,sizeof(duplicate));assert(strcmp(text,duplicate)&&strstr(text,"Energy")&&strstr(duplicate,"Energy"));
 struct{char before;char text[24];char after;}bounded={.before='A',.after='B'};ScaleAttemptFormat(&attempt,bounded.text,sizeof(bounded.text));assert(bounded.before=='A'&&bounded.after=='B'&&strlen(bounded.text)<sizeof(bounded.text));
 /* Removed Settings page: Cleanup cleared render info (Zune keeps global info). */
 reset();mode=1;live_scale=200;editor.render=NULL;editor.values[1]=editor.values[2]=TRUE;assert(!SMEditor__MUIM_PrefsEditor_Cancel(NULL,&editor,NULL));assert(notices==1&&notice_app==(Object*)&application&&!notice_win&&live_scale==200&&editor.values[2]);
 reset();mode=1;live_scale=200;editor.global=editor.render=NULL;screenmodeprefs.smp_Scale=100;assert(!SMEditor__MUIM_PrefsEditor_Revert(NULL,&editor,NULL));assert(notices==1&&!notice_app&&!notice_win&&editor.values[1]&&editor.values[2]);
 reset();live_scale=175;editor.global=editor.render=NULL;editor_data.initial_live_captured=FALSE;editor.values[2]=TRUE;assert(!SMEditor__MUIM_PrefsEditor_Cancel(NULL,&editor,NULL));assert(notices==1&&!notice_app&&!notice_win&&strstr(notice,"not captured"));
 /* Held key: intermediate refusals beep; one requester on release reports the last refusal. */
 struct Hook gesture={.h_Data=&editor_data};
 reset();mode=1;ScaleGesture(&gesture,&editor,&held_true);assert(editor_data.gesture_held&&!notices);
 gear.values[0]=4;ScaleChanged(&hook,&editor,NULL);gear.values[0]=5;ScaleChanged(&hook,&editor,NULL);assert(sets==2&&beeps==2&&!notices&&gear.values[0]==0&&!pub_locks);
 ScaleGesture(&gesture,&editor,&held_false);assert(notices==1&&beeps==2&&!editor_data.gesture_held&&strstr(notice,"Cannot apply 225%")&&strstr(notice,"1580")&&!strcmp(notice_title,"Display scale"));
 ScaleGesture(&gesture,&editor,&held_true);ScaleGesture(&gesture,&editor,&held_false);assert(notices==1);
 reset();ScaleGesture(&gesture,&editor,&held_true);fail_call=1;gear.values[0]=4;ScaleChanged(&hook,&editor,NULL);assert(beeps==1&&!notices);gear.values[0]=4;ScaleChanged(&hook,&editor,NULL);assert(live_scale==200);ScaleGesture(&gesture,&editor,&held_false);assert(!notices);/* a later accepted step supersedes the refusal */
 reset();mode=1;editor.global=editor.render=NULL;ScaleGesture(&gesture,&editor,&held_true);gear.values[0]=4;ScaleChanged(&hook,&editor,NULL);ScaleGesture(&gesture,&editor,&held_false);assert(notices==1&&!notice_app&&!notice_win);
 /* C1 controls (0x9B is CSI on the AROS console) never reach the output. */
 memset(&attempt,0,sizeof(attempt));attempt.screen_available=attempt.api_available=TRUE;attempt.requested=200;attempt.result.ssr_Status=SSR_FAILED;attempt.result.ssr_Reason=SSR_REASON_PREPARE_FAILED;attempt.result.ssr_Stage=SSR_STAGE_FRAME;attempt.result.ssr_Window=(IPTR)0x3333;strcpy((char*)attempt.result.ssr_WindowTitle,"Ev\x9b" "2J\x80" "il\x9f\x7f" "\xa0" "ok");ScaleAttemptFormat(&attempt,text,sizeof(text));
 for(const char*c=text;*c;c++)assert(!((UBYTE)*c>=127&&(UBYTE)*c<=159));assert(strstr(text,"Ev 2J il  \xa0ok"));
 /* SUCCESS whose read-back differs: the reported applied scale and the read-back are both named. */
 memset(&attempt,0,sizeof(attempt));attempt.screen_available=attempt.api_available=attempt.actual_known=TRUE;attempt.requested=200;attempt.actual=175;attempt.result.ssr_Status=SSR_SUCCESS;attempt.result.ssr_RequestedScale=200;attempt.result.ssr_AppliedScale=200;ScaleAttemptFormat(&attempt,text,sizeof(text));assert(!ScaleAttemptSucceeded(&attempt)&&strstr(text,"applied scale 200%")&&strstr(text,"read-back scale: 175%"));
 attempt.actual_reported=TRUE;ScaleAttemptFormat(&attempt,text,sizeof(text));assert(strstr(text,"read-back scale: unavailable"));
 reset();bad_readback=TRUE;LockPubScreen("Workbench");ScaleAttemptApply(&live_screen,200,&attempt);UnlockPubScreen(NULL,&live_screen);assert(attempt.actual_known&&attempt.actual_reported&&attempt.actual==200);
 reset();LockPubScreen("Workbench");ScaleAttemptApply(&live_screen,200,&attempt);UnlockPubScreen(NULL,&live_screen);assert(attempt.actual_known&&!attempt.actual_reported);
 /* Exact frame refusal causes, in the shared wording of Intuition's kernel log line. */
 {static const struct{ULONG detail,method,refusal;LONG v[3];const char*name,*want;}cases[]={
  {SSR_DETAIL_FRAME_NO_GENERATION,0,0,{0,0,0},"","the window has no committed frame generation"},
  {SSR_DETAIL_FRAME_CONTEXT,0,0,{0,0,0},"","the render context has no draw information for the window's screen"},
  {SSR_DETAIL_FRAME_CONTEXT_BORDERS,0,0,{1,0,0},"","the render context has no logical frame border metrics"},
  {SSR_DETAIL_FRAME_CONTEXT_BORDERS,0,0,{0,0,0},"","the render context has no physical frame border metrics"},
  {SSR_DETAIL_FRAME_NO_FONT,0,0,{0,0,0},"","the render context has no frame font"},
  {SSR_DETAIL_FRAME_BORDER_RANGE,0,0,{1,140,0},"","the top border would be 140 pixels; frame borders must be 0..127"},
  {SSR_DETAIL_FRAME_SIZE_IMAGE,0,0,{1,36,128},"","the size gadget image query returned status 1 with 36 x 128 pixels; it must be ready and 1..127 pixels"},
  {SSR_DETAIL_FRAME_LAYOUT_SIZE,0,0,{0,400,0},"","the proposed frame size 0 x 400 is outside 1..32767 or the frame was already prepared"},
  {SSR_DETAIL_FRAME_USER_BUFFER,0,0,{-1,0,0},"","the window decorator's buffer size -1 is invalid"},
  {SSR_DETAIL_FRAME_UNTYPED_GADGET,0,0,{10,0,0},"","system gadget slot 10 has an image but no system image type"},
  {SSR_DETAIL_FRAME_IMAGE_QUERY,0,0,{0,0,0},"close","the window decorator (WDM_GETSCALEDEFSIZE_SYSIMAGE) returned status 0 for the 'close' image with 0 x 0 pixels"},
  {SSR_DETAIL_FRAME_IMAGE_EXTENT,0,0,{40000,20,-1},"depth","the 'depth' image extent 40000 x 20 (preferred height -1) is outside 1..32767"},
  {SSR_DETAIL_FRAME_IMAGE_OBJECT,0,0,{36,36,0},"size","the 'size' system image object (36 x 36) could not be created"},
  {SSR_DETAIL_FRAME_METRICS,0,0,{0,0,0},"","the window is missing from the measured frame set"},
  {SSR_DETAIL_DECORATOR,WDM_LAYOUTSCALEBORDERGADGETS,WDP_REFUSE_SUPERCLASS,{WDM_LAYOUTSCALEBORDERGADGETS,0,0},"","frame gadget layout was refused by the window decorator (WDM_LAYOUTSCALEBORDERGADGETS): its superclass refused WDM_LAYOUTSCALEBORDERGADGETS"},
  {SSR_DETAIL_DECORATOR,WDM_PREPARESCALEWINDOW,WDP_REFUSE_UNSUPPORTED,{0,0,0},"","frame resource preparation was refused by the window decorator (WDM_PREPARESCALEWINDOW): the decorator class does not implement this method"},
  {SSR_DETAIL_DECORATOR,WDM_PREPARESCALEWINDOW,WDP_REFUSE_ARGUMENTS,{200,0,400},"","frame resource preparation was refused by the window decorator (WDM_PREPARESCALEWINDOW): the request is invalid (scale 200%, 0 x 400)"},
  {SSR_DETAIL_DECORATOR,WDM_LAYOUTSCALEBORDERGADGETS,WDP_REFUSE_STATE,{0,0,0},"","frame gadget layout was refused by the window decorator (WDM_LAYOUTSCALEBORDERGADGETS): the window decoration buffer is missing or in the wrong state"},
  {SSR_DETAIL_DECORATOR,WDM_PREPARESCALEWINDOW,WDP_REFUSE_NO_MEMORY,{4096,0,0},"","frame resource preparation was refused by the window decorator (WDM_PREPARESCALEWINDOW): allocation of 4096 bytes failed"},
  {SSR_DETAIL_DECORATOR,WDM_PREPARESCALEWINDOW,WDP_REFUSE_CONFIG,{70000,200,0},"BarJoinTB","frame resource preparation was refused by the window decorator (WDM_PREPARESCALEWINDOW): theme value 'BarJoinTB' = 70000 is out of range at 200%"},
  {SSR_DETAIL_DECORATOR,WDM_PREPARESCALEWINDOW,WDP_REFUSE_IMAGE_GRID,{57,19,401},"System/Close/Default","frame resource preparation was refused by the window decorator (WDM_PREPARESCALEWINDOW): theme image 'System/Close/Default' (57 x 19) does not divide into its 4 x 1 states"},
  {SSR_DETAIL_DECORATOR,WDM_GETSCALEDEFSIZE_SYSIMAGE,WDP_REFUSE_IMAGE_EXTENT,{28,38,0},"System/Depth/Default","system image sizing was refused by the window decorator (WDM_GETSCALEDEFSIZE_SYSIMAGE): theme image 'System/Depth/Default' cannot be prepared at 28 x 38 pixels per state"},
  {SSR_DETAIL_DECORATOR,WDM_PREPARESCALEWINDOW,WDP_REFUSE_ELEMENT,{40,30,56},"System/Titlebar/Default","frame resource preparation was refused by the window decorator (WDM_PREPARESCALEWINDOW): a theme element of image 'System/Titlebar/Default' at offset 40 size 30 exceeds its 56 pixel extent"},
  {SSR_DETAIL_DECORATOR,WDM_PREPARESCALEWINDOW,WDP_REFUSE_FONT,{0,19,0},"","frame resource preparation was refused by the window decorator (WDM_PREPARESCALEWINDOW): font height 0 cannot be matched to theme bar height 19"},
  {SSR_DETAIL_DECORATOR,WDM_LAYOUTSCALEBORDERGADGETS,WDP_REFUSE_NONE,{0,0,0},"","frame gadget layout was refused by the window decorator (WDM_LAYOUTSCALEBORDERGADGETS): the decorator stated no cause"},
 };
 for(unsigned c=0;c<sizeof(cases)/sizeof(cases[0]);c++){char want[SCALE_DIAGNOSTIC_SIZE];memset(&attempt,0,sizeof(attempt));attempt.screen_available=attempt.api_available=attempt.actual_known=TRUE;attempt.requested=200;attempt.actual=100;attempt.result.ssr_Status=SSR_FAILED;attempt.result.ssr_Reason=cases[c].detail==SSR_DETAIL_FRAME_METRICS?SSR_REASON_INVALID_CONTEXT:SSR_REASON_PREPARE_FAILED;attempt.result.ssr_Stage=SSR_STAGE_FRAME;attempt.result.ssr_Window=(IPTR)0x3333;strcpy((char*)attempt.result.ssr_WindowTitle,"Energy - arrows/wheel scroll");attempt.result.ssr_Detail=cases[c].detail;attempt.result.ssr_DetailMethod=cases[c].method;attempt.result.ssr_DecorRefusal=cases[c].refusal;memcpy(attempt.result.ssr_DetailValue,cases[c].v,sizeof(cases[c].v));strcpy((char*)attempt.result.ssr_DetailName,cases[c].name);ScaleAttemptFormat(&attempt,text,sizeof(text));snprintf(want,sizeof(want),"Cannot apply 200%%. Window frame preparation for Window \"Energy - arrows/wheel scroll\" [%p]: %s. Current scale: 100%%.",(void*)(IPTR)0x3333,cases[c].want);if(strcmp(text,want)){printf("detail %u\n got %s\nwant %s\n",c,text,want);abort();}}
  /* A decorator name is owned data: unterminated, quotes and controls stay one bounded line. */
  memset(attempt.result.ssr_DetailName,'q',sizeof(attempt.result.ssr_DetailName));attempt.result.ssr_DetailName[3]='\x9b';attempt.result.ssr_DetailName[4]='\'';attempt.result.ssr_DetailName[5]='\n';attempt.result.ssr_Detail=SSR_DETAIL_DECORATOR;attempt.result.ssr_DecorRefusal=WDP_REFUSE_CONFIG;ScaleAttemptFormat(&attempt,text,sizeof(text));assert(strlen(text)<sizeof(text)&&!strchr(text,'\n')&&strstr(text,"'qqq   qqqq"));{char quoted[80]="'qqq   ";for(unsigned q=6;q<sizeof(attempt.result.ssr_DetailName);q++)strcat(quoted,"q");strcat(quoted,"' = ");assert(strstr(text,quoted));}for(const char*c=text;*c;c++)assert(!((UBYTE)*c>=127&&(UBYTE)*c<=159));
  attempt.result.ssr_Detail=SSR_DETAIL_NONE;ScaleAttemptFormat(&attempt,text,sizeof(text));assert(strstr(text,": preparation was refused."));
 printf("PASS exact frame refusal details: %u frame checks and decorator causes, unterminated/control decorator names bounded\n",(unsigned)(sizeof(cases)/sizeof(cases[0])));}
 /* Exact layer refusal causes: Intuition's role, the Layers check, the
    lease step and the driver's own cause, in the kernel log's words. */
 {static const struct{ULONG detail,role,check,flags,pixels,lease,driver;LONG v[4];IPTR layer;const char*name;int window;const char*want;}cases[]={
  {SSR_DETAIL_LAYER,SSR_LAYER_BAR,LGR_BOUNDS,0,0,0,0,{2560,-1,0,0},1,"",0,"the screen title bar layer: its proposed size 2560 x -1 is outside 1..32767"},
  {SSR_DETAIL_LAYER,SSR_LAYER_WINDOW,LGR_UPDATING,0,0,0,0,{0,0,0,0},1,"",1,"the window layer: it is between BeginUpdate and EndUpdate (a refresh is in progress)"},
  {SSR_DETAIL_LAYER,SSR_LAYER_ROOT,LGR_PIXELS,LGRF_ROOT,LGR_PIXELS_STACK,LGR_LEASE_DRIVER,HIDD_PIXELS_REFUSE_SCANOUT,{HIDD_SCANOUT_NOT_OWNED,0,0,0},1,"hidd.bitmap.efifb",0,"the screen root layer: the pixel lease of the screen bitmap was refused: 'hidd.bitmap.efifb' refused PreparePixels: the shown bitmap differs from the scanout: the boot console still owns the framebuffer"},
  {SSR_DETAIL_LAYER,SSR_LAYER_WINDOW_CLIENT,LGR_PIXELS,0,LGR_PIXELS_STACK,LGR_LEASE_DRIVER,HIDD_PIXELS_REFUSE_SCANOUT,{HIDD_SCANOUT_MASKS,0x3ff00000,0,0},1,"hidd.bitmap.efifb",1,"the window client layer: the pixel lease of the screen bitmap was refused: 'hidd.bitmap.efifb' refused PreparePixels: the shown bitmap differs from the scanout: the scanout red mask is 0x3ff00000"},
  {SSR_DETAIL_LAYER,SSR_LAYER_CANVAS_ROOT,LGR_PIXELS,LGRF_ROOT|LGRF_PRIVATE,LGR_PIXELS_STACK,LGR_LEASE_DRIVER,HIDD_PIXELS_REFUSE_SUBCLASS,{0,0,0,0},1,"hidd.gfx.chunkybitmap",1,"the root layer of the window's logical canvas: the pixel lease of the private canvas bitmap was refused: 'hidd.gfx.chunkybitmap' refused PreparePixels: the object is a subclass of the audited class"},
  {SSR_DETAIL_LAYER,SSR_LAYER_WINDOW,LGR_PIXELS,0,LGR_PIXELS_OBSCURED,LGR_LEASE_MEMORY,0,{16384000,0,0,0},1,"",1,"the window layer: the pixel lease of a hidden smart-refresh backing bitmap was refused: allocation of 16384000 bytes for its copy failed"},
  {SSR_DETAIL_LAYER,SSR_LAYER_PRESENTATION,LGR_NO_MEMORY,0,0,0,0,{32174080,LGR_MEM_CONTENT,2560,1571},1,"",1,"the window presentation layer: allocation of 32174080 bytes for the detached canvas of 2560 x 1571 pixels failed"},
  {SSR_DETAIL_LAYER,SSR_LAYER_OTHER,LGR_SHAPE_HOOK,0,0,0,0,{200,100,1,0},0x4444,"",0,"layer 0x4444: its client shape hook would have to run for its new 200 x 100 replaced shape"},
  {SSR_DETAIL_LAYER,SSR_LAYER_ROOT,LGR_PIXELS,LGRF_ROOT,LGR_PIXELS_STACK,LGR_LEASE_CURSOR,HIDD_PIXELS_REFUSE_CURSOR_RENDER,{HIDD_CURSOR_REFUSE_TARGET,1,0,0},1,"CursorFB",0,"the screen root layer: the pixel lease of the screen bitmap was refused: 'CursorFB' refused RenderPreparedCursor: the live pointer is drawn on another bitmap"},
  {SSR_DETAIL_LAYER,SSR_LAYER_NONE,LGR_CURSOR_OWNER,0,0,0,0,{0,0,0,0},0,"",0,"the cursor bitmap is not the physical screen bitmap"},
  {SSR_DETAIL_LAYER,SSR_LAYER_CANVAS,LGR_PIXELS,LGRF_PRIVATE,LGR_PIXELS_STACK,LGR_LEASE_RANGE,0,{1270,0,20,8},1,"",1,"the window layer in its logical canvas: the pixel lease of the private canvas bitmap was refused: the rectangle 20 x 8 at 1270,0 is outside the mapping"},
  {SSR_DETAIL_LAYER_REQUEST,SSR_LAYER_REQUESTER,0,0,0,0,0,{10,20,40000,30},1,"",1,"a requester layer: the proposed rectangle 40000 x 30 at 10,20 is outside the 16-bit layer range"},
  {SSR_DETAIL_LAYER,SSR_LAYER_WINDOW,LGR_NONE,0,0,0,0,{0,0,0,0},1,"",1,"the window layer: Layers stated no cause"}};
 for(unsigned c=0;c<sizeof(cases)/sizeof(cases[0]);c++){char want[SCALE_DIAGNOSTIC_SIZE];memset(&attempt,0,sizeof(attempt));attempt.screen_available=attempt.api_available=attempt.actual_known=TRUE;attempt.requested=200;attempt.actual=100;
  struct ScreenScaleResult*r=&attempt.result;r->ssr_Status=SSR_FAILED;r->ssr_Reason=SSR_REASON_PREPARE_FAILED;r->ssr_Stage=SSR_STAGE_LAYER_GEOMETRY;
  if(cases[c].window){r->ssr_Window=(IPTR)0x3333;strcpy((char*)r->ssr_WindowTitle,"Wanderer");}
  r->ssr_Detail=cases[c].detail;r->ssr_LayerRole=cases[c].role;r->ssr_LayerCheck=cases[c].check;r->ssr_LayerFlags=cases[c].flags;r->ssr_LayerPixels=cases[c].pixels;r->ssr_LayerLease=cases[c].lease;r->ssr_LayerDriver=cases[c].driver;
  memcpy(r->ssr_LayerValue,cases[c].v,sizeof(cases[c].v));r->ssr_Layer=cases[c].layer;strcpy((char*)r->ssr_DetailName,cases[c].name);
  ScaleAttemptFormat(&attempt,text,sizeof(text));
  if(cases[c].window)snprintf(want,sizeof(want),"Cannot apply 200%%. Layer geometry preparation for Window \"Wanderer\" [%p]: %s. Current scale: 100%%.",(void*)(IPTR)0x3333,cases[c].want);
  else snprintf(want,sizeof(want),"Cannot apply 200%%. Layer geometry preparation: %s. Current scale: 100%%.",cases[c].want);
  if(strcmp(text,want)){printf("layer detail %u\n got %s\nwant %s\n",c,text,want);abort();}}
  /* The composition detail uses the shared three values. */
  memset(&attempt,0,sizeof(attempt));attempt.screen_available=attempt.api_available=attempt.actual_known=TRUE;attempt.requested=200;attempt.actual=100;attempt.result.ssr_Status=SSR_FAILED;attempt.result.ssr_Reason=SSR_REASON_PREPARE_FAILED;attempt.result.ssr_Stage=SSR_STAGE_LAYER_GEOMETRY;attempt.result.ssr_Detail=SSR_DETAIL_COMPOSITION;attempt.result.ssr_DetailValue[0]=2560;attempt.result.ssr_DetailValue[1]=3200;
  ScaleAttemptFormat(&attempt,text,sizeof(text));assert(!strcmp(text,"Cannot apply 200%. Layer geometry preparation: the legacy window composition could not project its shape to 2560 x 3200 physical pixels. Current scale: 100%."));
  /* Every recorded code has its own words: none falls to a default. */
  unsigned worded=0;struct ScreenScaleResult r;char clause[320];
  for(ULONG check=1;check<=LGR_SCREEN_OFFSET;check++){memset(&r,0,sizeof r);r.ssr_Detail=SSR_DETAIL_LAYER;r.ssr_LayerCheck=check;r.ssr_LayerPixels=LGR_PIXELS_STACK;r.ssr_LayerLease=LGR_LEASE_NOT_HIDD;r.ssr_LayerValue[1]=LGR_MEM_BATCH;assert(ScaleDetailFormat(&r,clause,sizeof clause));assert(!strstr(clause,"stated no cause")&&!strstr(clause,"unknown"));worded++;}
  for(ULONG lease=1;lease<=LGR_LEASE_CURSOR;lease++){memset(&r,0,sizeof r);r.ssr_Detail=SSR_DETAIL_LAYER;r.ssr_LayerCheck=LGR_PIXELS;r.ssr_LayerPixels=LGR_PIXELS_STACK;r.ssr_LayerLease=lease;r.ssr_LayerDriver=HIDD_PIXELS_REFUSE_UNSUPPORTED;assert(ScaleDetailFormat(&r,clause,sizeof clause));assert(!strstr(clause,"unknown")&&!strstr(clause,"stated no cause"));worded++;}
  for(ULONG kind=1;kind<=LGR_PIXELS_SOURCE;kind++){memset(&r,0,sizeof r);r.ssr_Detail=SSR_DETAIL_LAYER;r.ssr_LayerCheck=LGR_PIXELS;r.ssr_LayerPixels=kind;r.ssr_LayerLease=LGR_LEASE_OOP;assert(ScaleDetailFormat(&r,clause,sizeof clause));assert(!strstr(clause,"of a bitmap"));worded++;}
  for(ULONG what=1;what<=LGR_MEM_ROW;what++){memset(&r,0,sizeof r);r.ssr_Detail=SSR_DETAIL_LAYER;r.ssr_LayerCheck=LGR_NO_MEMORY;r.ssr_LayerValue[1]=what;assert(ScaleDetailFormat(&r,clause,sizeof clause));assert(!strstr(clause,"unknown buffer"));worded++;}
  for(ULONG driver=1;driver<=HIDD_PIXELS_REFUSE_FOREIGN;driver++){memset(&r,0,sizeof r);r.ssr_Detail=SSR_DETAIL_LAYER;r.ssr_LayerCheck=LGR_PIXELS;r.ssr_LayerPixels=LGR_PIXELS_STACK;r.ssr_LayerLease=LGR_LEASE_DRIVER;r.ssr_LayerDriver=driver;
   r.ssr_LayerValue[0]=driver==HIDD_PIXELS_REFUSE_SCANOUT?HIDD_SCANOUT_WIDTH:driver==HIDD_PIXELS_REFUSE_CURSOR_RENDER?HIDD_CURSOR_REFUSE_LEASE:4;
   assert(ScaleDetailFormat(&r,clause,sizeof clause));assert(!strstr(clause,"stated no cause")&&!strstr(clause,"unknown"));worded++;}
  for(LONG scan=1;scan<=HIDD_SCANOUT_PITCH;scan++){memset(&r,0,sizeof r);r.ssr_Detail=SSR_DETAIL_LAYER;r.ssr_LayerCheck=LGR_PIXELS;r.ssr_LayerLease=LGR_LEASE_DRIVER;r.ssr_LayerDriver=HIDD_PIXELS_REFUSE_SCANOUT;r.ssr_LayerValue[0]=scan;assert(ScaleDetailFormat(&r,clause,sizeof clause));assert(!strstr(clause,"unknown"));worded++;}
  for(LONG step=1;step<=HIDD_CURSOR_REFUSE_BACKUP;step++){memset(&r,0,sizeof r);r.ssr_Detail=SSR_DETAIL_LAYER;r.ssr_LayerCheck=LGR_PIXELS;r.ssr_LayerLease=LGR_LEASE_CURSOR;r.ssr_LayerDriver=HIDD_PIXELS_REFUSE_CURSOR_RENDER;r.ssr_LayerValue[0]=step;assert(ScaleDetailFormat(&r,clause,sizeof clause));assert(!strstr(clause,"unknown"));worded++;}
  for(ULONG role=SSR_LAYER_ROOT;role<=SSR_LAYER_PRIVATE;role++){memset(&r,0,sizeof r);r.ssr_Detail=SSR_DETAIL_LAYER;r.ssr_LayerRole=role;r.ssr_LayerCheck=LGR_NOT_MEMBER;assert(ScaleDetailFormat(&r,clause,sizeof clause));assert(strncmp(clause,"layer:",6)&&strstr(clause,": it is not in the prepared batch"));worded++;}
  printf("PASS exact layer refusal details: %u formatted cases, %u codes each worded (checks, lease steps, buffers, allocations, driver causes, scanout and cursor steps, roles)\n",(unsigned)(sizeof(cases)/sizeof(cases[0]))+1,worded);}
 puts("PASS actual CLI refusal/no-save, verified save rollback/refused rollback, 50.19 no-mutation, per-attempt reset, Settings lock/requester order and actual gear/cancel/revert; removed-page requester fallback; held-key beep with one release requester; C0/C1-safe bounded owned-caption diagnostics; applied vs read-back mismatch");
}
'''
def generate():
 cli=(ROOT/'workbench/c/DisplayScale/DisplayScale.c').read_text()
 cli='\n'.join(l for l in cli.splitlines() if not l.startswith('#include')).replace('int main(void)','static int display_scale_main(void)')
 snapshot=(ROOT/'workbench/c/DisplayScale/snapshot.h').read_text();snapshot=snapshot[snapshot.index('#define SCALE_SNAPSHOT_CAPACITY'):snapshot.index('/* Caller pins')]
 snapshot+='static BOOL CaptureScaleSnapshot(struct Screen*s,struct ScaleSnapshot*r){assert(0);return FALSE;}\n'
 editor=(ROOT/'workbench/prefs/screenmode/smeditor.c').read_text();settings=(ROOT/'workbench/prefs/screenmode/screensettings.h').read_text()
 methods='\n'.join(function(editor,n) for n in ['ScreenSettings_ScaleNotice','ScreenSettings_ScaleSync','ScreenSettings_ScaleFailure'])
 hook=editor[editor.index('AROS_UFH3(IPTR, ScaleChanged,'):editor.index('AROS_UFH3(IPTR, RasterChanged,')]
 methods+=hook+'\n'+function(editor,'SMEditor__MUIM_PrefsEditor_Cancel')+'\n'+function(editor,'SMEditor__MUIM_PrefsEditor_Revert')
 helpers='\n'.join(function(settings,n) for n in ['ScreenSettings_GetLiveScale','ScreenSettings_SetLiveScale'])
 # Contract comes before mocks that consume it; actual attempt code follows.
 prefix=PREFIX.replace('static BOOL GetAttr(',contract()+'\nstatic BOOL GetAttr(',1)
 return prefix+shared()+snapshot+cli+UI+helpers+methods+DRIVER

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=True)
 text=generate();(a.out/'actual.c').write_text(text);cmd=['clang','-std=c11','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(a.out/'actual.c'),'-o',str(a.out/'actual')]
 p=subprocess.run(cmd,capture_output=True,text=True);(a.out/'compile.log').write_text(p.stdout+p.stderr);assert not p.returncode,p.stderr
 p=subprocess.run([cmd[-1]],capture_output=True,text=True);(a.out/'run.log').write_text(p.stdout+p.stderr);assert not p.returncode,p.stdout+p.stderr;print(p.stdout,end='')
 rejected=[]
 mutations={
  'old_api_mutation':('attempt->api_available = base->lib_Version > 50 ||\n            (base->lib_Version == 50 && base->lib_Revision >= 20);','attempt->api_available = TRUE;'),
  'unreported_success':('(attempt->result.ssr_Status == SSR_SUCCESS ||\n         attempt->result.ssr_Status == SSR_UNCHANGED)','TRUE'),
  'stale_attempt':('memset(attempt, 0, sizeof(*attempt));','/* stale receipt */'),
  'save_refusal':('if (!ScaleAttemptSucceeded(&attempt))','if (FALSE)'),
  'unchecked_rollback':('if (ScaleAttemptSucceeded(&attempt))\n            Printf','if (TRUE)\n            Printf'),
  'requester_before_unlock':('if (screen) UnlockPubScreen(NULL, screen);','/* keep screen pinned */'),
  'gear_wanted_after_refusal':('NNSET(data->scale, MUIA_Numeric_Value, (attempt->actual - 100) / 25);','/* keep requested gear value */'),
  'cancel_claims_success':('ScreenSettings_ScaleFailure(self, data, "Restore display scale", &attempt);\n            return FALSE;','ScreenSettings_ScaleFailure(self, data, "Restore display scale", &attempt);\n            return TRUE;'),
  'invent_oom':('case SSR_REASON_PREPARE_FAILED: reason = "preparation was refused";','case SSR_REASON_PREPARE_FAILED: reason = "memory allocation failed";'),
  'caption_as_format':('0, title, "_OK", "%s", arguments);','0, title, "_OK", text, arguments);'),
  'unguarded_removed_page':('MUI_RequestA(muiGlobalInfo(editor) ? _app(editor) : NULL,\n                 muiRenderInfo(editor) ? _win(editor) : NULL,','MUI_RequestA(_app(editor),\n                 _win(editor),'),
  'render_guard_only':('muiGlobalInfo(editor) ? _app(editor) : NULL,','_app(editor),'),
  'c1_passthrough':('caption[i] = value < 32 || (value >= 127 && value <= 159)','caption[i] = value < 32 || value == 127'),
  'detail_ignored':('if (ScaleDetailFormat(result, detail, sizeof(detail))) reason = detail;','(void)detail;'),
  'detail_method_swapped':('case WDM_LAYOUTSCALEBORDERGADGETS: return "WDM_LAYOUTSCALEBORDERGADGETS";','case WDM_LAYOUTSCALEBORDERGADGETS: return "WDM_PREPARESCALEWINDOW";'),
  'detail_name_unbounded':('for (i = 0; i < size && name[i] && t->at + 1 < t->end; ++i)','for (i = 0; name[i] && t->at + 1 < t->end; ++i)'),
  'detail_name_c1':("*t->at++ = value < 32 || (value >= 127 && value <= 159) || value == '\\'' || value == '\"' ?","*t->at++ = value < 32 || value == '\\'' || value == '\"' ?"),
  'detail_negative_lost':("if (value < 0 && t->at + 1 < t->end) *t->at++ = '-';","(void)0;"),
  'detail_wrong_side':('static const char *const sides[] = {"left", "top", "right", "bottom"};','static const char *const sides[] = {"top", "left", "right", "bottom"};'),
  'requester_mid_gesture':('if (data->gesture_held)\n','if (FALSE)\n'),
  'release_drops_refusal':('ScreenSettings_ScaleNotice(editor, "Display scale", text);\n    }\n    return 0;','(void)text;\n    }\n    return 0;'),
  'stale_gesture_refusal':('    /* A later accepted step of the same gesture supersedes its refusal. */\n    data->pending_failure = FALSE;','    /* keep refusal */'),
  'mismatch_hides_applied':('Intuition reported result %lu with applied scale %lu%%; read-back scale: %s.",\n            (unsigned long)result->ssr_Status, (unsigned long)result->ssr_AppliedScale, read_back);','Intuition reported result %lu; read-back scale: %s.",\n            (unsigned long)result->ssr_Status, read_back);'),
  'layer_role_dropped':('    if (r->ssr_LayerRole == SSR_LAYER_NONE) return;\n','    return;\n'),
  'scanout_mask_decimal':('if (v[0] == HIDD_SCANOUT_MASKS) ScaleDetailHex(t, (ULONG)v[1]);','if (0) ScaleDetailHex(t, (ULONG)v[1]);'),
  'private_canvas_as_screen':('return (r->ssr_LayerFlags & LGRF_PRIVATE) ? "the private canvas bitmap" : "the screen bitmap";','return "the screen bitmap";'),
  'lease_driver_lost':('        ScaleDetailDriver(t, r->ssr_LayerDriver, v);\n','        (void)v;\n'),
  'layer_detail_ignored':('    case SSR_DETAIL_LAYER:\n        ScaleDetailLayer(&t, r);\n        break;\n','\n'),
  'no_window_identity':('snprintf(window, sizeof(window), "Window \\"%s\\" [%p]", caption, (void *)result->ssr_Window);','snprintf(window, sizeof(window), "Window %s", caption);'),
 }
 for name,(old,new) in mutations.items():
  assert old in text and old!=new,name
  path=a.out/(name+'.c');path.write_text(text.replace(old,new,1));command=cmd.copy();command[-3]=str(path);command[-1]=str(a.out/name)
  p=subprocess.run(command,capture_output=True,text=True);assert not p.returncode,p.stderr
  p=subprocess.run([command[-1]],capture_output=True,text=True);(a.out/(name+'.log')).write_text(p.stdout+p.stderr);assert p.returncode,'surviving '+name;rejected.append(name)
 print('Rejected consumer mutants:',','.join(rejected))
 (a.out/'result.json').write_text(json.dumps({'status':'PASS_BOUNDED','scope':__doc__,'command':cmd,'rejected_mutants':rejected,'sources':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/'compiler/include/intuition/screens.h',ROOT/'workbench/include/scaleresult.h',ROOT/'workbench/c/DisplayScale/DisplayScale.c',ROOT/'workbench/prefs/screenmode/screensettings.h',ROOT/'workbench/prefs/screenmode/smeditor.c',ROOT/'workbench/prefs/screenmode/smeditor_data.h',ROOT/'workbench/prefs/screenmode/scalegear.h',Path(__file__).resolve()]}},indent=2)+'\n')
if __name__=='__main__':main()
