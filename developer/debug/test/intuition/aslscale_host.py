#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License 1.1.
"""Compile actual ASL layout/scale/generation bodies with deterministic OS adapters.

Uses real public/private AROS headers. Adapters model allocation, font metrics,
BOOPSI storage and Process event delivery; this is not an AROS or raster run.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]
SRC = ROOT / "workbench/libs/asl"
SDK = Path(os.environ.get("ASL_SCALE_SDK", ROOT.parent / "Build/display-composition-native"))

def body(file, name):
    text = (SRC / file).read_text()
    match = re.search(r"(?:STATIC |static )?(?:BOOL|ULONG|UWORD|VOID|void|Object \*|LONG|IPTR|STRPTR)\s*" + re.escape(name) + r"\([^;]*?\)\s*\{", text)
    if not match:
        raise RuntimeError(f"production function missing: {file}:{name}")
    end = match.end()
    depth = 1
    while depth:
        if text[end] == "{": depth += 1
        elif text[end] == "}": depth -= 1
        end += 1
    return text[match.start():end]

def native_body(file, name):
    text = (ROOT / 'rom/intuition' / file).read_text()
    start = text.index('\n{\n    AROS_LIBFUNC_INIT') + 1
    end = text.index('} /* ' + name + ' */', start) + 1
    return text[start:end]

PRELUDE = r"""
#include "asl_intern.h"
#include "propunits.h"
#include "filereqhooks.h"
#include "fontreqhooks.h"
#include "modereqhooks.h"
#include "filereqsupport.h"
#include "fontreqsupport.h"
#include "modereqsupport.h"
#include <intuition/imageclass.h>
#include <intuition/extensions.h>
#include <intuition/icclass.h>
#include <intuition/cghooks.h>
#include <libraries/coolimages.h>
#include <exec/memory.h>
#include <exec/interrupts.h>
#include <devices/rawkeycodes.h>
#include <dos/dosextens.h>
#include <workbench/startup.h>
#include <string.h>
extern void *malloc(__SIZE_TYPE__);
extern void free(void *);
extern void abort(void);
extern int printf(const char *, ...);
#define STATIC static
#define G(x) ((struct Gadget *)(x))
#define EG(x) ((struct ExtGadget *)(x))
#define StringEditFunc FakeStringEditFunc
#define ReturnBool(name,value) return (value)
#define D(x)
#define EnterFunc(x)
#define CHECK(x) do { if (!(x)) { printf("CHECK line %d: %s\n",__LINE__,#x); abort(); } } while (0)
#define NEWLIST(l) do { (l)->lh_Head=(struct Node *)&(l)->lh_Tail; (l)->lh_Tail=0; (l)->lh_TailPred=(struct Node *)&(l)->lh_Head; } while (0)
static BOOL install_lock,inject_edit,late_event,late_text,modal_test;
static ULONG modal_phase,modal_activations,modal_outcome,modal_fail_point,modal_alloc_start,modal_alloc_points,modal_publications;
static char modal_final_text[256];
static ULONG modal_flip_mode,modal_flip_at;
static BOOL modal_preparing,modal_prepare_seen,modal_fault_armed;
static ULONG modal_initial_flip,modal_initial_fail,modal_prepare_semantic,modal_request_start,modal_initial_points;
static BOOL modal_parent_fail;
static VOID EmitPrepareSemantic(void);
struct StringModal;
static BOOL RelayoutStringModal(struct StringModal *,struct AslBase_intern *);
static const struct ScreenRenderContext *modal_flip_target;
static VOID ModalFlipContext(void);
static VOID RememberModalGeometry(const struct ScaleWindowGeometry *,ULONG);
static VOID ScaleModalNative(const struct ScreenRenderContext *);
static VOID NativeScale(struct LayoutData *,const struct ScreenRenderContext *);
static struct Window *modal_window;
static const struct ScreenRenderContext *event_target;
static struct Gadget *modal_identity;
static struct StringInfo *modal_info;static APTR modal_buffer,modal_undo,modal_work;
static ULONG raw_seen;
static VOID EmitEvent(ULONG,ULONG);
static VOID InjectKey(UBYTE letter);
static ULONG alloc_count, fail_at, live_alloc, visual_count, objects_created, objects_disposed;
static struct Screen screens[2];
static const struct ScreenRenderContext *current[2];
static struct AslBase_intern base;
static Class classes[12];
static APTR CoolImagesBase;
static LONG ioerr;
static ULONG semaphore_depth, shown_count, closed_count, stale_show;
static struct Process self;
static struct MsgPort eventport, appport;
struct FakeWindow {struct Window w;ULONG specialflags;struct RastPort rp;const struct ScreenRenderContext *expected;BOOL painted;};
static VOID NoLock(void) {CHECK(semaphore_depth==0);}
static VOID ObtainSemaphore(struct SignalSemaphore *s) {(void)s;++semaphore_depth;}
static VOID ReleaseSemaphore(struct SignalSemaphore *s) {(void)s;CHECK(semaphore_depth);--semaphore_depth;}
static APTR FindTask(CONST_STRPTR name) {(void)name;return &self;}
static BOOL NativeModifyIDCMP(struct Window *,ULONG);
static VOID NativeCloseWindow(struct Window *);
static VOID CheckNativePortPolicies(void);
static ULONG native_port_creates,native_port_deletes,native_modal_closes,native_late_replies;
static struct MsgPort *CreateMsgPort(void);
static VOID DeleteMsgPort(struct MsgPort *);
static BOOL IsLivePort(struct MsgPort *);
static BOOL ModifyIDCMP(struct Window *w,ULONG flags) {NoLock();return NativeModifyIDCMP(w,flags);}
static struct Window *OpenWindowTagList(struct NewWindow *nw,struct TagItem *tags);
static BOOL ShowWindow(struct Window *w,struct Window *other);
static VOID CloseWindow(struct Window *w);
static VOID ActivateWindow(struct Window *w) {CHECK(modal_test&&w==modal_window);++modal_activations;}
static BOOL ActivateGadget(struct Gadget *,struct Window *,struct Requester *);
static UWORD RemoveGadget(struct Window *w,struct Gadget *g) {(void)w;(void)g;return 0;}
#define OpenWindowTags(nw,...) OpenWindowTagList(nw,(struct TagItem[]){__VA_ARGS__})
static struct AppWindow *AddAppWindow(ULONG id,IPTR user,struct Window *w,struct MsgPort *p,struct TagItem *t);
static VOID RemoveAppWindow(struct AppWindow *a);
static VOID ClearMenuStrip(struct Window *w) {(void)w;NoLock();}
static BOOL SetMenuStripContext(struct Window *w,struct Menu *m,const struct ScreenRenderContext *c,BOOL logical) {(void)w;(void)m;(void)logical;CHECK(c);NoLock();return TRUE;}
static VOID CloseFont(struct TextFont *f);

