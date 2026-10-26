#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Real window planner receipt coupled to the real CLI/Settings formatter.
Dependency lifetime/geometry adapters are inherited from windowfit_host; the
producer and consumer execute their production source, with no fabricated receipt.
"""
from pathlib import Path
import argparse, hashlib, json, re, tempfile
import windowfit_host as fixture
from scaleresult_fixture import detail_header as fixture_detail
ROOT=Path(__file__).resolve().parents[4]

def title_sources():
    """Actual OpenWindow title statements, int_setwindowtitles and windowtitle.h."""
    opener=(ROOT/'rom/intuition/openwindow.c').read_text(encoding='latin1')
    start=opener.index('    w->Title = nw.Title;\n');opener=opener[start:opener.index('\n\n',start)+1]
    setter=(ROOT/'rom/intuition/setwindowtitles.c').read_text(encoding='latin1')
    message=setter[setter.index('struct SetWindowTitlesActionMsg\n{'):]
    message=message[:message.index('};')+2]
    definition='static VOID int_setwindowtitles(struct SetWindowTitlesActionMsg *msg,\n                               struct IntuitionBase *IntuitionBase)\n{'
    assert setter.count(definition)==1
    setter=message+'\n'+setter[setter.index(definition):]
    titles=(ROOT/'rom/intuition/windowtitle.h').read_text()
    return opener,setter,titles
def title_support(opener,setter,titles):
    intern=(ROOT/'rom/intuition/intuition_intern.h').read_text(encoding='latin1')
    length=re.search(r'#define TITLEBUFFERLEN\s+(\d+)',intern).group(1)
    # Count every copy into a published window made without the reader's lock.
    titles=titles.replace('static inline void WindowTitleCopy(','static inline void WindowTitleCopyProduction(')
    return r"""
