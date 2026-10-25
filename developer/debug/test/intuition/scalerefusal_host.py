#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Check the native probe's strict verdict/cleanup using bounded API responses.
This validates the client oracle, not the scale engine or native hardware.
The fake OpenWindow and SetWindowTitles keep an owned title copy the way
Intuition does; refusal captions come only from that copy, never from a
caption prepopulated by the response adapter.
"""
import argparse,json,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
PRELUDE=r'''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t ULONG;typedef int32_t LONG;typedef uint16_t UWORD;typedef uint8_t UBYTE;typedef uintptr_t IPTR;typedef int BOOL;typedef void Object;
#define TRUE 1
#define FALSE 0
#define RETURN_OK 0
#define RETURN_FAIL 20
#define CHECK(x) do{if(!(x)){fprintf(stderr,"CHECK %d %s\n",__LINE__,#x);abort();}}while(0)
#define SA_Dummy 1000
#define SA_Scale 1100
#define SA_LikeWorkbench 1
#define SA_Type 2
#define SA_Quiet 3
#define SA_Title 4
#define CUSTOMSCREEN 5
#define TAG_DONE 0
#define WA_CustomScreen 1
#define WA_Title 2
#define WA_Left 3
#define WA_Top 4
#define WA_Width 5
#define WA_Height 6
#define WA_MinWidth 7
#define WA_MinHeight 8
#define WA_MaxWidth 9
#define WA_MaxHeight 10
#define WA_Flags 11
#define WFLG_SMART_REFRESH 1
#define WFLG_BORDERLESS 2
#define WFLG_DRAGBAR 4
#define WFLG_DEPTHGADGET 8
typedef const char *CONST_STRPTR;
#define TITLEBUFFERLEN 255
struct Library {UWORD lib_Version,lib_Revision;};struct IntuitionBase {struct Library LibNode;};
struct Screen {LONG Width,Height;ULONG scale;};struct Window {LONG MinWidth,MinHeight,MaxWidth,MaxHeight,BorderLeft,BorderTop,BorderRight,BorderBottom;ULONG Flags;UBYTE *Title;UBYTE titlebuffer[TITLEBUFFERLEN+1];};
struct TagItem {IPTR ti_Tag,ti_Data;};
static struct IntuitionBase base={{50,20}},*IntuitionBase=&base;
static struct Screen screen_fixture;static struct Window window_fixture;
static ULONG mode,screen_live,window_live,opens,attempts,lowers,seen_orders,flushes,retitles;
static void owned_title(struct Window *window,const char *title){ULONG i=0;if(title)for(;i<TITLEBUFFERLEN&&title[i];++i)window->titlebuffer[i]=title[i];window->titlebuffer[i]=0;}
static int fake_flush(FILE *stream){CHECK(stream==stdout);++flushes;return (mode==18||(mode==19&&flushes==6))?EOF:0;}
#define fflush(stream) fake_flush(stream)
static IPTR tag(struct TagItem *tags,IPTR wanted){for(;tags->ti_Tag;tags++)if(tags->ti_Tag==wanted)return tags->ti_Data;return 0;}
#define OpenScreenTags(unused,...) fake_screen((struct TagItem[]){__VA_ARGS__})
#define OpenWindowTags(unused,...) fake_window((struct TagItem[]){__VA_ARGS__})
static struct Screen *fake_screen(struct TagItem *tags){CHECK(!screen_live&&!window_live&&base.LibNode.lib_Revision>=20);CHECK(tag(tags,SA_LikeWorkbench)&&tag(tags,SA_Type)==CUSTOMSCREEN&&tag(tags,SA_Scale)==100);++opens;screen_live=1;screen_fixture=(struct Screen){2560,1600,100};return &screen_fixture;}
static struct Window *fake_window(struct TagItem *tags){CHECK(screen_live&&!window_live&&tag(tags,WA_CustomScreen)==(IPTR)&screen_fixture);if(mode==17)return NULL;ULONG flags=tag(tags,WA_Flags);CHECK(!(flags&WFLG_BORDERLESS)&&(flags&WFLG_DRAGBAR));window_live=1;window_fixture=(struct Window){tag(tags,WA_MinWidth),tag(tags,WA_MinHeight),2560,1600,4,22,18,2,flags,(UBYTE*)tag(tags,WA_Title)};
 if(mode==24){window_fixture.Flags|=WFLG_BORDERLESS;window_fixture.BorderLeft=window_fixture.BorderTop=window_fixture.BorderRight=window_fixture.BorderBottom=0;}
 owned_title(&window_fixture,(char*)window_fixture.Title);CHECK(window_fixture.MinHeight==801&&tag(tags,WA_Height)==801);CHECK(!strcmp((char*)tag(tags,WA_Title),"ScaleRefusalLegacy"));return &window_fixture;}
static void SetWindowTitles(struct Window *window,CONST_STRPTR title,CONST_STRPTR screen){CHECK(window==&window_fixture&&window_live&&screen==(CONST_STRPTR)~0&&title);++retitles;if(mode==25)return;window->Title=(UBYTE*)title;if(mode!=21)owned_title(window,title);}
static void CloseWindow(struct Window *window){CHECK(window==&window_fixture&&window_live&&screen_live);window_live=0;}
static BOOL CloseScreen(struct Screen *screen){CHECK(screen==&screen_fixture&&screen_live&&!window_live);screen_live=0;return mode!=13;}
static BOOL GetAttr(ULONG attr,Object *screen,IPTR *result){CHECK(attr==SA_Scale&&screen==(Object*)&screen_fixture&&screen_live);if(mode==15&&attempts)return FALSE;*result=screen_fixture.scale;return TRUE;}
static BOOL WindowLimits(struct Window *window,LONG w,LONG h,UWORD mw,UWORD mh){CHECK(window==&window_fixture&&window_live&&w==1&&h==32&&mw==65535&&mh==65535);++lowers;if(mode==14)return FALSE;window->MinWidth=w;window->MinHeight=h;return TRUE;}
'''
RESPONSES=r'''
static void SetAttrsA(Object *object,struct TagItem *tags){
 CHECK(object==(Object*)&screen_fixture&&screen_live&&window_live);ULONG wanted=tag(tags,SA_Scale);struct ScreenScaleResult *result=(void*)tag(tags,SA_ScaleResult);CHECK(result);++attempts;
 seen_orders|=tags[0].ti_Tag==SA_ScaleResult?1:2;
 if(mode==2||(mode==9&&attempts==2&&tags[0].ti_Tag==SA_Scale))return;
 BOOL refuse=window_fixture.MinHeight>screen_fixture.Height/2&&wanted==200;
 if(refuse){memset(result,0,sizeof *result);result->ssr_Status=SSR_FAILED;result->ssr_Reason=SSR_REASON_GEOMETRY;result->ssr_Stage=SSR_STAGE_GEOMETRY;result->ssr_Domain=mode==4?SSR_DOMAIN_PHYSICAL:SSR_DOMAIN_LOGICAL;result->ssr_Axis=mode==5?SSR_AXIS_WIDTH:SSR_AXIS_HEIGHT;result->ssr_Window=mode==3?0:(IPTR)&window_fixture;result->ssr_RequestedScale=wanted;result->ssr_AppliedScale=mode==8?200:100;LONG fw=window_fixture.BorderLeft+window_fixture.BorderRight+1,fh=window_fixture.BorderTop+window_fixture.BorderBottom+1;result->ssr_MinWidth=window_fixture.MinWidth<fw?fw:window_fixture.MinWidth;result->ssr_MinHeight=(window_fixture.MinHeight<fh?fh:window_fixture.MinHeight)-(mode==6);result->ssr_MaxWidth=1280;result->ssr_MaxHeight=800;result->ssr_AvailableWidth=1280;result->ssr_AvailableHeight=mode==7?799:800;ULONG i;for(i=0;i+1<sizeof result->ssr_WindowTitle&&window_fixture.titlebuffer[i];++i)result->ssr_WindowTitle[i]=window_fixture.titlebuffer[i];result->ssr_WindowTitle[i]=0;
 if(mode==20)strcpy((char*)result->ssr_WindowTitle,"ScaleRefusalLegacy");if(mode==22&&i)result->ssr_WindowTitle[i-1]=0;if(mode==23)result->ssr_WindowTitle[0]=0;if(mode==16)memset(result->ssr_WindowTitle,'X',128);return;}
 ULONG status=screen_fixture.scale==wanted?SSR_UNCHANGED:SSR_SUCCESS;
 IPTR staleWindow=result->ssr_Window;UBYTE staleTitle[128];memcpy(staleTitle,result->ssr_WindowTitle,sizeof staleTitle);
 memset(result,0,sizeof *result);if(mode==10){result->ssr_Window=staleWindow;memcpy(result->ssr_WindowTitle,staleTitle,sizeof staleTitle);}
 result->ssr_Status=mode==11&&status==SSR_UNCHANGED?SSR_SUCCESS:status;result->ssr_Reason=SSR_REASON_NONE;result->ssr_Stage=SSR_STAGE_NONE;result->ssr_RequestedScale=wanted;result->ssr_AppliedScale=wanted;
 if(!(mode==12&&wanted==100))screen_fixture.scale=wanted;
}
'''
DRIVER=r'''
int main(int argc,char **argv){CHECK(argc==2);mode=strtoul(argv[1],NULL,10);base.LibNode.lib_Revision=mode==1?19:20;int rc=probe_main();CHECK(!screen_live&&!window_live);
 if(mode==0)CHECK(rc==0&&attempts==5&&lowers==1&&seen_orders==3&&retitles==1);
 else CHECK(rc!=0);
 if(mode==1)CHECK(!opens&&!attempts);
 printf("PROBE-ORACLE mode=%u client_rc=%d adapter=PASS\n",mode,rc);return 0;}
'''
MODES=26
# Expected final failure reason per adapter mode; 18/19 are stdout flush failures.
REASONS={1:'unsupported-intuition',2:'refusal-result-first',3:'refusal-result-first',4:'refusal-result-first',5:'refusal-result-first',6:'refusal-result-first',7:'refusal-result-first',8:'refusal-result-first',9:'refusal-scale-first',10:'success-after-refusal',11:'unchanged-clears-refusal',12:'reset-clears-refusal',13:'close-private-screen',14:'lower-minimum',15:'refusal-result-first',16:'refusal-result-first',17:'private-window',20:'refusal-scale-first',21:'refusal-scale-first',22:'refusal-result-first',23:'refusal-result-first',24:'titled-window',25:'refusal-scale-first'}
PHASES=['refuse-result-first','refuse-scale-first','success-after-refusal','unchanged-clears-refusal','reset-clears-refusal']
def check_stdout(mode,stdout):
 lines=stdout.splitlines()
 assert lines and lines[-1].startswith('PROBE-ORACLE mode=%d '%mode),lines[-1:]
 lines=lines[:-1]
 if mode in (18,19):return
 if mode==0:
  assert len(lines)==6,lines
  for i,(line,phase) in enumerate(zip(lines,PHASES)):
   assert line.startswith('[SCALE-REFUSAL] phase=%s '%phase)and line.endswith(' result=CHECK'),line
   caption=['ScaleRefusalLegacy','ScaleRefusalRetitled'][i] if i<2 else ''
   assert ' title=%s readback='%caption in line,line
  assert lines[-1]=='[SCALE-REFUSAL] result=PASS checks=5 reason=none',lines[-1]
  return
 assert lines and lines[-1].startswith('[SCALE-REFUSAL] result=FAIL ')and lines[-1].endswith(' reason='+REASONS[mode]),(mode,lines[-1:])
 assert all(l.startswith('[SCALE-REFUSAL] ') for l in lines),lines
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path);args=ap.parse_args()
 source=(ROOT/'developer/debug/test/intuition/scalerefusal.c').read_text();header=(ROOT/'compiler/include/intuition/screens.h').read_text(encoding='latin1');api=header[header.index('#define SA_ScaleResult'):header.index('#define SA_SCALE_MIN')]
 actual=PRELUDE+api+RESPONSES+source[source.index('static const char title[]'):].replace('int main(void)','static int probe_main(void)',1)+DRIVER
 with tempfile.TemporaryDirectory(prefix='scalerefusal-') as tmp:
  out=args.out or Path(tmp);out.mkdir(parents=True,exist_ok=True);p=out/'actual.c';p.write_text(actual);cmd=['clang','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(p),'-o',str(out/'actual')]
  r=subprocess.run(cmd,capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr)
  if r.returncode:raise SystemExit(r.stderr)
  for mode in range(MODES):
   r=subprocess.run([cmd[-1],str(mode)],capture_output=True,text=True);(out/(str(mode)+'.log')).write_text(r.stdout+r.stderr)
   if r.returncode:raise SystemExit(r.stdout+r.stderr)
   check_stdout(mode,r.stdout)
   print('PASS actual probe oracle mode',mode)
  mutants={'ignore_caption':('!strcmp((const char *)result->ssr_WindowTitle, (const char *)window->Title)','TRUE'),'caption_from_constant':('(const char *)window->Title);','title);'),'skip_retitle':('SetWindowTitles(window, (CONST_STRPTR)retitle, (CONST_STRPTR)~0);',''),'ignore_title_pointer':('window->Title == (UBYTE *)caption &&',''),'accept_borderless':('if ((window->Flags & WFLG_BORDERLESS) || window->Title != (UBYTE *)title ||\n        window->BorderTop <= 0) goto done;',''),'ignore_geometry':('result->ssr_MinHeight == minimumheight','TRUE'),'single_tag_order':('attempt(screen, 200, FALSE, &result, "refuse-scale-first"','attempt(screen, 200, TRUE, &result, "refuse-scale-first"'),'stale_success':('!result->ssr_Window && !result->ssr_WindowTitle[0]','TRUE'),'skip_reset_readback':('&& actual == wanted;',';'),'wrong_getattr_storage':('GetAttr(SA_Scale, (Object *)screen, &observed)','GetAttr(SA_Scale, (Object *)screen, (IPTR *)actual)')}
  rejected=[]
  for name,(old,new) in mutants.items():
   if old not in actual:raise RuntimeError(name)
   p=out/(name+'.c');p.write_text(actual.replace(old,new,1));m=cmd.copy();m[-3]=str(p);m[-1]=str(out/name);r=subprocess.run(m,capture_output=True,text=True)
   if r.returncode:raise SystemExit(r.stderr)
   caught=False;logs=[]
   for mode in range(MODES):
    r=subprocess.run([m[-1],str(mode)],capture_output=True,text=True);logs.append(r.stdout+r.stderr)
    if r.returncode:caught=True;break
    try:check_stdout(mode,r.stdout)
    except AssertionError as e:logs.append(str(e));caught=True;break
   (out/(name+'.log')).write_text('\n'.join(logs))
   if not caught:raise RuntimeError('survived '+name)
   rejected.append(name)
  (out/'result.json').write_text(json.dumps({'status':'PASS','scope':'actual native probe oracle with bounded API responses; engine/runtime not qualified','cases':MODES,'rejected_mutants':rejected},indent=2)+'\n')
if __name__=='__main__':main()