struct FakeObject { struct Gadget g; struct TagItem tags[80]; union { struct AslListViewData lv; struct AslFontPreviewData fp; struct AslButtonData button; struct AslStringData string; struct AslPropData prop; struct AslCycleData cycle; struct AslFontStyleData style; struct AslColorPickerData color; struct AslFrameData frame; } data; struct StringInfo info; struct PropInfo propinfo; struct StringExtend extend; char undo[256],work[256]; char *text; Class *cl; };
static APTR AllocVec(ULONG n, ULONG flags) { CHECK(!install_lock);if(inject_edit){inject_edit=FALSE;InjectKey('x');} if(modal_test&&modal_preparing&&modal_phase==1&&modal_prepare_semantic)EmitPrepareSemantic();++alloc_count;if(modal_test&&modal_preparing&&modal_phase==1&&modal_flip_mode==1){modal_flip_mode=0;ModalFlipContext();} if (fail_at && alloc_count==fail_at) return NULL; void *p=malloc(n?n:1); CHECK(p); if(flags & MEMF_CLEAR) memset(p,0,n); ++live_alloc; return p; }
static VOID FreeVec(APTR p) { CHECK(!install_lock);if(p) { CHECK(live_alloc); --live_alloc; free(p); } }
static VOID CopyMem(const void *a, void *b, ULONG n) { memcpy(b,a,n); }
static struct TagItem *NextTagItem(struct TagItem **p) { while(*p) { struct TagItem *t=(*p)++; if(t->ti_Tag==TAG_DONE) return NULL; if(t->ti_Tag==TAG_IGNORE) continue; if(t->ti_Tag==TAG_MORE) { *p=(APTR)t->ti_Data; continue; } if(t->ti_Tag==TAG_SKIP) { *p+=t->ti_Data; continue; } return t; } return NULL; }
static struct TagItem *FindTagItem(Tag id, struct TagItem *p) { struct TagItem *t; while((t=NextTagItem(&p))) if(t->ti_Tag==id) return t; return NULL; }
static IPTR GetTagData(Tag id, IPTR def, struct TagItem *p) { struct TagItem *t=FindTagItem(id,p); return t?t->ti_Data:def; }
#undef OCLASS
#define OCLASS(o) (((struct FakeObject *)(o))->cl)
static BOOL GetAttr(ULONG tag, Object *object, IPTR *value) {
 if((APTR)object==&screens[0] || (APTR)object==&screens[1]) { *value=(IPTR)current[(APTR)object==&screens[1]]; return tag==SA_RenderContext; }
 struct FakeObject *o=(APTR)object; struct TagItem *t=FindTagItem(tag,o->tags);
 if(tag==STRINGA_TextVal && o->cl==base.aslstringclass) { *value=(IPTR)o->info.Buffer; return TRUE; }
 if(tag==ASLLV_Labels && o->cl==base.asllistviewclass) { *value=(IPTR)o->data.lv.labels; return TRUE; }
 if(t) { *value=t->ti_Data; return TRUE; } return FALSE;
}
static VOID DisposeObject(Object *object) { struct FakeObject *o=(APTR)object;if(modal_test&&object==(Object *)modal_identity&&o->text){CHECK(o->info.Extension->Font==event_target->src_PhysicalDrawInfo->dri_Font);strcpy(modal_final_text,o->text);} if(!o)return; FreeVec(o->text); ++objects_disposed; FreeVec(o); }
static Object *NewObjectA(Class *cl, STRPTR name, struct TagItem *tags) {
 struct FakeObject *o=AllocVec(sizeof(*o),MEMF_CLEAR); if(!o)return NULL; ++objects_created; o->cl=cl;
 struct TagItem *t,*scan=tags; ULONG n=0;
 while((t=NextTagItem(&scan))) { CHECK(n<79); o->tags[n++]=*t; }
 o->g.LeftEdge=GetTagData(GA_Left,0,tags); o->g.TopEdge=GetTagData(GA_Top,0,tags); o->g.Width=GetTagData(GA_Width,0,tags); o->g.Height=GetTagData(GA_Height,0,tags);
 if((t=FindTagItem(GA_RelRight,tags))) {o->g.LeftEdge=t->ti_Data;o->g.Flags|=GFLG_RELRIGHT;}
 if((t=FindTagItem(GA_RelBottom,tags))) {o->g.TopEdge=t->ti_Data;o->g.Flags|=GFLG_RELBOTTOM;}
 if((t=FindTagItem(GA_RelWidth,tags))) {o->g.Width=t->ti_Data;o->g.Flags|=GFLG_RELWIDTH;}
 if((t=FindTagItem(GA_RelHeight,tags))) {o->g.Height=t->ti_Data;o->g.Flags|=GFLG_RELHEIGHT;}
 o->g.GadgetID=GetTagData(GA_ID,0,tags); o->g.UserData=(APTR)GetTagData(GA_UserData,0,tags);
 struct Gadget *prev=(APTR)GetTagData(GA_Previous,0,tags); if(prev) prev->NextGadget=&o->g;
 if(cl==base.aslstringclass) { STRPTR s=(APTR)GetTagData(STRINGA_TextVal,(IPTR)"",tags); if(!s)s=(STRPTR)""; o->text=AllocVec(256,0); if(!o->text){DisposeObject((Object *)o);return NULL;} strcpy(o->text,(char *)s);o->g.SpecialInfo=&o->info;o->g.Flags|=GFLG_STRINGEXTEND|GFLG_SELECTED;o->g.GadgetType=GTYP_CUSTOMGADGET;o->info.Buffer=(UBYTE *)o->text;o->info.MaxChars=256;o->info.BufferPos=strlen(o->text);o->info.UndoBuffer=(UBYTE *)o->undo;o->info.Extension=&o->extend;o->extend.WorkBuffer=(STRPTR)o->work;o->extend.Font=((struct LayoutData *)o->g.UserData)->ld_Font;o->data.string.ld=o->g.UserData; }
 if(cl==base.asllistviewclass) {o->data.lv.ld=o->g.UserData;o->data.lv.itemheight=((struct LayoutData *)o->g.UserData)->ld_Font->tf_YSize;o->data.lv.spacing=AslScaleValue(o->g.UserData,BORDERLVITEMSPACINGY)*2;o->data.lv.lineheight=o->data.lv.itemheight+o->data.lv.spacing;NEWLIST(&o->data.lv.emptylist); o->data.lv.labels=(APTR)GetTagData(ASLLV_Labels,(IPTR)&o->data.lv.emptylist,tags); if(!o->data.lv.labels)o->data.lv.labels=&o->data.lv.emptylist; }
 if(cl==base.aslbuttonclass)o->data.button.ld=o->g.UserData;
 if(cl==base.aslpropclass){o->data.prop.ld=o->g.UserData;o->g.SpecialInfo=&o->propinfo;}
 if(cl==base.aslcycleclass)o->data.cycle.ld=o->g.UserData;
 if(cl==base.aslcolorpickerclass)o->data.color.ld=o->g.UserData;
 if(cl==base.aslfontstyleclass)o->data.style.ld=o->g.UserData;
 if(cl==base.aslfontpreviewclass)o->data.fp.ld=o->g.UserData;
 return (Object *)o;
}
static VOID SetAttrsA(Object *o, struct TagItem *tags) { struct FakeObject *f=(APTR)o; struct TagItem *t,*p=tags; while((t=NextTagItem(&p))) {if(t->ti_Tag==GA_Image)f->g.GadgetRender=(APTR)t->ti_Data; struct TagItem *a=FindTagItem(t->ti_Tag,f->tags); if(a)*a=*t; else {ULONG n=0;while(f->tags[n].ti_Tag)++n;CHECK(n<79);f->tags[n]=*t;f->tags[n+1].ti_Tag=TAG_DONE;} } }
#define SetAttrs(o,...) SetAttrsA((o),(struct TagItem[]) { __VA_ARGS__ })
static ULONG TextLength(struct RastPort *rp, CONST_STRPTR text, ULONG n) { (void)text; return n*rp->Font->tf_XSize; }
static VOID SetFont(struct RastPort *rp, struct TextFont *f) { rp->Font=f; rp->TxHeight=f->tf_YSize; rp->TxBaseline=f->tf_Baseline; rp->TxWidth=f->tf_XSize; }
static ULONG LockIBase(ULONG n) {(void)n;return 0;}
static VOID UnlockIBase(ULONG n) {(void)n;}
static VOID LockLayerInfo(struct Layer_Info *li) {(void)li;}
static VOID UnlockLayerInfo(struct Layer_Info *li) {(void)li;}
static APTR GetVisualInfoA(struct Screen *s, struct TagItem *t) { (void)s; CHECK(GetTagData(GTVI_RenderContext,0,t)); APTR v=AllocVec(1,0); if(v)++visual_count; return v; }
static VOID FreeVisualInfo(APTR v) { if(v){CHECK(visual_count);--visual_count;FreeVec(v);} }
static struct LayoutData *event_ld;
static const struct ScreenRenderContext *event_target;
static ULONG event_phase,event_mode,event_replies,event_waits,event_delays;
static struct MsgPort *modal_port;
static APTR WaitPort(struct MsgPort *p) {(void)p;return NULL;}
static struct IntuiMessage event_messages[4];
static ULONG Wait(ULONG mask);
static VOID Delay(ULONG ticks);
static ULONG SetSignal(ULONG a,ULONG b) {(void)a;return b;}
static VOID BeginRefresh(struct Window *w) {(void)w;}
static VOID EndRefresh(struct Window *w,BOOL complete) {(void)w;(void)complete;}
static VOID RefreshGadgets(struct Gadget *g,struct Window *w,struct Requester *r) {(void)g;(void)w;(void)r;}
#define DoASLIntuiMsgFunc(a,b,c) ((void)0)
#define DoASLHookFunc(a,b,c,d) ((void)0)
VOID FRDropFromDifferentDrawersRequester(struct LayoutData *ld,struct AslBase_intern *b) {(void)ld;(void)b;abort();}
VOID MyFreeVecPooled(APTR p,struct AslBase_intern *b) {(void)b;FreeVec(p);}
char *VecPooledCloneString(const char *s,const char *len,APTR pool,struct AslBase_intern *b) {(void)len;(void)pool;(void)b;ULONG n=strlen(s)+1;STRPTR p=AllocVec(n,0);if(p)CopyMem(s,p,n);return p;}
static VOID Forbid(void) {}
static VOID Permit(void) {}
static VOID Remove(struct Node *n) {n->ln_Pred->ln_Succ=n->ln_Succ;n->ln_Succ->ln_Pred=n->ln_Pred;}
static struct Node *RemHead(struct List *l) {struct Node *n=l->lh_Head;if(!n->ln_Succ)return NULL;Remove(n);return n;}
static VOID AddTail(struct List *l,struct Node *n) {n->ln_Succ=(struct Node *)&l->lh_Tail;n->ln_Pred=l->lh_TailPred;l->lh_TailPred->ln_Succ=n;l->lh_TailPred=n;}
static struct Message *GetMsg(struct MsgPort *p) {CHECK(p&&IsLivePort(p));return (struct Message *)RemHead(&p->mp_MsgList);}
static VOID ReplyMsg(struct Message *m) {for(ULONG i=0;i<4;i++)if(m==(struct Message *)&event_messages[i]){CHECK(!m->mn_Node.ln_Name);m->mn_Node.ln_Name=(STRPTR)1;++event_replies;}}
static VOID RemoveGList(struct Window *w, struct Gadget *g, LONG n) {(void)w;(void)g;(void)n;abort();}
static VOID AddGList(struct Window *w, struct Gadget *g, LONG a,LONG b,APTR r) {(void)a;(void)b;(void)r;w->FirstGadget=g;}
static VOID RefreshGList(struct Gadget *g,struct Window *w,APTR r,LONG n) {(void)g;(void)r;(void)n;((struct FakeWindow *)w)->painted=TRUE;if(modal_test&&w==modal_window&&modal_phase==1&&modal_flip_mode==2){modal_flip_mode=0;ModalFlipContext();}if(late_event){late_event=FALSE;EmitEvent(2,IDCMP_RAWKEY);event_messages[2].Code=RAWKEY_UP;}if(late_text){late_text=FALSE;InjectKey('z');}}
static BOOL FitWindowLayout(struct Window *,const struct ScreenRenderContext *,LONG,LONG);
static struct Window *OpenWindowTagList(struct NewWindow *nw,struct TagItem *t) {
 (void)nw;NoLock();const struct ScreenRenderContext *ctx=(APTR)GetTagData(WA_RenderContext,0,t);
 CHECK(GetTagData(WA_Hidden,FALSE,t)&&!GetTagData(WA_Activate,FALSE,t));
 if(!ctx||ctx!=current[0])return NULL;
 struct FakeWindow *f=AllocVec(sizeof(*f),MEMF_CLEAR);if(!f)return NULL;
 f->expected=ctx;f->w.WScreen=&screens[0];f->w.RPort=&f->rp;
 f->w.Width=GetTagData(WA_Width,GetTagData(WA_InnerWidth,2560,t),t);f->w.Height=GetTagData(WA_Height,GetTagData(WA_InnerHeight,1600,t),t);
 f->w.LeftEdge=GetTagData(WA_Left,0,t);f->w.TopEdge=GetTagData(WA_Top,0,t);
 if(f->w.LeftEdge+f->w.Width>screens[0].Width)f->w.LeftEdge=screens[0].Width-f->w.Width;
 if(f->w.TopEdge+f->w.Height>screens[0].Height)f->w.TopEdge=screens[0].Height-f->w.Height;
 if(f->w.LeftEdge<0)f->w.LeftEdge=0;if(f->w.TopEdge<0)f->w.TopEdge=0;
 f->w.BorderLeft=(4*ctx->src_Scale+50)/100;f->w.BorderRight=f->w.BorderLeft;
 f->w.BorderTop=ctx->src_PhysicalDrawInfo->dri_Font->tf_YSize+(3*ctx->src_Scale+50)/100;
 f->w.BorderBottom=(16*ctx->src_Scale+50)/100;
 if(modal_test) {CHECK(GetTagData(WA_ScaleAware,FALSE,t));CHECK(GetTagData(WA_IDCMP,0,t)&IDCMP_CHANGESCALE);const struct ScaleWindowGeometry *geometry=(APTR)GetTagData(WA_ScaleGeometry,0,t);CHECK(geometry);RememberModalGeometry(geometry,ctx->src_Scale);f->w.FirstGadget=(APTR)GetTagData(WA_Gadgets,0,t);CHECK(!f->specialflags);if(!ModifyIDCMP(&f->w,GetTagData(WA_IDCMP,0,t))){FreeVec(f);return NULL;}f->painted=TRUE;modal_window=&f->w;modal_port=f->w.UserPort;modal_initial_points=alloc_count-modal_request_start;if(modal_initial_fail){fail_at=alloc_count+modal_initial_fail;modal_initial_fail=0;}if(modal_initial_flip){modal_initial_flip=0;const struct ScreenRenderContext *saved=event_target;ModalFlipContext();event_target=saved;}}
 return &f->w;
}
static BOOL ShowWindow(struct Window *w,struct Window *other) {NoLock();CHECK(!other);struct FakeWindow *f=(APTR)w;CHECK(f->painted);if(stale_show||f->expected!=current[0])return FALSE;++shown_count;return TRUE;}
static VOID CloseWindow(struct Window *w) {NoLock();CHECK(w!=self.pr_WindowPtr);if(modal_test){struct MsgPort *saved=w->UserPort;NativeCloseWindow(w);CHECK(!IsLivePort(saved)&&event_messages[2].ExecMessage.mn_Node.ln_Name&&event_messages[2].IDCMPWindow==NULL&&event_messages[3].ExecMessage.mn_Node.ln_Name&&event_messages[3].IDCMPWindow==NULL);native_late_replies+=2;return;}for(ULONG i=0;i<4;i++)if(event_messages[i].IDCMPWindow==w)CHECK(event_messages[i].ExecMessage.mn_Node.ln_Name);++closed_count;FreeVec(w);}
static struct AppWindow *AddAppWindow(ULONG id,IPTR user,struct Window *w,struct MsgPort *p,struct TagItem *t) {(void)id;(void)user;(void)w;(void)p;(void)t;NoLock();return AllocVec(1,0);}
static VOID RemoveAppWindow(struct AppWindow *a) {NoLock();FreeVec(a);}
static VOID SetIoErr(LONG n) {ioerr=n;}
static char *GetStringMock(LONG id);
#define GetString(id,cat,lib) ((STRPTR)GetStringMock(id))
VOID LocalizeMenus(struct NewMenu *n,struct Catalog *c,struct AslBase_intern *b) {(void)n;(void)c;(void)b;}
static struct Menu *CreateMenusA(struct NewMenu *n,struct TagItem *t) {(void)n;(void)t;return AllocVec(sizeof(struct Menu),MEMF_CLEAR);}
static BOOL LayoutMenusA(struct Menu *m,APTR v,struct TagItem *t) {(void)m;(void)v;(void)t;return TRUE;}
static VOID FreeMenus(struct Menu *m) {FreeVec(m);}
static const struct CoolImage *COOL_ObtainImageA(ULONG id,struct TagItem *t) {(void)id;(void)t;return NULL;}
LONG FOGetFonts(struct LayoutData *ld,struct AslBase_intern *b) {(void)ld;(void)b;return 0;}
LONG SMGetModes(struct LayoutData *ld,struct AslBase_intern *b) {(void)ld;(void)b;return 0;}
VOID FORestore(struct LayoutData *ld,CONST_STRPTR n,LONG y,struct AslBase_intern *b) {(void)ld;(void)n;(void)y;(void)b;}
VOID SMRestore(struct LayoutData *ld,struct AslBase_intern *b) {(void)ld;(void)b;}
VOID FOUpdatePreview(struct LayoutData *ld,struct AslBase_intern *b) {(void)ld;(void)b;}
VOID SMClosePropertyWindow(struct LayoutData *ld,struct AslBase_intern *b) {(void)ld;(void)b;abort();}
VOID SMOpenPropertyWindow(struct LayoutData *ld,struct AslBase_intern *b) {(void)ld;(void)b;abort();}
static VOID FRGadCleanup(struct LayoutData *ld,struct AslBase_intern *b) {(void)ld;(void)b;abort();}
static VOID FOGadCleanup(struct LayoutData *ld,struct AslBase_intern *b) {(void)ld;(void)b;abort();}
static VOID SMGadCleanup(struct LayoutData *ld,struct AslBase_intern *b) {(void)ld;(void)b;abort();}
BOOL FRGetVolumes(struct LayoutData *ld,struct AslBase_intern *b) {(void)ld;(void)b;return TRUE;}
BOOL FRNewPath(STRPTR s,struct LayoutData *ld,struct AslBase_intern *b) {(void)s;(void)ld;(void)b;return TRUE;}
void FRChangeActiveLVItem(struct LayoutData *ld,LONG d,ULONG q,struct Gadget *g,struct AslBase_intern *b) {(void)ld;(void)d;(void)q;(void)g;(void)b;}
static VOID StringEditFunc(void) {}
static VOID ASLFRRenderHook(void) {}
static VOID SizeListviewRenderFunc(void) {}
static IPTR DoSuperMethodA(Class *cl,Object *o,Msg m) {(void)cl;(void)o;(void)m;return 0;}
static IPTR CallHookPkt(struct Hook *,APTR,APTR);