#define TITLEBUFFERLEN """+length+r"""
typedef char titlebuffer_matches_intwindow[sizeof(((struct Window*)0)->titlebuffer)==TITLEBUFFERLEN+1?1:-1];
typedef const UBYTE *CONST_STRPTR;
#define VOID void
#define LOCKWINDOWLAYERS(w) ;
#define UNLOCKWINDOWLAYERS(w) ;
#define REFRESHGAD_TOPBORDER 1
#define DOUBLEBUFFER 1
#define WFLG_WINDOWACTIVE 1
#define WFLG_TOOLBOX 2
struct IntuiActionMsg { int unused; };
static int framerefreshes, copies_unlocked;
static void int_RefreshWindowFrame(struct Window *w,int what,int pens,int mode,struct IntuitionBase *b){(void)w;(void)what;(void)pens;(void)mode;(void)b;framerefreshes++;}
static void RenderScreenTitle(struct Screen *s,struct IntuitionBase *b){(void)s;(void)b;}
static BOOL published(struct Window *w){for(struct Window *x=w->WScreen->FirstWindow;x;x=x->NextWindow)if(x==w)return TRUE;return FALSE;}
"""+titles+r"""
static void WindowTitleCopy(struct Window *w,CONST_STRPTR title){if(published(w)&&!refreshlocks)copies_unlocked++;WindowTitleCopyProduction(w,title);}
static void open_title(struct Window *w,UBYTE *title){struct {UBYTE *Title;} nw={title};
"""+opener+r"""}
"""+setter+r"""
static void SetWindowTitles(struct Window *window,const void *windowTitle,const void *screenTitle){
struct SetWindowTitlesActionMsg msg;msg.window=window;msg.windowTitle=windowTitle;msg.screenTitle=screenTitle;int_setwindowtitles(&msg,&base);}
"""
MAIN=r'''int main(void) {
 struct Window energy=makewin(0,0,400,1580),parent=makewin(0,0,1000,1500);
 char client[64]="Energy";
 /* Captions come only from the actual OpenWindow and SetWindowTitles writers. */
 open_title(&energy,(UBYTE*)client);open_title(&parent,(UBYTE*)"Parent");
 assert(energy.Title==(UBYTE*)client&&!strcmp(energy.titlebuffer,"Energy")&&!copies_unlocked);
 energy.MinHeight=1580;
 screen.FirstWindow=&energy;assert(WindowFitPrepareOpen(&energy,&base));
 /* A same-content call keeps the frame; edited client storage refreshes it. */
 SetWindowTitles(&energy,client,(CONST_STRPTR)~0);assert(!framerefreshes);
 strcpy(client,"Energz");SetWindowTitles(&energy,client,(CONST_STRPTR)~0);
 assert(framerefreshes==1&&!strcmp(energy.titlebuffer,"Energz"));
 SetWindowTitles(&energy,(CONST_STRPTR)~0,(CONST_STRPTR)~0);
 assert(framerefreshes==1&&energy.Title==(UBYTE*)client&&!strcmp(energy.titlebuffer,"Energz"));
 SetWindowTitles(&energy,NULL,(CONST_STRPTR)~0);assert(framerefreshes==2&&!energy.Title&&!energy.titlebuffer[0]);
 SetWindowTitles(&energy,"",(CONST_STRPTR)~0);assert(framerefreshes==2&&!energy.titlebuffer[0]);
 /* Latin-1 bytes compare as bytes whatever the signedness of plain char. */
 SetWindowTitles(&energy,"\xc9nergie",(CONST_STRPTR)~0);SetWindowTitles(&energy,"\xc9nergie",(CONST_STRPTR)~0);
 assert(framerefreshes==3&&!strcmp(energy.titlebuffer,"\xc9nergie"));
 char exact[TITLEBUFFERLEN+1],longer[TITLEBUFFERLEN+46];
 memset(exact,'E',TITLEBUFFERLEN);exact[TITLEBUFFERLEN]=0;memset(longer,'L',sizeof longer-1);longer[sizeof longer-1]=0;
 SetWindowTitles(&energy,exact,(CONST_STRPTR)~0);SetWindowTitles(&energy,exact,(CONST_STRPTR)~0);
 assert(framerefreshes==4&&!strcmp(energy.titlebuffer,exact));
 /* A truncated copy never claims a longer title is unchanged. */
 SetWindowTitles(&energy,longer,(CONST_STRPTR)~0);SetWindowTitles(&energy,longer,(CONST_STRPTR)~0);
 assert(framerefreshes==6&&strlen(energy.titlebuffer)==TITLEBUFFERLEN&&!strncmp(energy.titlebuffer,longer,TITLEBUFFERLEN));
 strcpy(client,"Energy");SetWindowTitles(&energy,client,(CONST_STRPTR)~0);
 assert(framerefreshes==7&&!copies_unlocked&&!refreshlocks);
 /* The refusal path must not dereference the borrowed client pointer. */
 energy.Title=(UBYTE*)(IPTR)1;strcpy(client,"Mutated");
 struct WindowFitTransaction *transaction=NULL;
 struct ScreenScaleResult result;memset(&result,0xa5,sizeof result);
 ScaleResultReset(&result,200,100);
 assert(!WindowFitPreflight(&screen,200,NULL,0,NULL,&transaction,&result,&base));
 assert(!transaction&&!allocations&&result.ssr_Status==SSR_FAILED);
 assert(result.ssr_Reason==SSR_REASON_GEOMETRY&&result.ssr_Stage==SSR_STAGE_GEOMETRY);
 assert(result.ssr_Domain==SSR_DOMAIN_LOGICAL&&result.ssr_Axis==SSR_AXIS_HEIGHT);
 assert(result.ssr_MinHeight==1580&&result.ssr_AvailableHeight==800&&result.ssr_MaxHeight==800);
 assert(result.ssr_Window==(IPTR)&energy&&!strcmp((char*)result.ssr_WindowTitle,"Energy"));
 struct ScaleAttempt attempt={.result=result,.requested=200,.actual=100,.screen_available=TRUE,.api_available=TRUE,.actual_known=TRUE};
 /* The receipt is a snapshot: a later title change does not reach it. */
 char text[768];SetWindowTitles(&energy,"Overwritten",(CONST_STRPTR)~0);
 ScaleAttemptFormat(&attempt,text,sizeof text);
 assert(strstr(text,"Energy")&&!strstr(text,"Overwritten")&&strstr(text,"1580")&&strstr(text,"800 logical")&&strstr(text,"100%"));puts(text);
 /* Parent recursion must not attribute a child's refusal to the parent. */
 screen.Scale=100;SetWindowTitles(&energy,"Child",(CONST_STRPTR)~0);
 energy.parent=&parent;energy.NextWindow=&parent;screen.FirstWindow=&energy;
 assert(WindowFitPrepareOpen(&parent,&base));
 ScaleResultReset(&result,200,100);
 assert(!WindowFitPreflight(&screen,200,NULL,0,NULL,&transaction,&result,&base));
 assert(result.ssr_Window==(IPTR)&energy&&!strcmp((char*)result.ssr_WindowTitle,"Child"));
 /* Malformed aware geometry after successful parent recursion retains the child. */
 energy.ScaleAware=TRUE;energy.ScaleGeometryEnabled=TRUE;
 struct WindowFitFrameMetrics frame={&energy,0,0,0,0};
 ScaleResultReset(&result,200,100);
 assert(!WindowFitPreflight(&screen,200,&frame,1,NULL,&transaction,&result,&base));
 assert(result.ssr_Reason==SSR_REASON_INVALID_CONTEXT&&result.ssr_Domain==SSR_DOMAIN_NONE&&result.ssr_Window==(IPTR)&energy);
 /* Missing proposed frames identify the frame/window rather than a stale outer stage. */
 ScaleResultReset(&result,200,100);
 assert(!WindowFitPreflight(&screen,200,NULL,0,NULL,&transaction,&result,&base));
 assert(result.ssr_Stage==SSR_STAGE_FRAME&&result.ssr_Window==(IPTR)&energy&&result.ssr_Reason==SSR_REASON_INVALID_CONTEXT);
 assert(result.ssr_Detail==SSR_DETAIL_FRAME_METRICS);
 {struct ScaleAttempt missing={.result=result,.requested=200,.actual=100,.screen_available=TRUE,.api_available=TRUE,.actual_known=TRUE};char line[768];ScaleAttemptFormat(&missing,line,sizeof line);puts(line);
  assert(strstr(line,"Window frame preparation for Window \"Child\"")&&strstr(line,": the window is missing from the measured frame set. Current scale: 100%."));}
 /* A layout window absent from the plans is a geometry refusal for that window;
  * the last measured frame window never stays attached to the receipt. */
 {struct Window stray=energy;stray.NextWindow=NULL;stray.parent=NULL;struct WindowLayoutFit fit={&stray,NULL,10,10};
  ScaleResultReset(&result,200,100);
  assert(!WindowFitPreflight(&screen,200,&frame,1,&fit,&transaction,&result,&base));
  assert(result.ssr_Stage==SSR_STAGE_GEOMETRY&&result.ssr_Window==(IPTR)&stray&&result.ssr_Reason==SSR_REASON_INVALID_CONTEXT&&result.ssr_Detail==SSR_DETAIL_NONE);}
 /* Direct allocation failure is distinguishable from an unspecified helper refusal. */
 failalloc=1;ScaleResultReset(&result,200,100);
 assert(!WindowFitPreflight(&screen,200,NULL,0,NULL,&transaction,&result,&base));
 assert(result.ssr_Reason==SSR_REASON_NO_MEMORY);failalloc=0;
 /* A reused receipt must clear prior identity/constraints/reason on success/noop. */
 ScaleResultFinish(&result,SSR_SUCCESS,200);
 assert(result.ssr_Status==SSR_SUCCESS&&result.ssr_Reason==SSR_REASON_NONE&&result.ssr_AppliedScale==200);
 assert(!result.ssr_Window&&!result.ssr_WindowTitle[0]&&!result.ssr_MinHeight&&!result.ssr_AvailableHeight);
 ScaleResultFinish(&result,SSR_UNCHANGED,200);assert(result.ssr_Status==SSR_UNCHANGED);
 SetWindowTitles(&energy,longer,(CONST_STRPTR)~0);ScaleResultSetStage(&result,SSR_STAGE_GEOMETRY,&energy);
 assert(result.ssr_WindowTitle[127]==0&&strlen((char*)result.ssr_WindowTitle)==127&&result.ssr_WindowTitle[0]=='L');
 assert(!allocations&&!copies_unlocked&&!refreshlocks);puts("PASS: actual OpenWindow/SetWindowTitles owned caption, producer/consumer refusal, child recursion, allocation reason and reused receipt");return 0;
}
'''
def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=Path)
    parser.add_argument('--consumer',type=Path,default=ROOT/'workbench/include/scaleresult.h')
    args=parser.parse_args()
    consumer=args.consumer.read_text()
    attempt=consumer[consumer.index('struct ScaleAttempt\n'):consumer.index('static inline BOOL ScaleAttemptSucceeded')]
    formatter=consumer[consumer.index('static inline const char *ScaleResultStage'):]
    formatter=fixture_detail(ROOT)+formatter[:formatter.rindex('#endif')]
    base_tests=fixture.TESTS[:fixture.TESTS.index('int main(void)')]
    def compose(open_title,set_titles,title_helpers):
        fixture.TESTS=base_tests+title_support(open_title,set_titles,title_helpers)+attempt+formatter+MAIN
    sources=title_sources()
    compose(*sources)

    body='struct FitGeometry'+fixture.SOURCE.read_text().split('struct FitGeometry',1)[1]
    import contextlib
    with (contextlib.nullcontext(str(args.output.resolve())) if args.output else tempfile.TemporaryDirectory(prefix='aros-result-')) as directory:
        output=Path(directory);output.mkdir(parents=True,exist_ok=True)
        result=fixture.run(body,output,'production')
        print(result.stdout,end='');assert result.returncode==0,result.stderr
        mutants={
          'wrong-axis':body.replace('SSR_AXIS_HEIGHT','SSR_AXIS_WIDTH'),
          'physical-legacy':body.replace('SSR_DOMAIN_LOGICAL','SSR_DOMAIN_PHYSICAL'),
          'wrong-parent-identity':body.replace('    /* Parent planning may have published its own diagnostic snapshot. Restore\n     * this child before any child-specific validation can refuse the plan. */\n    ScaleResultSetStage(diagnostic, SSR_STAGE_GEOMETRY, window);','')
        }
        # Deliberately corrupt the actual reported minimum, not a second model.
        mutants['stale-frame-stage']=body.replace('    ScaleResultSetStage(diagnostic, SSR_STAGE_GEOMETRY, NULL);\n    if (layout && find_plan(transaction, layout->window) < 0)\n    {\n        ScaleResultSetStage(diagnostic, SSR_STAGE_GEOMETRY, layout->window);\n','    if (layout && find_plan(transaction, layout->window) < 0)\n    {\n')
        mutants['metrics-detail-lost']=body.replace('ScaleResultDetail(diagnostic, SSR_DETAIL_FRAME_METRICS, 0, 0, 0, NULL);','(void)0;')
        mutants['wrong-minimum']=body.replace('SSR_AXIS_HEIGHT,\n            minWidth, minHeight,','SSR_AXIS_HEIGHT,\n            minWidth, minHeight + 1,')
        checked=[]
        for name,changed in mutants.items():
            if changed==body:continue
            result=fixture.run(changed,output,name);assert result.returncode!=0,name;checked.append(name)
        opener,setter,titles=sources
        title_mutants={
          'open-writer-removed':(opener.replace('    WindowTitleCopy(w, nw.Title);','    (void)0;'),setter,titles),
          'set-writer-removed':(opener,setter.replace('        WindowTitleCopy(window, windowTitle);\n',''),titles),
          'set-writer-unlocked':(opener,setter.replace('        LOCK_REFRESH(window->WScreen);\n','        (void)LayersBase;\n').replace('        UNLOCK_REFRESH(window->WScreen);\n',''),titles),
          'compare-truncated-equal':(opener,setter,titles.replace('for (i = 0; i <= TITLEBUFFERLEN; ++i)','for (i = 0; i < TITLEBUFFERLEN; ++i)').replace('    return FALSE;\n}\nstatic inline void','    return TRUE;\n}\nstatic inline void')),
          'compare-plain-char':(opener,setter,titles.replace('(UBYTE)IW(window)->titlebuffer[i]','IW(window)->titlebuffer[i]')),
          'copy-unbounded':(opener,setter,titles.replace('i < TITLEBUFFERLEN && title[i]','title[i]')),
        }
        for name,changed in title_mutants.items():
            assert changed!=sources,name
            compose(*changed);result=fixture.run(body,output,name);assert result.returncode!=0,name;checked.append(name)
        compose(*sources)
        # The producer must read only the owned copy, never the borrowed title.
        production_helpers=fixture.result_helpers
        fixture.result_helpers=lambda root:production_helpers(root).replace('IW(window)->titlebuffer[i]; ++i)\n        result->ssr_WindowTitle[i] = IW(window)->titlebuffer[i];','window->Title[i]; ++i)\n        result->ssr_WindowTitle[i] = window->Title[i];')
        assert fixture.result_helpers(ROOT)!=production_helpers(ROOT)
        result=fixture.run(body,output,'producer-reads-client-title');assert result.returncode!=0;checked.append('producer-reads-client-title')
        fixture.result_helpers=production_helpers
        (output/'result.json').write_text(json.dumps({'production':'PASS','rejected_mutants':checked,'consumer':str(args.consumer),'consumer_sha256':hashlib.sha256(args.consumer.read_bytes()).hexdigest()},indent=2)+'\n')
if __name__=='__main__':main()