#include <devices/inputevent.h>
#include <prefs/icontrol.h>
#include "../../../rom/intuition/awaregeometry.h"
#define ReturnInt(name,type,value) return (value)
#define ReturnVoid(name) return
static struct {struct Library *UtilityBase,*KeymapBase;struct Hook *GlobalEditHook;struct IControlPrefs IControlPrefs;struct SignalSemaphore WindowLock;struct Interrupt *InputHandler;} fakeIBase;
#define GetPrivIBase(base) (&fakeIBase)
static struct Hook globalHook;
static struct Gadget *active;
static struct GadgetInfo activeGI;
static struct RastPort activeRP;
static struct TextFont previewSeed;
static struct TextFontExtension previewExtension;
static LONG MapRawKey(struct InputEvent *ie,UBYTE *out,LONG count,struct KeyMap *km) {(void)count;(void)km;*out=ie->ie_Code;return 1;}
static ULONG DoSGHKey(struct SGWork *,struct IntuitionBase *);
static ULONG HandleStrInput(struct Gadget *,struct GadgetInfo *,struct InputEvent *,UWORD *,struct IntuitionBase *);
static ULONG ToUpper(ULONG c) {return c>='a'&&c<='z'?c-'a'+'A':c;}
extern long atol(const char *);
static struct RastPort *ObtainGIRPort(struct GadgetInfo *gi) {CHECK(gi->gi_Window==self.pr_WindowPtr||(modal_test&&gi->gi_Window==modal_window));return gi->gi_RastPort;}
static VOID ReleaseGIRPort(struct RastPort *rp) {(void)rp;}
static IPTR Custom_DoMethodA(struct IntuitionBase *ib,struct Gadget *g,Msg m) {(void)ib;(void)m;CHECK(g==active);CHECK(g->Flags&GFLG_SELECTED);SetFont(activeGI.gi_RastPort,((struct StringInfo *)g->SpecialInfo)->Extension->Font);return 0;}
static VOID RefreshStrGadget(struct Gadget *g,struct Window *w,struct Requester *r,struct IntuitionBase *ib) {(void)g;(void)w;(void)r;(void)ib;abort();}
static VOID DoMethodA(Object *o,Msg m) {CHECK(install_lock);struct opSet *set=(APTR)m;CHECK(m->MethodID==OM_SET);SetAttrsA(o,set->ops_AttrList);}
static IPTR AslButton__GM_LAYOUT(Class *,struct Gadget *,struct gpLayout *);
static IPTR AslButton__ASLGM_INSTALLSCALE(Class *,Object *,Msg);
static ULONG DoGadgetMethodA(struct Gadget *g,struct Window *w,struct Requester *r,Msg m) {
 (void)r;struct GadgetInfo gi={.gi_Window=w,.gi_Screen=w->WScreen,.gi_Domain={0,0,w->Width,w->Height}};
 if(m->MethodID==ASLGM_INSTALLSCALE){struct AslInstallScale *s=(APTR)m;CHECK(!install_lock);s->ginfo=&gi;install_lock=TRUE;BOOL ok;if(OCLASS(g)==base.aslbuttonclass){CHECK(modal_test&&g==s->live->ld_GList&&w==modal_window&&s->candidate->ld_RenderContext==current[0]);ok=AslButton__ASLGM_INSTALLSCALE(OCLASS(g),(Object *)g,m);++modal_publications;}else ok=AslPublishScale(s->live,s->candidate,&gi,&base);install_lock=FALSE;return ok;}
 CHECK(m->MethodID==GM_LAYOUT);if(modal_test&&modal_preparing&&modal_phase==1&&modal_flip_mode==3){modal_flip_mode=0;ModalFlipContext();}if(OCLASS(g)==base.aslbuttonclass){((struct gpLayout *)m)->gpl_GInfo=&gi;AslButton__GM_LAYOUT(OCLASS(g),g,(APTR)m);}return 0;
}
static BOOL ActivateGadget(struct Gadget *g,struct Window *w,struct Requester *r) {(void)r;CHECK(modal_test&&w==modal_window);active=g;modal_identity=g;modal_info=g->SpecialInfo;modal_buffer=modal_info->Buffer;modal_undo=modal_info->UndoBuffer;modal_work=modal_info->Extension->WorkBuffer;strcpy((char *)modal_undo,"modal undo");activeGI=(struct GadgetInfo){.gi_Window=w,.gi_Screen=w->WScreen,.gi_RastPort=&activeRP};return TRUE;}
static struct TextFont *OpenDiskFont(struct TextAttr *ta) {CHECK(!install_lock);struct TextFont *f=AllocVec(sizeof(*f),MEMF_CLEAR);if(!f)return NULL;f->tf_YSize=ta->ta_YSize;f->tf_XSize=8;f->tf_Message.mn_Node.ln_Name=ta->ta_Name;f->tf_Style=ta->ta_Style;f->tf_Extension=&previewExtension;return f;}

static VOID CloseFont(struct TextFont *f) {NoLock();if(f&&f!=&previewSeed)FreeVec(f);}
"""

NATIVE_PORT_ADAPTERS = r"""
/* Exact public native bodies below; private flags/action and Exec transport
   are bounded adapters. The synchronous action frees Window BEFORE drain,
   just like native int_closewindow; ports have independent allocations. */
#define SKINS 1
#define IntWindow FakeWindow
#define SPFLAG_USERPORT 64
#define AROS_LIBFUNC_INIT
#define AROS_LIBFUNC_EXIT
#define SANITY_CHECKR(w,result) CHECK(w);
#define DEBUG_MODIFYIDCMP(...)
#define DEBUG_CLOSEWINDOW(...)
#define MUST_UNLOCK_SCREEN(w,s) FALSE
#define HAS_CHILDREN(w) FALSE
#define IS_CHILD(w) FALSE
#define USE_IDCMPUPDATE_MESSAGECACHE 0
#define USEWINDOWLOCK
#define FireScreenNotifyMessage(...) ((void)0)
#define UnlockPubScreen(...) CHECK(FALSE)
struct IIHData { struct Task *InputDeviceTask; };
static struct IIHData native_iih;static struct Interrupt native_input_handler;
struct LifecycleAction { IPTR unused; };
struct CloseWindowActionMsg { struct LifecycleAction msg;struct Window *window; };
static struct IntuitionBase *IntuitionBase;
static struct {struct MsgPort *port;BOOL live;} port_records[128];
static ULONG port_records_count;
static BOOL IsLivePort(struct MsgPort *p) {
 if(p==&eventport||p==&appport)return TRUE;
 for(ULONG i=0;i<port_records_count;i++)if(port_records[i].port==p&&port_records[i].live)return TRUE;
 return FALSE;
}
static struct MsgPort *CreateMsgPort(void) {
 struct MsgPort *p=AllocVec(sizeof(*p),MEMF_CLEAR);if(!p)return NULL;
 CHECK(port_records_count<128);port_records[port_records_count].port=p;port_records[port_records_count++].live=TRUE;
 p->mp_SigBit=9;NEWLIST(&p->mp_MsgList);++native_port_creates;return p;
}
static VOID DeleteMsgPort(struct MsgPort *p) {
 CHECK(p&&p!=&eventport&&p!=&appport&&IsLivePort(p)&&IsListEmpty(&p->mp_MsgList));
 for(ULONG i=0;i<port_records_count;i++)if(port_records[i].port==p&&port_records[i].live){port_records[i].live=FALSE;++native_port_deletes;FreeVec(p);return;}
 abort();
}
static VOID int_closewindow(struct CloseWindowActionMsg *m,struct IntuitionBase *b) {
 (void)b;struct Window *w=m->window;CHECK(w&&w!=self.pr_WindowPtr&&IsLivePort(w->UserPort));
 /* Last InputTask reader sees live gadgets until synchronous detachment. */
 for(struct Gadget *g=w->FirstGadget;g;g=g->NextGadget){volatile UWORD id=g->GadgetID;(void)id;if(g->SpecialInfo){volatile UBYTE c=((struct StringInfo *)g->SpecialInfo)->Buffer[0];(void)c;}}
 for(ULONG slot=2;slot<4;slot++){struct IntuiMessage *late=&event_messages[slot];memset(late,0,sizeof(*late));late->IDCMPWindow=w;late->Class=modal_phase?IDCMP_RAWKEY:IDCMP_CHANGESCALE;late->IAddress=modal_identity;AddTail(&w->UserPort->mp_MsgList,(struct Node *)late);}
 ++native_modal_closes;++closed_count;FreeVec(w);
}
static VOID DoSyncAction(APTR function,struct LifecycleAction *m,struct IntuitionBase *b) {
 CHECK(!install_lock&&semaphore_depth==1&&function==(APTR)int_closewindow);int_closewindow((struct CloseWindowActionMsg *)m,b);
}
static VOID CheckNativePortPolicies(void) {
 native_iih.InputDeviceTask=(struct Task *)(IPTR)1;native_input_handler.is_Data=&native_iih;fakeIBase.InputHandler=&native_input_handler;
 struct FakeWindow f={0};CHECK(NativeModifyIDCMP(&f.w,IDCMP_RAWKEY));struct MsgPort *owned=f.w.UserPort;CHECK(owned&&IsLivePort(owned));CHECK(NativeModifyIDCMP(&f.w,0));CHECK(!f.w.UserPort&&!IsLivePort(owned));
 f.specialflags=SPFLAG_USERPORT;f.w.UserPort=CreateMsgPort();CHECK(f.w.UserPort);struct MsgPort *external=f.w.UserPort;CHECK(NativeModifyIDCMP(&f.w,IDCMP_RAWKEY)&&f.w.UserPort==external);CHECK(NativeModifyIDCMP(&f.w,0)&&!f.w.UserPort&&IsLivePort(external));DeleteMsgPort(external);
 f.w.UserPort=&eventport;CHECK(NativeModifyIDCMP(&f.w,IDCMP_RAWKEY));CHECK(NativeModifyIDCMP(&f.w,0)&&!f.w.UserPort&&IsLivePort(&eventport));
}
"""

DRIVER = r"""
static IPTR CallHookPkt(struct Hook *h,APTR obj,APTR lib) {
 if(h==&globalHook) {CHECK(*(ULONG *)lib==SGH_KEY);return DoSGHKey(obj,NULL);}
 struct LayoutData *ld=obj;
 if(ld->ld_Command==LDCMD_LAYOUT)return TRUE;
 if(ld->ld_Command==LDCMD_HANDLEEVENTS) {
  struct IntuiMessage *m=ld->ld_Event;
  if(m->Class==IDCMP_RAWKEY){++raw_seen;return TRUE;}
  if(CancelEvent(m))return FALSE;
  if(m->Class==IDCMP_GADGETUP)return LDRET_FINISHED;
  return TRUE;
 }
 if(ld->ld_Command==LDCMD_HANDLEAPPWINDOW)return TRUE;
 CHECK(ld->ld_Command==LDCMD_INIT);
 switch(ld->ld_IntReq->ir_ReqType) {case ASL_FileRequest:return FRGadInit(ld,lib);case ASL_FontRequest:return FOGadInit(ld,lib);default:return SMGadInit(ld,lib);}
}
static Object *edit_object(struct LayoutData *ld) {
 switch(ld->ld_IntReq->ir_ReqType) {case ASL_FileRequest:return ((struct FRUserData *)ld->ld_UserData)->FileGad;case ASL_FontRequest:return ((struct FOUserData *)ld->ld_UserData)->NameString;default:return ((struct SMUserData *)ld->ld_UserData)->WidthGadget;}
}
static VOID prepare_class(Class *cl) {cl->cl_InstOffset=__builtin_offsetof(struct FakeObject,data);cl->cl_UserData=(IPTR)&base;}
static VOID InjectKey(UBYTE letter) {
 CHECK(active&&(active->Flags&GFLG_SELECTED));
 struct InputEvent event={.ie_Class=IECLASS_RAWKEY,.ie_Code=letter};UWORD code=0;
 ULONG result=HandleStrInput(active,&activeGI,&event,&code,NULL);
 CHECK(result&SGA_USE);CHECK(!(result&(SGA_END|SGA_NEXTACTIVE|SGA_PREVACTIVE)));
}
static struct AwareGeometry canonical, propertyCanonical, modalCanonical;
static VOID RememberModalGeometry(const struct ScaleWindowGeometry *d,ULONG scale) {
 LONG a[8]={d->Left,d->Top,d->Width,d->Height,d->MinWidth,d->MinHeight,d->MaxWidth,d->MaxHeight};(void)scale;for(ULONG i=0;i<8;i++)modalCanonical.axis[i]=(struct AwareGeometryValue){a[i],100};
}
static VOID ScaleModalNative(const struct ScreenRenderContext *ctx) {
 NativeScale(event_ld,ctx);struct Window *w=modal_window;struct AwareGeometryPlan p;struct AwareGeometryFrame f={(4*ctx->src_Scale+50)/100,ctx->src_PhysicalDrawInfo->dri_Font->tf_YSize+(3*ctx->src_Scale+50)/100,(4*ctx->src_Scale+50)/100,(16*ctx->src_Scale+50)/100};CHECK(aware_geometry_prepare(&modalCanonical,ctx->src_Scale,2560,1600,&f,&p));w->LeftEdge=p.left;w->TopEdge=p.top;w->Width=p.width;w->Height=p.height;w->BorderLeft=f.left;w->BorderRight=f.right;w->BorderTop=f.top;w->BorderBottom=f.bottom;activeGI.gi_Domain=(struct IBox){0,0,w->Width,w->Height};activeGI.gi_DrInfo=ctx->src_PhysicalDrawInfo;
}
static VOID ModalFlipContext(void) {current[0]=modal_flip_target;event_target=modal_flip_target;ScaleModalNative(modal_flip_target);}
static BOOL fit_refuse;
static ULONG fits, fit_fail_at;
static BOOL FitWindowLayout(struct Window *w,const struct ScreenRenderContext *ctx,LONG minw,LONG minh) {
 CHECK(!install_lock);if(ctx!=current[0])return FALSE;CHECK(minw>0&&minh>0);++fits;
 if(fit_refuse||fits==fit_fail_at)return FALSE;
 struct AwareGeometry before=canonical;
 if(minw>2560||minh>1600)return FALSE;if(w->Width<minw)w->Width=minw;if(w->Height<minh)w->Height=minh;if(w->LeftEdge+w->Width>2560)w->LeftEdge=2560-w->Width;if(w->TopEdge+w->Height>1600)w->TopEdge=1600-w->Height;
 CHECK(!memcmp(&canonical,&before,sizeof(canonical)));return TRUE;
}
static VOID NativeScale(struct LayoutData *ld,const struct ScreenRenderContext *ctx) {
 struct Window *w=ld->ld_Window;struct AwareGeometryPlan plan;
 current[0]=ctx;
 struct AwareGeometryFrame frame={(4*ctx->src_Scale+50)/100,ctx->src_PhysicalDrawInfo->dri_Font->tf_YSize+(3*ctx->src_Scale+50)/100,(4*ctx->src_Scale+50)/100,(16*ctx->src_Scale+50)/100};
 CHECK(aware_geometry_prepare(&canonical,ctx->src_Scale,2560,1600,&frame,&plan));
 w->LeftEdge=plan.left;w->TopEdge=plan.top;w->Width=plan.width;w->Height=plan.height;
 w->BorderLeft=frame.left;w->BorderTop=frame.top;w->BorderRight=frame.right;w->BorderBottom=frame.bottom;
 if(ld->ld_Window2) {struct Window *p=ld->ld_Window2;struct AwareGeometryPlan pp;CHECK(aware_geometry_prepare(&propertyCanonical,ctx->src_Scale,2560,1600,&frame,&pp));p->Width=pp.width;p->Height=pp.height;p->BorderLeft=frame.left;p->BorderTop=frame.top;p->BorderRight=frame.right;p->BorderBottom=frame.bottom;}

 /* Server cache preparation148 is an explicit dependency. This fixture
    executes ASL+actual string editing, not that separate server transaction. */
 activeGI.gi_DrInfo=ctx->src_PhysicalDrawInfo;activeGI.gi_Domain=(struct IBox){0,0,w->Width,w->Height};SetFont(&activeRP,ctx->src_PhysicalDrawInfo->dri_Font);
}
static VOID validate(struct LayoutData *ld) {
 CHECK(active==G(edit_object(ld)));CHECK(active->Flags&GFLG_SELECTED);
 CHECK(((struct StringInfo *)active->SpecialInfo)->Extension->Font==ld->ld_Font);
 for(struct AslObjectRecord *r=ld->ld_Objects;r;r=r->next)if(r->kind==ASLOBJ_GADGET) {
  struct Gadget *g=(APTR)r->object;CHECK(g->UserData==ld->ld_GadgetContext);
  if(r->objectClass==base.aslstringclass)CHECK(((struct AslStringData *)INST_DATA(r->objectClass,g))->ld==ld->ld_GadgetContext);
  if(r->objectClass==base.aslpropclass) {
   Object *list=ScrollerList(ld,r->object);CHECK(list);struct AslListViewData *v=INST_DATA(base.asllistviewclass,list);
   struct AslPropUnits units=AslPropEncode(v->totalpixels,v->visiblepixels,v->toppixel);IPTR actual;
   CHECK(GetAttr(PGA_Total,r->object,&actual)&&actual==(IPTR)units.total);CHECK(GetAttr(PGA_Visible,r->object,&actual)&&actual==(IPTR)units.visible);CHECK(GetAttr(PGA_Top,r->object,&actual)&&actual==(IPTR)units.top);
   CHECK(v->total==3000&&v->top==1500&&v->active==1700);
  }
  if(r->objectClass==base.asleraserclass)continue;
  LONG x=g->LeftEdge+((g->Flags&GFLG_RELRIGHT)?ld->ld_Window->Width-1:0),y=g->TopEdge+((g->Flags&GFLG_RELBOTTOM)?ld->ld_Window->Height-1:0);
  LONG width=g->Width+((g->Flags&GFLG_RELWIDTH)?ld->ld_Window->Width:0),height=g->Height+((g->Flags&GFLG_RELHEIGHT)?ld->ld_Window->Height:0);
  if(!(x>=0&&y>=0&&width>0&&height>0&&x+width<=ld->ld_Window->Width&&y+height<=ld->ld_Window->Height))printf("bad id%u xy%ld,%ld wh%ld,%ld\n",g->GadgetID,(long)x,(long)y,(long)width,(long)height);
  CHECK(x>=0&&y>=0&&width>0&&height>0&&x+width<=ld->ld_Window->Width&&y+height<=ld->ld_Window->Height);
  if(r->objectClass==base.aslarrowclass) {
   BOOL found=FALSE;for(struct AslObjectRecord *im=ld->ld_Objects;im;im=im->next)if(im->kind==ASLOBJ_IMAGE&&im->object==g->GadgetRender)found=TRUE;CHECK(found);
  }
 }
}
static VOID EmitEvent(ULONG index,ULONG kind) {
 struct IntuiMessage *m=&event_messages[index];memset(m,0,sizeof(*m));m->Class=kind;m->IDCMPWindow=event_ld->ld_Window;
 AddTail(&m->IDCMPWindow->UserPort->mp_MsgList,(struct Node *)m);
}
static VOID EmitPrepareSemantic(void) {
 ULONG kind=modal_prepare_semantic;modal_prepare_semantic=0;struct IntuiMessage *m=&event_messages[1];memset(m,0,sizeof(*m));m->IDCMPWindow=kind==3?event_ld->ld_Window:modal_window;m->Class=kind==3?IDCMP_CLOSEWINDOW:IDCMP_GADGETUP;m->IAddress=kind==2?modal_window->FirstGadget->NextGadget:active;active->Flags&=~GFLG_SELECTED;AddTail(&m->IDCMPWindow->UserPort->mp_MsgList,(struct Node *)m);
}
static VOID CheckModalMethodGuards(void) {
 struct LayoutData l={0},c={0};struct AslInstallScale m={ASLGM_INSTALLSCALE,NULL,&l,&c};l.ld_ScaleAware=TRUE;l.ld_GList=modal_window->FirstGadget->NextGadget;c.ld_GadgetContext=(APTR)1;
 CHECK(!AslButton__ASLGM_INSTALLSCALE(base.aslbuttonclass,(Object *)modal_window->FirstGadget,(Msg)&m));
 l.ld_GList=modal_window->FirstGadget;CHECK(!AslButton__ASLGM_INSTALLSCALE(&classes[11],(Object *)modal_window->FirstGadget,(Msg)&m));
}
static ULONG Wait(ULONG mask) {
 if(modal_test) {
  if(!IsListEmpty(&modal_port->mp_MsgList))return 1UL<<modal_port->mp_SigBit;
  CHECK(mask==((1UL<<modal_port->mp_SigBit)|(1UL<<eventport.mp_SigBit)));CHECK(active==modal_identity);
  ULONG index=modal_phase++;CHECK(index<2);
  if(!index){CheckModalMethodGuards();current[0]=event_target;ScaleModalNative(event_target);InjectKey('z');modal_alloc_start=alloc_count;inject_edit=TRUE;if(modal_fail_point)modal_fault_armed=TRUE;if(modal_parent_fail)fail_at=alloc_count+1;if(modal_flip_mode==1)modal_flip_at=alloc_count+1;}
  else {ULONG count=0;for(struct Gadget *g=modal_window->FirstGadget;g;g=g->NextGadget){++count;CHECK(g->LeftEdge>=modal_window->BorderLeft&&g->TopEdge>=modal_window->BorderTop&&g->Width>0&&g->Height>0&&g->LeftEdge+g->Width<=modal_window->Width-modal_window->BorderRight&&g->TopEdge+g->Height<=modal_window->Height-modal_window->BorderBottom);for(struct Gadget *p=g->NextGadget;p;p=p->NextGadget)CHECK(g->LeftEdge+g->Width<=p->LeftEdge||p->LeftEdge+p->Width<=g->LeftEdge||g->TopEdge+g->Height<=p->TopEdge||p->TopEdge+p->Height<=g->TopEdge);}CHECK(count==3);CHECK(active->SpecialInfo==modal_info&&modal_info->Buffer==modal_buffer&&modal_info->UndoBuffer==modal_undo&&modal_info->Extension->WorkBuffer==modal_work&&!strcmp((char *)modal_undo,"modal undo"));CHECK(modal_window->LeftEdge>=0&&modal_window->TopEdge>=0&&modal_window->LeftEdge+modal_window->Width<=2560&&modal_window->TopEdge+modal_window->Height<=1600);CHECK(((struct StringInfo *)active->SpecialInfo)->Extension->Font==event_target->src_PhysicalDrawInfo->dri_Font);InjectKey('y');}
  struct IntuiMessage *m=&event_messages[index];memset(m,0,sizeof(*m));m->IDCMPWindow=(index&&modal_outcome==2)?event_ld->ld_Window:modal_window;m->Class=index?(modal_outcome==2?IDCMP_CLOSEWINDOW:IDCMP_GADGETUP):IDCMP_CHANGESCALE;m->IAddress=(index&&modal_outcome==1)?modal_window->FirstGadget->NextGadget:active;m->Code=0;
  AddTail(&m->IDCMPWindow->UserPort->mp_MsgList,(struct Node *)m);return 1UL<<m->IDCMPWindow->UserPort->mp_SigBit;
 }
 CHECK(++event_waits<10);CHECK(mask==((1UL<<eventport.mp_SigBit)|(modal_port?(1UL<<modal_port->mp_SigBit):event_ld->ld_AppMsgPort?(1UL<<appport.mp_SigBit):0)));
 if(event_phase++==0) {
  NativeScale(event_ld,event_target);EmitEvent(0,IDCMP_CHANGESCALE);
  if(event_mode==1)EmitEvent(1,IDCMP_CLOSEWINDOW);
  if(event_mode==2)fail_at=alloc_count+1;
  if(event_mode==3)late_event=TRUE;
  if(event_mode==4)late_text=TRUE;
  if(event_mode==5)for(struct AslObjectRecord*r=event_ld->ld_Objects;r;r=r->next)if(r->objectClass==base.aslpropclass)((struct PropInfo*)G(r->object)->SpecialInfo)->Flags|=KNOBHIT;
 }else EmitEvent(1,IDCMP_CLOSEWINDOW);
 return 1UL<<eventport.mp_SigBit;
}
static VOID Delay(ULONG ticks) {CHECK(ticks==1);CHECK(++event_delays<10);fail_at=0;stale_show=0;
 if(event_mode==5)for(struct AslObjectRecord*r=event_ld->ld_Objects;r;r=r->next)if(r->objectClass==base.aslpropclass)((struct PropInfo*)G(r->object)->SpecialInfo)->Flags&=~KNOBHIT;
}
int main(void) {
 CheckNativePortPolicies();
 fakeIBase.GlobalEditHook=&globalHook;
 for(ULONG i=0;i<12;i++)prepare_class(&classes[i]);
 base.aslbuttonclass=&classes[0];base.aslstringclass=&classes[1];base.asllistviewclass=&classes[2];base.aslpropclass=&classes[3];base.aslarrowclass=&classes[4];base.aslcycleclass=&classes[5];base.aslfontpreviewclass=&classes[6];base.aslfontstyleclass=&classes[7];base.aslcolorpickerclass=&classes[8];base.asleraserclass=&classes[9];base.aslframeclass=&classes[10];
 ULONG scales[]={100,125,150,200,300,400,100};
 for(ULONG kind=0;kind<3;kind++) {
  union {struct IntFileReq fr;struct IntFontReq fo;struct IntSMReq sm;} ir={0};ir.fr.ifr_IntReq.ir_ReqType=kind;
  if(kind==ASL_FileRequest)ir.fr.ifr_Flags1=FRF_DOPATTERNS;
  if(kind==ASL_FontRequest){ir.fo.ifo_Flags=FOF_DOSTYLE|FOF_DOFRONTPEN|FOF_DOBACKPEN|FOF_DODRAWMODE;ir.fo.ifo_MaxFrontPen=16;ir.fo.ifo_MaxBackPen=16;}
  if(kind==ASL_ScreenModeRequest)ir.sm.ism_Flags=ISMF_DOWIDTH|ISMF_DOHEIGHT|ISMF_DODEPTH|ISMF_DOOVERSCAN|ISMF_DOAUTOSCROLL;
  struct LayoutData ld={0};ld.ld_Events=AllocVec(sizeof(*ld.ld_Events),0);NEWLIST(ld.ld_Events);ld.ld_IntReq=&ir.fr.ifr_IntReq;ld.ld_Screen=&screens[0];ld.ld_ScaleAware=TRUE;
  ULONG bytes=kind==ASL_FileRequest?sizeof(struct FRUserData):kind==ASL_FontRequest?sizeof(struct FOUserData):sizeof(struct SMUserData);ld.ld_UserDataSize=bytes;ld.ld_UserData=AllocVec(bytes,MEMF_CLEAR);
  struct TextFont physical[7]={0},logical={.tf_YSize=13,.tf_XSize=8,.tf_Baseline=10};struct DrawInfo dri[7]={0},baseDri={.dri_Font=&logical,.dri_Screen=&screens[0]};struct ScreenRenderContext ctx[7]={0};
  screens[0].Width=2560;screens[0].Height=1600;screens[0].WBorLeft=screens[0].WBorRight=4;screens[0].WBorTop=2;
  for(ULONG i=0;i<7;i++){physical[i].tf_YSize=(13*scales[i]+50)/100;physical[i].tf_XSize=(8*scales[i]+50)/100;physical[i].tf_Baseline=(10*scales[i]+50)/100;dri[i].dri_Font=&physical[i];dri[i].dri_Screen=&screens[0];ctx[i].src_Scale=scales[i];ctx[i].src_PreparedScaleWindow=TRUE;ctx[i].src_LogicalDrawInfo=&baseDri;ctx[i].src_PhysicalDrawInfo=&dri[i];ctx[i].src_LegacyScaleFactor=(scales[i]+50)/100;ctx[i].src_BaseFont=(struct TTextAttr){.tta_Name="fixture.font",.tta_YSize=13};}
  current[0]=&ctx[0];CHECK(AslPrepareContext(&ld,&base));ld.ld_Command=LDCMD_INIT;CHECK(CallHookPkt(NULL,&ld,&base));
  ld.ld_Window=OpenWindowTagList(NULL,(struct TagItem[]){{WA_RenderContext,(IPTR)&ctx[0]},{WA_Hidden,TRUE},{WA_Width,640},{WA_Height,500},{WA_Left,40},{WA_Top,60},{TAG_DONE,0}});CHECK(ld.ld_Window);
  NEWLIST(&eventport.mp_MsgList);NEWLIST(&appport.mp_MsgList);eventport.mp_SigBit=3;appport.mp_SigBit=7;ld.ld_Window->UserPort=&eventport;ld.ld_Window->IDCMPFlags=IDCMP_CHANGESCALE|IDCMP_CLOSEWINDOW;
  self.pr_WindowPtr=ld.ld_Window;ld.ld_GadgetsAttached=TRUE;struct Window *identity=ld.ld_Window;
  active=G(edit_object(&ld));struct StringInfo *info=active->SpecialInfo;strcpy((char *)info->Buffer,"typed value");info->BufferPos=strlen((char *)info->Buffer);strcpy((char *)info->UndoBuffer,"undo remains");
  struct StringInfo *infoIdentity=info;APTR buffer=info->Buffer,undo=info->UndoBuffer,work=info->Extension->WorkBuffer;
  activeGI=(struct GadgetInfo){.gi_Window=identity,.gi_Screen=&screens[0],.gi_RastPort=&activeRP};
  struct NewWindow nw={.LeftEdge=40,.TopEdge=60,.Width=640,.Height=500};struct ScaleWindowGeometry descriptor;AslPrepareWindowGeometry(&ld,&nw,&descriptor);CHECK(descriptor.Width==640&&descriptor.Height==500);
  LONG values[8]={descriptor.Left,descriptor.Top,descriptor.Width,descriptor.Height,descriptor.MinWidth,descriptor.MinHeight,descriptor.MaxWidth,descriptor.MaxHeight};for(ULONG i=0;i<8;i++)canonical.axis[i]=(struct AwareGeometryValue){values[i],100};
  if(kind==ASL_FontRequest){previewSeed=(struct TextFont){.tf_YSize=13,.tf_Style=FSF_TAGGED,.tf_Extension=&previewExtension};previewSeed.tf_Message.mn_Node.ln_Name=(STRPTR)"preview.font";((struct FOUserData *)ld.ld_UserData)->PreviewFont=&previewSeed;((struct FOUserData *)ld.ld_UserData)->PreviewSize=13;((struct AslFontPreviewData *)INST_DATA(base.aslfontpreviewclass,((struct FOUserData *)ld.ld_UserData)->Preview))->font=&previewSeed;}
  if(kind==ASL_ScreenModeRequest) {
   struct SMUserData *sm=ld.ld_UserData;ir.sm.ism_PropertyList_NotWB=(STRPTR)"Property";(&ir.sm.ism_PropertyList_NotWB)[1]=(STRPTR)"Long display property";
   ld.ld_Window2=OpenWindowTagList(NULL,(struct TagItem[]){{WA_RenderContext,(IPTR)&ctx[0]},{WA_Hidden,TRUE},{WA_Width,240},{WA_Height,230},{TAG_DONE,0}});CHECK(ld.ld_Window2);
   sm->PropertyGadget=NewObjectA(base.asllistviewclass,NULL,(struct TagItem[]){{GA_UserData,(IPTR)ld.ld_GadgetContext},{TAG_DONE,0}});CHECK(sm->PropertyGadget);
   struct AslListViewData *pv=INST_DATA(base.asllistviewclass,sm->PropertyGadget);pv->font=ld.ld_Font;pv->total=2;pv->frame=NewObjectA(base.aslframeclass,NULL,(struct TagItem[]){{TAG_DONE,0}});CHECK(pv->frame);
   ((struct AslFrameData *)INST_DATA(base.aslframeclass,pv->frame))->ld=ld.ld_GadgetContext;
   struct NewWindow pn={.Width=240,.Height=230};struct ScaleWindowGeometry pd;AslPrepareWindowGeometry(&ld,&pn,&pd);LONG pa[8]={pd.Left,pd.Top,pd.Width,pd.Height,pd.MinWidth,pd.MinHeight,pd.MaxWidth,pd.MaxHeight};for(ULONG j=0;j<8;j++)propertyCanonical.axis[j]=(struct AwareGeometryValue){pa[j],100};
  }
  for(struct AslObjectRecord *r=ld.ld_Objects;r;r=r->next)if(r->objectClass==base.asllistviewclass) {
   struct AslListViewData *v=INST_DATA(base.asllistviewclass,r->object);v->total=3000;v->totalpixels=3000*v->lineheight;v->top=1500;v->toppixel=1500*v->lineheight;v->active=1700;
  }
  ld.ld_RenderContext=NULL; /* Exercise initial publication with a nonempty list. */
  for(ULONG i=0;i<7;i++) {
   NativeScale(&ld,&ctx[i]);ULONG before=strlen((char *)info->Buffer);inject_edit=i!=0;
   CHECK(AslRelayout(&ld,&(struct AslReqInfo){0},&base));CHECK(ld.ld_Window==identity&&self.pr_WindowPtr==identity);
   if(kind==ASL_ScreenModeRequest) {struct SMUserData *sm=ld.ld_UserData;struct AslListViewData *pv=INST_DATA(base.asllistviewclass,sm->PropertyGadget);CHECK(pv->font==ld.ld_Font&&pv->ld==ld.ld_GadgetContext&&pv->default_renderhook.h_Data==ld.ld_GadgetContext);CHECK(((struct AslFrameData *)INST_DATA(base.aslframeclass,pv->frame))->ld==ld.ld_GadgetContext);CHECK(G(sm->PropertyGadget)->Width>0&&G(sm->PropertyGadget)->Height>0&&ld.ld_Window2->RPort->Font==ld.ld_Font);}
   validate(&ld);CHECK(active->SpecialInfo==infoIdentity&&info->Buffer==buffer&&info->UndoBuffer==undo&&info->Extension->WorkBuffer==work);CHECK(!strcmp((char *)undo,"undo remains"));
   CHECK(strlen((char *)buffer)==before+(i!=0));InjectKey('y');CHECK(info->Buffer[strlen((char *)buffer)-1]=='y');CHECK(activeRP.Font==ld.ld_Font);
   const struct ScreenRenderContext *same=ld.ld_RenderContext;current[1]=&ctx[(i+1)%7];CHECK(AslRelayout(&ld,&(struct AslReqInfo){0},&base));CHECK(ld.ld_RenderContext==same);
   if(kind==ASL_FontRequest)CHECK(((struct FOUserData *)ld.ld_UserData)->PreviewFont->tf_YSize==physical[i].tf_YSize);
   printf("ASL stable kind%u scale%u window%ux%u font%u typed%u PASS\n",kind,scales[i],identity->Width,identity->Height,ld.ld_Font->tf_YSize,(ULONG)strlen((char *)buffer));
  }
  {struct LayoutData legacy={0};legacy.ld_Screen=&screens[0];legacy.ld_IntReq=ld.ld_IntReq;legacy.ld_ScaleAware=TRUE;current[0]=&ctx[0];ctx[0].src_PreparedScaleWindow=FALSE;
   CHECK(AslPrepareContext(&legacy,&base));CHECK(!legacy.ld_ScaleAware&&legacy.ld_RenderScale==100&&legacy.ld_Font==ctx[0].src_LogicalDrawInfo->dri_Font);FreeVisualInfo(legacy.ld_VisualInfo);ctx[0].src_PreparedScaleWindow=TRUE;
   CHECK(AslScaleEligible(ld.ld_IntReq));ld.ld_IntReq->ir_TextAttr=(APTR)1;CHECK(!AslScaleEligible(ld.ld_IntReq));ld.ld_IntReq->ir_TextAttr=NULL;ld.ld_IntReq->ir_IntuiMsgFunc=(APTR)1;CHECK(!AslScaleEligible(ld.ld_IntReq));ld.ld_IntReq->ir_IntuiMsgFunc=NULL;
   ld.ld_ScaleAware=FALSE;ULONG held=alloc_count;NativeScale(&ld,&ctx[3]);CHECK(AslRelayout(&ld,&(struct AslReqInfo){0},&base));CHECK(alloc_count==held&&ld.ld_RenderScale==100&&ld.ld_Window==identity);ld.ld_ScaleAware=TRUE;NativeScale(&ld,&ctx[0]);
  }
  NativeScale(&ld,&ctx[3]);ULONG start=alloc_count;CHECK(AslRelayout(&ld,&(struct AslReqInfo){0},&base));ULONG points=alloc_count-start;NativeScale(&ld,&ctx[0]);CHECK(AslRelayout(&ld,&(struct AslReqInfo){0},&base));
  for(ULONG point=1;point<=points;point++){
   NativeScale(&ld,&ctx[3]);struct LayoutData snapshot=ld;ULONG held=live_alloc;struct StringInfo textstate=*info;ULONG oldlen=strlen((char *)buffer);fail_at=alloc_count+point;
   CHECK(!AslRelayout(&ld,&(struct AslReqInfo){0},&base));fail_at=0;CHECK(ld.ld_Window==identity&&memcmp(&snapshot,&ld,sizeof(ld))==0&&live_alloc==held);CHECK(memcmp(&textstate,info,sizeof(*info))==0&&strlen((char *)buffer)==oldlen);
   NativeScale(&ld,&ctx[0]);
  }
  printf("ASL stable kind%u faults%u old identities/text retained PASS\n",kind,points);
  { /* Real GadInit minima + real106 arithmetic,150 server is separate. */
   NativeScale(&ld,&ctx[0]);CHECK(AslRelayout(&ld,&(struct AslReqInfo){0},&base));
   struct AwareGeometry saved=canonical;
   canonical.axis[2]=(struct AwareGeometryValue){ld.ld_MinWidth,100};canonical.axis[3]=(struct AwareGeometryValue){ld.ld_MinHeight,100};
   LONG minw=ld.ld_MinWidth,minh=ld.ld_MinHeight;
   NativeScale(&ld,&ctx[2]);struct LayoutData snapshot=ld;ULONG held=live_alloc;
   fit_refuse=TRUE;CHECK(!AslRelayout(&ld,&(struct AslReqInfo){0},&base));fit_refuse=FALSE;
   CHECK(!memcmp(&snapshot,&ld,sizeof(ld))&&live_alloc==held);
   struct AwareGeometry intent=canonical;
   CHECK(AslRelayout(&ld,&(struct AslReqInfo){0},&base));printf("FITTED kind%u minimum%ld,%ld window%u,%u\n",kind,(long)ld.ld_MinWidth,(long)ld.ld_MinHeight,identity->Width,identity->Height);validate(&ld);CHECK(!memcmp(&canonical,&intent,sizeof(canonical)));
   printf("MIN kind%u %ld,%ld -> %ld,%ld window%u,%u\n",kind,(long)minw,(long)minh,(long)ld.ld_MinWidth,(long)ld.ld_MinHeight,identity->Width,identity->Height);
   if(kind==ASL_FileRequest)CHECK(minw==316&&minh==223&&ld.ld_MinWidth==474&&ld.ld_MinHeight==345&&identity->Width==474&&identity->Height==345);
   printf("ASL measured fit kind%u %ldx%ld100 ->%ldx%ld150 canonical retained PASS\n",kind,(long)minw,(long)minh,(long)ld.ld_MinWidth,(long)ld.ld_MinHeight);
   NativeScale(&ld,&ctx[0]);CHECK(AslRelayout(&ld,&(struct AslReqInfo){0},&base));CHECK(identity->Width==minw&&identity->Height==minh);canonical=saved;
  }

  for(ULONG mode=0;mode<6;mode++) {
   NativeScale(&ld,&ctx[0]);CHECK(AslRelayout(&ld,&(struct AslReqInfo){0},&base));event_ld=&ld;event_target=&ctx[3];event_phase=event_waits=event_delays=event_replies=raw_seen=0;event_mode=mode;memset(event_messages,0,sizeof(event_messages));ld.ld_RetryScale=FALSE;
   CHECK(!HandleEvents(&ld,&(struct AslReqInfo){0},&base));CHECK(ld.ld_Window==identity&&event_replies==(mode==3?3:2));CHECK(raw_seen==(mode==3));if(mode==5)CHECK(event_delays==1&&ld.ld_RenderScale==200);CHECK(IsListEmpty(ld.ld_Events)&&IsListEmpty(&eventport.mp_MsgList));
  }
  if(kind==ASL_FileRequest) {
   struct TextFont mf[15]={0};struct DrawInfo md[15]={0};struct ScreenRenderContext mc[15]={0};ULONG ms[15];
   for(ULONG j=0;j<15;j++){ms[j]=j<13?100+j*25:100;mf[j]=(struct TextFont){.tf_YSize=(13*ms[j]+50)/100,.tf_XSize=(8*ms[j]+50)/100,.tf_Baseline=(10*ms[j]+50)/100};md[j]=(struct DrawInfo){.dri_Font=&mf[j],.dri_Screen=&screens[0]};mc[j]=ctx[0];mc[j].src_Scale=ms[j];mc[j].src_LegacyScaleFactor=(ms[j]+50)/100;mc[j].src_PhysicalDrawInfo=&md[j];if(j==14){mf[j].tf_YSize=18;mf[j].tf_XSize=11;mf[j].tf_Baseline=14;mc[j].src_BaseFont.tta_YSize=18;}}
   ULONG faults=0;
   for(ULONG phase=0;phase<14+faults+10;phase++) {
    ULONG j=phase<14?phase:0;NativeScale(&ld,&mc[j]);CHECK(AslRelayout(&ld,&(struct AslReqInfo){0},&base));event_ld=&ld;event_target=&mc[phase<14?(j+1)%14:phase==17+faults?14:4];memset(event_messages,0,sizeof(event_messages));modal_test=TRUE;modal_phase=modal_activations=modal_publications=0;modal_outcome=phase<14?phase%3:0;modal_fail_point=(phase>=14&&phase<14+faults)?phase-13:0;modal_flip_mode=phase==14+faults?1:phase==15+faults?2:phase==16+faults?3:0;modal_flip_at=0;modal_prepare_seen=modal_fault_armed=FALSE;modal_initial_flip=phase==18+faults;modal_initial_fail=phase==19+faults?1:0;modal_parent_fail=phase==23+faults;modal_prepare_semantic=phase>=20+faults?(modal_parent_fail?1:phase-19-faults):0;modal_flip_target=&mc[12];modal_final_text[0]=0;struct GadgetInfo savedGI=activeGI;event_delays=0;
    modal_request_start=alloc_count;ULONG shown_before=shown_count;STRPTR result=REQ_String((STRPTR)"Title",(STRPTR)"modal",(STRPTR)"OK",(STRPTR)"Cancel",&ld,&base);CHECK(modal_phase==(phase>=20+faults?1:2)&&modal_activations==1&&modal_publications>=1&&shown_count==shown_before+1);const char *expected=phase>=20+faults?"modalzx":"modalzxy";CHECK(!strcmp(modal_final_text,expected));if(modal_outcome||(phase>=21+faults&&!modal_parent_fail))CHECK(!result);else CHECK(result&&!strcmp(result,expected));if(modal_parent_fail)CHECK(ld.ld_RetryScale&&ld.ld_RenderContext!=current[0]);else CHECK(ld.ld_RenderContext==current[0]);if(result)FreeVec(result);
    if(!phase)faults=modal_alloc_points;if(phase>=14&&phase<17+faults)CHECK(event_delays);modal_test=FALSE;modal_window=NULL;modal_port=NULL;modal_identity=NULL;active=G(edit_object(&ld));activeGI=savedGI;ld.ld_PendingAbort=FALSE;AslDiscardEvents(&ld,NULL,&base);memset(event_messages,0,sizeof(event_messages));
    if(modal_parent_fail) {
     event_phase=event_waits=event_delays=event_replies=0;event_mode=0;modal_parent_fail=FALSE;CHECK(!HandleEvents(&ld,&(struct AslReqInfo){0},&base));CHECK(!ld.ld_RetryScale&&ld.ld_RenderContext==current[0]);memset(event_messages,0,sizeof(event_messages));
    }
    printf("ASL sharp stable modal case%u birth%u target%u font%u semantic%u fault%u PASS\n",phase,ms[j],event_target->src_Scale,event_target->src_PhysicalDrawInfo->dri_Font->tf_YSize,phase>=20+faults?phase-19-faults+10:modal_outcome,modal_fail_point);
   }
   NativeScale(&ld,&ctx[0]);CHECK(AslRelayout(&ld,&(struct AslReqInfo){0},&base));
   ULONG initial=modal_initial_points;
   for(ULONG point=1;point<=initial;point++) {
    event_ld=&ld;event_target=&mc[4];modal_test=TRUE;modal_phase=modal_activations=0;modal_initial_flip=modal_initial_fail=modal_prepare_semantic=modal_flip_mode=0;modal_fault_armed=FALSE;modal_window=NULL;modal_identity=NULL;modal_port=NULL;modal_request_start=alloc_count;ULONG held=live_alloc;struct Gadget *oldactive=active;struct StringInfo before=*(struct StringInfo *)active->SpecialInfo;fail_at=alloc_count+point;
    CHECK(!REQ_String((STRPTR)"Title",(STRPTR)"modal",(STRPTR)"OK",(STRPTR)"Cancel",&ld,&base));fail_at=0;CHECK(!modal_phase&&!modal_activations&&live_alloc==held&&active==oldactive&&!memcmp(&before,active->SpecialInfo,sizeof(before)));modal_test=FALSE;
   }
   printf("ASL modal initial allocations%u refusal/old main input preserved PASS\n",initial);
  }
  if(kind==ASL_ScreenModeRequest) {
   NativeScale(&ld,&ctx[2]);struct LayoutData snapshot=ld;ULONG held=live_alloc;fit_fail_at=fits+2;CHECK(!AslRelayout(&ld,&(struct AslReqInfo){0},&base));fit_fail_at=0;CHECK(!memcmp(&snapshot,&ld,sizeof(ld))&&held==live_alloc);
   CHECK(AslRelayout(&ld,&(struct AslReqInfo){0},&base));struct SMUserData *sm=ld.ld_UserData;struct AslListViewData *pv=INST_DATA(base.asllistviewclass,sm->PropertyGadget);DisposeObject(pv->frame);DisposeObject(sm->PropertyGadget);CloseWindow(ld.ld_Window2);ld.ld_Window2=NULL;
  }
  if(kind==ASL_FontRequest)CloseFont(((struct FOUserData *)ld.ld_UserData)->PreviewFont);
  memset(event_messages,0,sizeof(event_messages));self.pr_WindowPtr=NULL;AslDiscardEvents(&ld,NULL,&base);CloseWindow(identity);FreeVec(ld.ld_Events);AslDisposeObjects(&ld,&base);FreeVisualInfo(ld.ld_VisualInfo);if(ld.ld_Menu)FreeMenus(ld.ld_Menu);FreeVec(ld.ld_UserData);active=NULL;
 }
 CHECK(native_port_creates==native_port_deletes&&native_modal_closes*2==native_late_replies);printf("Native owned modal CloseWindow %u detach/drain/reply/delete paths; shared parent intact PASS\n",native_modal_closes);
 CHECK(live_alloc==0&&visual_count==0&&objects_created==objects_disposed);printf("PASS actual stable-ASL publishing + HandleStrInput/DoSGHKey and native106 arithmetic (server148 separate)\n");return 0;
}
"""

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path);args=ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='aslstable-') as tmp:
        out=args.out or Path(tmp);out.mkdir(parents=True,exist_ok=True)
        if re.search(r"\{\s*WA_UserPort\b",body('specialreq.c','OpenStringModal')):raise RuntimeError('modal must own its private port')
        flags=(ROOT/'rom/intuition/intuition_intern.h').read_text()
        flag=re.search(r'^#define SPFLAG_USERPORT[^\n]*',flags,re.M).group(0)
        text=PRELUDE+NATIVE_PORT_ADAPTERS.replace('#define SPFLAG_USERPORT 64',flag)
        text+='\nstatic BOOL NativeModifyIDCMP(struct Window *window,ULONG flags)\n'+native_body('modifyidcmp.c','ModifyIDCMP')+'\n'
        text+='\nstatic VOID NativeCloseWindow(struct Window *window)\n'+native_body('closewindow.c','CloseWindow')+'\n'
        text+='\n#define CATCOMP_ARRAY\n'+(SDK/'workbench/libs/asl/strings.h').read_text()+'\n'
        text+='static char *GetStringMock(LONG id) {for(ULONG i=0;CatCompArray[i].cca_Str;i++)if(CatCompArray[i].cca_ID==id)return (char *)CatCompArray[i].cca_Str;return "?";}\n'
        core=(ROOT/'rom/intuition/strgadgets.c').read_text()
        text+='\n'+core[core.index('#define SHIFT'):core.index('/*****************************************************************************************/',core.index('#define ENDRAW'))]+'\n'
        for name in ['UpdateStringInfo','MoveCharsLeft','DoSGHKey','HandleStrInput']:text+=body('../../../rom/intuition/strgadgets.c',name)+'\n'
        text+=body('basicfuncs.c','BiggestTextLength')+'\n'
        scale=(SRC/'aslscale.c').read_text();text+=scale[scale.index('LONG AslUnscaleValue'):]
        publish=(SRC/'aslpublish.c').read_text();text+=publish[publish.index('#define SWAP'):]
        gadgets=(SRC/'gadgets.c').read_text();text+=gadgets[gadgets.index('static const struct TagItem arrowinc_to_prop'):]
        text+=body('filereqsupport.c','FRCalcColumnWidths')+'\n'
        for file,prefix in [('filereqhooks.c','FR'),('fontreqhooks.c','FO'),('modereqhooks.c','SM')]:
            source=(SRC/file).read_text();defines='\n'.join(line for line in source.splitlines() if re.match(r'#(?:un)?def(?:ine)? (?:ID_|NUMBUTS|FONTPREVIEWHEIGHT)',line));a=source.index('struct ButtonInfo');b=source.index('};',a)+2
            text+='\n'+defines+'\n'+source[a:b].replace('struct ButtonInfo','struct '+prefix+'ButtonInfo')+'\n'+body(file,prefix+'GadInit').replace('struct ButtonInfo','struct '+prefix+'ButtonInfo')+'\n'
        text+='\n#define AslBase ((struct AslBase_intern *)cl->cl_UserData)\n'+body('buttonclass.c','AslButton__GM_LAYOUT')+'\n#undef AslBase\n'
        text+=body('basicfuncs.c','HandleEvents')+'\n'
        text+='\n#define OK_ID 1\n#define CANCEL_ID 2\n#define STRING_ID 3\n'
        special=(SRC/'specialreq.c').read_text();text+=special[special.index('struct StringModal'):].replace('static BOOL RelayoutStringModal(', 'static BOOL ActualRelayoutStringModal(',1)+'\n'
        text+='''static BOOL RelayoutStringModal(struct StringModal *m,struct AslBase_intern *b) {BOOL capture=modal_test&&modal_phase==1&&!modal_prepare_seen;ULONG start=alloc_count;modal_preparing=TRUE;if(modal_fault_armed){fail_at=alloc_count+modal_fail_point;modal_fault_armed=FALSE;}BOOL ok=ActualRelayoutStringModal(m,b);modal_preparing=FALSE;if(capture){modal_alloc_points=alloc_count-start;modal_prepare_seen=TRUE;}return ok;}\n'''
        text+='\n#define AslBase ((struct AslBase_intern *)cl->cl_UserData)\n'+body('buttonclass.c','AslButton__ASLGM_INSTALLSCALE')+'\n#undef AslBase\n'
        text+=DRIVER
        (out/'actual.c').write_text(text)
        cmd=['clang','-std=gnu11','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-Wno-incompatible-pointer-types','-Wno-pointer-sign','-Wno-macro-redefined','-I',str(SRC),'-I',str(ROOT/'compiler/include'),'-I',str(SDK/'bin/apple-aarch64/gen/include'),str(out/'actual.c'),'-o',str(out/'actual')]
        x=subprocess.run(cmd,capture_output=True,text=True);(out/'compile.log').write_text(x.stdout+x.stderr)
        if x.returncode:raise SystemExit(x.stderr)
        x=subprocess.run([cmd[-1]],capture_output=True,text=True);(out/'run.log').write_text(x.stdout+x.stderr);print(x.stdout+x.stderr,end='')
        if x.returncode:raise SystemExit(x.returncode)
        mutations={
            'publication_units':('struct AslPropUnits units = AslPropEncode(view->totalpixels, view->visiblepixels, view->toppixel);', 'struct AslPropUnits units = {view->totalpixels, view->visiblepixels, view->toppixel};'),
            'held_prop_gate':('if (info && (info->Flags & KNOBHIT)) return FALSE;', 'if (FALSE) return FALSE;'),
            'provider_admission':('ld->ld_ScaleAware && !ctx->src_PreparedScaleWindow','FALSE'),
            'legacy_route':('if (!ld->ld_ScaleAware && ld->ld_GadgetsAttached) return TRUE;','if (FALSE) return TRUE;'),
            'focus_call':('published = DoGadgetMethodA(ld->ld_GList, ld->ld_Window, NULL, (Msg)&install);','ActivateWindow(ld->ld_Window); published = DoGadgetMethodA(ld->ld_GList, ld->ld_Window, NULL, (Msg)&install);'),
            'string_font':('info->Extension->Font = candidate->ld_Font;','(void)info;'),
            'string_identity':('struct StringInfo *info = a->SpecialInfo;','a->SpecialInfo = b->SpecialInfo; struct StringInfo *info = a->SpecialInfo;'),
            'publication_alloc':('if (old || next) return FALSE;','if (old || next) return FALSE; AllocVec(1, MEMF_ANY);'),
            'image_ownership':('SWAP(old->object, next->object);','(void)old;'),
            'native_geometry':('AslUnscaleValue(ld, nw->Width), AslUnscaleValue(ld, nw->Height),','1, 1,'),
            'native_close_requires_full_drain':('while ((im = (struct IntuiMessage *) GetMsg (userport)))', 'if ((im = (struct IntuiMessage *) GetMsg (userport)))'),
            'native_close_requires_capture':('userport = window->UserPort;', 'userport = NULL;'),
            'old_modal_port_shutdown':('CloseWindow(modal->layout.ld_Window);', 'struct IntuiMessage *message; ModifyIDCMP(modal->layout.ld_Window,0); while ((message = (struct IntuiMessage *)GetMsg(modal->layout.ld_Window->UserPort))) ReplyMsg((struct Message *)message); CloseWindow(modal->layout.ld_Window);'),
            'parent_retry_after_modal_accept':('parent->ld_RetryScale = !parentReady && !parent->ld_PendingAbort;', 'parent->ld_RetryScale = FALSE;'),
            'modal_admission_context':('if (!GetAttr(SA_RenderContext,(Object *)live->ld_Screen,(IPTR *)&ctx) || ctx != candidate.ld_RenderContext)', 'if (FALSE)'),
            'modal_exact_class':('OCLASS(object) != cl || cl != AslBase->aslbuttonclass ||', 'FALSE ||'),
            'modal_same_receiver':('object != (Object *)install->live->ld_GList', 'FALSE'),
            'modal_aware_font':('modal->layout.ld_PrepareOnly = FALSE;', 'modal->layout.ld_ScaleAware = FALSE; modal->layout.ld_PrepareOnly = FALSE;'),
            'measured_minimum':('candidate.ld_MinWidth, candidate.ld_MinHeight)) goto failed;', '1, 1)) goto failed;'),
            'preserve_layout_context':('SWAP(x->frame,y->frame); SWAP(x->ld,y->ld);\n            /* These exact ASL strings','SWAP(x->frame,y->frame);\n            /* These exact ASL strings'),
        }
        rejected=[]
        for name,(old,new) in mutations.items():
            if old not in text:raise RuntimeError('mutation anchor missing '+name)
            mutant=out/(name+'.c');mutant.write_text(text.replace(old,new,1));args=cmd.copy();args[-3]=str(mutant);args[-1]=str(out/name)
            x=subprocess.run(args,capture_output=True,text=True)
            if x.returncode:raise RuntimeError('invalid mutant '+name+x.stderr)
            x=subprocess.run([args[-1]],capture_output=True,text=True);(out/(name+'.log')).write_text(x.stdout+x.stderr)
            if not x.returncode:raise RuntimeError('mutation survived '+name)
            rejected.append(name)
        print('Rejected actual-body mutations:',','.join(rejected))
        (out/'result.json').write_text(json.dumps({'status':'PASS','scope':'actual ASL resource publication, HandleStrInput/DoSGHKey, native106 arithmetic with OS/BOOPSI/raster adapters; cache148 and native runtime separate','command':cmd,'rejected_mutations':rejected},indent=2))
if __name__=='__main__':main()
