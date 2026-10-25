#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License 1.1.
"""Actual requester row/count consumers with explicit gadget/pool/OS adapters."""
from pathlib import Path
import argparse,subprocess,re,json,hashlib
ROOT=Path(__file__).resolve().parents[4]
SRC=ROOT/'workbench/libs/asl'
SDK=ROOT.parent/'Build/display-composition-native'
def body(file,name):
 text=(SRC/file).read_text()
 m=re.search(r'(?m)^(?:static |STATIC )?(?:struct Node \*|struct ASLLVFontReqNode \*|struct DisplayMode \*|LONG |WORD |ULONG |VOID |void |int )'+name+r'\s*\([^;]*?\)\s*\{',text);assert m,(file,name)
 end=m.end();depth=1
 while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[m.start():end]
PREFIX=r'''
#include "asl_intern.h"
#include "layout.h"
#include "filereqhooks.h"
#include "filereqsupport.h"
#include "fontreqhooks.h"
#include "fontreqsupport.h"
#include "modereqhooks.h"
#include "modereqsupport.h"
#include <exec/memory.h>
#include <devices/inputevent.h>
#include <workbench/startup.h>
#include <string.h>
extern void *malloc(__SIZE_TYPE__);
extern void free(void*);
extern void abort(void);
extern int printf(const char*,...);
extern int sprintf(char*,const char*,...);
extern void qsort(void*,__SIZE_TYPE__,__SIZE_TYPE__,int(*)(const void*,const void*));
#define CHECK(x) do{if(!(x)){printf("CHECK %d %s\n",__LINE__,#x);abort();}}while(0)
#define D(x)
#define NEWLIST(l) do{(l)->lh_Head=(struct Node*)&(l)->lh_Tail;(l)->lh_Tail=0;(l)->lh_TailPred=(struct Node*)&(l)->lh_Head;}while(0)
struct Fake {IPTR active,total,visible,size;STRPTR text;};
static ULONG allocations,live,refuse,locks;
static BOOL GetAttr(ULONG t,Object*o,IPTR*v){struct Fake*f=(APTR)o;CHECK(f);switch(t){case ASLLV_Active:*v=f->active;break;case ASLLV_Total:*v=f->total;break;case ASLLV_Visible:*v=f->visible;break;case STRINGA_TextVal:*v=(IPTR)f->text;break;case STRINGA_LongVal:*v=f->size;break;default:CHECK(0);}return TRUE;}
static void SetGadgetAttrsA(struct Gadget*g,struct Window*w,struct Requester*r,struct TagItem*t){struct Fake*f=(APTR)g;CHECK(f);for(;t->ti_Tag;t++)if(t->ti_Tag==ASLLV_Active)f->active=t->ti_Data;}
static BOOL ActivateGadget(struct Gadget*g,struct Window*w,struct Requester*r){return TRUE;}
static LONG Stricmp(CONST_STRPTR a,CONST_STRPTR b){return strcmp(a,b);}
static LONG Strnicmp(CONST_STRPTR a,CONST_STRPTR b,LONG n){return strncmp(a,b,n);}
APTR MyAllocVecPooled(APTR pool,IPTR size,struct AslBase_intern*b){++allocations;if(allocations==refuse)return NULL;void*p=malloc(size);CHECK(p);++live;return p;}
void MyFreeVecPooled(APTR p,struct AslBase_intern*b){if(p){CHECK(live);--live;free(p);}}
char*VecPooledCloneString(const char*s,const char*end,APTR pool,struct AslBase_intern*b){IPTR n=strlen(s)+1;char*p=MyAllocVecPooled(pool,n,b);if(p)memcpy(p,s,n);return p;}
static BPTR Lock(CONST_STRPTR s,LONG mode){++locks;return (BPTR)1;}
static void UnLock(BPTR b){CHECK(b==(BPTR)1&&locks);--locks;}
static STRPTR file_value,font_value;
static LONG size_value;
void FRSetFile(STRPTR s,struct LayoutData*l,struct AslBase_intern*b){static char v[256];strcpy(v,s);file_value=v;}
void FRSetPath(STRPTR s,struct LayoutData*l,struct AslBase_intern*b){CHECK(0);}
void FRUpdatePath(STRPTR s,struct LayoutData*l,struct AslBase_intern*b){CHECK(0);}
void FRActivateMainStringGadget(struct LayoutData*l,struct AslBase_intern*b){}
void FOSetFontString(STRPTR s,struct LayoutData*l,struct AslBase_intern*b){font_value=s;}
void FOSetSizeString(LONG s,struct LayoutData*l,struct AslBase_intern*b){size_value=s;}
void FOSetDrawMode(struct LayoutData*l,UWORD id,struct AslBase_intern*b){}
void FOSetStyle(struct LayoutData*l,UBYTE id,struct AslBase_intern*b){}
void FOSetFGColor(struct LayoutData*l,UBYTE id,struct AslBase_intern*b){}
void FOSetBGColor(struct LayoutData*l,UBYTE id,struct AslBase_intern*b){}
void FOUpdatePreview(struct LayoutData*l,struct AslBase_intern*b){}
static struct DisplayMode*mode_value;
UWORD SMGetOverscan(struct LayoutData*l,struct DisplayMode*m,struct Rectangle**rect,struct AslBase_intern*b){static struct Rectangle r={0,0,639,479};*rect=&r;return 0;}
void SMFixValues(struct LayoutData*l,struct DisplayMode*m,LONG*w,LONG*h,LONG*depth,struct AslBase_intern*b){}
void SMRefreshPropertyWindow(struct LayoutData*l,struct DisplayMode*m,struct AslBase_intern*b){mode_value=m;}
LONG SMGetDepth(struct LayoutData*l,LONG*real,struct AslBase_intern*b){return 8;}
static BOOL ShowWindow(struct Window*w,struct Window*other){return TRUE;}
void SMClosePropertyWindow(struct LayoutData*l,struct AslBase_intern*b){}
static void linknodes(struct List*l,void*storage,IPTR size,LONG count){NEWLIST(l);for(LONG n=0;n<count;n++){struct Node*x=(APTR)((char*)storage+n*size);x->ln_Succ=n+1<count?(APTR)((char*)storage+(n+1)*size):(APTR)&l->lh_Tail;x->ln_Pred=n?(APTR)((char*)storage+(n-1)*size):(APTR)&l->lh_Head;}if(count){l->lh_Head=storage;l->lh_TailPred=(APTR)((char*)storage+(count-1)*size);}}
'''
DRIVER=r'''
int main(void){
 const LONG N=40000;struct AslBase_intern base={0};struct Window window={0};
 struct ASLLVFileReqNode*files=malloc(N*sizeof(*files));struct ASLLVFontReqNode*fonts=malloc(N*sizeof(*fonts));struct DisplayMode*modes=malloc(N*sizeof(*modes));struct Node*sizes=malloc(N*sizeof(*sizes));CHECK(files&&fonts&&modes&&sizes);memset(files,0,N*sizeof(*files));memset(fonts,0,N*sizeof(*fonts));memset(modes,0,N*sizeof(*modes));memset(sizes,0,N*sizeof(*sizes));
 for(LONG n=0;n<N;n++){NEWLIST(&fonts[n].SizeList);files[n].node.ln_Name=(STRPTR)(n==N-1?"last-file":"file");files[n].text[0]=files[n].node.ln_Name;files[n].node.ln_Pri=NODEPRIF_SELECTED|NODEPRIF_MULTISEL;fonts[n].node.ln_Name=(STRPTR)(n==N-1?"last-font":"font");sizes[n].ln_Name=(STRPTR)(IPTR)(n+1);}
 struct Fake list={39998,N,20},path={.text=(STRPTR)"drawer"},file={.text=(STRPTR)"typed"},name={.text=(STRPTR)"last-font"},size={.size=40000},sizelist={.total=N,.visible=20};
 struct FRUserData fr={.Listview=(Object*)&list,.PathGad=(Object*)&path,.FileGad=(Object*)&file};struct IntFileReq ifr={0};struct FileRequester result={0};struct LayoutData ld={.ld_Window=&window,.ld_UserData=&fr,.ld_IntReq=&ifr.ifr_IntReq,.ld_Req=&result};linknodes(&fr.ListviewList,files,sizeof(*files),N);
 CHECK(FindListNode(&fr.ListviewList,39999)==&files[39999].node);CHECK(!FindListNode(&fr.ListviewList,-1));
 FRChangeActiveLVItem(&ld,1,0,NULL,&base);CHECK(list.active==39999&&!strcmp(file_value,"last-file"));CHECK(FileDoubleClick(&ld)==&files[39999].node);list.active=0;file.text=(STRPTR)"last-file";FRChangeActiveLVItem(&ld,0,0,(struct Gadget*)&file,&base);CHECK(list.active==39999);file.text=(STRPTR)"typed";
 FRChangeActiveLVItem(&ld,-1,IEQUALIFIER_LALT,NULL,&base);CHECK(list.active==0);FRChangeActiveLVItem(&ld,1,IEQUALIFIER_LALT,NULL,&base);CHECK(list.active==39999);
 ifr.ifr_Flags1=FRF_DOMULTISELECT;CHECK(FRGetSelectedFiles(&ld,&base)==GHRET_FINISHED_OK);CHECK(result.fr_NumArgs==40000&&result.fr_ArgList&&locks==1);CHECK(!strcmp(result.fr_ArgList[39999].wa_Name,"last-file"));StripRequester(&result,ASL_FileRequest,&base);CHECK(!locks);
 /* Release cloned path/file separately: StripRequester deliberately leaves
  * these caller-visible pooled strings to the pool's lifetime. */
 MyFreeVecPooled(ifr.ifr_Drawer,&base);MyFreeVecPooled(ifr.ifr_File,&base);ifr.ifr_Drawer=ifr.ifr_File=NULL;
 for(ULONG point=3;point<=5;point++){
  refuse=allocations+(point==5?40003:point);CHECK(FRGetSelectedFiles(&ld,&base)==GHRET_OK);refuse=0;CHECK(!result.fr_ArgList&&!result.fr_NumArgs&&!locks);
  MyFreeVecPooled(result.fr_Drawer,&base);MyFreeVecPooled(result.fr_File,&base);result.fr_Drawer=result.fr_File=NULL;
 }
 CHECK(!live);
 struct FOUserData fo={.NameListview=(Object*)&list,.SizeListview=(Object*)&sizelist,.NameString=(Object*)&name,.SizeString=(Object*)&size};struct IntFontReq ifo={0};ld.ld_UserData=&fo;ld.ld_IntReq=&ifo.ifo_IntReq;linknodes(&fo.NameListviewList,fonts,sizeof(*fonts),N);linknodes(&fonts[N-1].SizeList,sizes,sizeof(*sizes),N);
 list.active=39998;FOChangeActiveFont(&ld,1,0,FALSE,&base);CHECK(sizelist.active==39999);CHECK(list.active==39999&&fo.ActiveFont==&fonts[39999]&&!strcmp(font_value,"last-font"));CHECK(FontDoubleClick(&ld)==&fonts[39999].node);CHECK(FOGetActiveFont(&ld,&base)==&fonts[39999]);list.active=0;FOChangeActiveFont(&ld,0,0,TRUE,&base);CHECK(list.active==39999);
 FORestore(&ld,(CONST_STRPTR)"last-font.font",40000,&base);CHECK(list.active==39999&&sizelist.active==39999);FOActivateSize(&ld,39999,&base);CHECK(sizelist.active==39999&&size_value==40000);CHECK(SizeDoubleClick(&ld)==&sizes[39999]);FOChangeActiveSize(&ld,-1,IEQUALIFIER_LALT,&base);CHECK(sizelist.active==0);FOChangeActiveSize(&ld,1,IEQUALIFIER_LALT,&base);CHECK(sizelist.active==39999);FOActivateSize(&ld,(-2147483647-1),&base);CHECK((LONG)sizelist.active==-1);
 FOChangeActiveFont(&ld,-1,IEQUALIFIER_LALT,FALSE,&base);CHECK(list.active==0);FOChangeActiveFont(&ld,1,IEQUALIFIER_LALT,FALSE,&base);CHECK(list.active==39999);
 struct SMUserData sm={.Listview=(Object*)&list};struct IntSMReq ism={0};ld.ld_UserData=&sm;ld.ld_IntReq=&ism.ism_IntReq;linknodes(&sm.ListviewList,modes,sizeof(*modes),N);list.active=39998;
 SMChangeActiveLVItem(&ld,1,0,&base);CHECK(list.active==39999&&mode_value==&modes[39999]);CHECK(ModeDoubleClick(&ld)==(struct Node*)&modes[39999]);CHECK(SMGetActiveMode(&ld,&base)==&modes[39999]);SMChangeActiveLVItem(&ld,-1,IEQUALIFIER_LALT,&base);CHECK(list.active==0);SMChangeActiveLVItem(&ld,1,IEQUALIFIER_LALT,&base);CHECK(list.active==39999);
 void*afstorage=malloc(sizeof(APTR)+N*sizeof(struct AvailFonts));CHECK(afstorage);struct AvailFontsHeader*afh=(APTR)((char*)afstorage+sizeof(APTR)-sizeof(*afh));afh->afh_NumEntries=N;struct AvailFonts*avf=(APTR)(afh+1);for(LONG n=0;n<N;n++){memset(&avf[n],0,sizeof(avf[n]));avf[n].af_Attr.ta_Name=(STRPTR)"font";avf[n].af_Attr.ta_YSize=N-n;}SortAvailFonts(afh,&base);CHECK(avf[0].af_Attr.ta_YSize==1&&avf[N-1].af_Attr.ta_YSize==N);free(afstorage);
 free(files);free(fonts);free(modes);free(sizes);printf("PASS actual requester lookup/navigation/activation and double-click lookup branches39999, 40000selectedargs and allocation refusals\n");
}
'''
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=True)
 text=PREFIX
 for file,names in [('basicfuncs.c',['FindListNode','CountNodes','StripRequester']),('filereqsupport.c',['FRChangeActiveLVItem']),('filereqhooks.c',['FRGetSelectedFiles']),('fontreqsupport.c',['AVFCompare','SortAvailFonts','FOGetActiveFont','FOActivateFont','FOActivateSize','FOChangeActiveFont','FOChangeActiveSize','FORestore']),('modereqsupport.c',['SMGetActiveMode','SMActivateMode','SMChangeActiveLVItem'])]:
  for name in names:text+=body(file,name)+'\n'
 # Extract the exact lookup predicates from the actual double-click handlers.
 for file,function,userdata,gadget,listfield,var,vartype in [('filereqhooks.c','FileDoubleClick','FRUserData','Listview','ListviewList','node','struct ASLLVFileReqNode'),('fontreqhooks.c','FontDoubleClick','FOUserData','NameListview','NameListviewList','fontnode','struct ASLLVFontReqNode'),('fontreqhooks.c','SizeDoubleClick','FOUserData','SizeListview','ActiveFont->SizeList','node','struct Node'),('modereqhooks.c','ModeDoubleClick','SMUserData','Listview','ListviewList','dispmode','struct DisplayMode')]:
  source=(SRC/file).read_text();line=next(l.strip() for l in source.splitlines() if 'if (('+var+' =' in l and 'FindListNode(&udata->'+listfield in l)
  text+='struct Node*'+function+'(struct LayoutData*ld){struct '+userdata+'*udata=ld->ld_UserData;IPTR active;'+vartype+'*'+var+';GetAttr(ASLLV_Active,udata->'+gadget+',&active);'+line+'return (struct Node*)'+var+';return NULL;}\n'
 text+=DRIVER;(a.out/'actual.c').write_text(text)
 cmd=['clang','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-Wno-incompatible-pointer-types','-Wno-pointer-sign','-I',str(SRC),'-I',str(ROOT/'compiler/include'),'-I',str(SDK/'bin/apple-aarch64/gen/include'),str(a.out/'actual.c'),'-o',str(a.out/'actual')]
 p=subprocess.run(cmd,capture_output=True,text=True);(a.out/'compile.log').write_text(p.stdout+p.stderr);assert not p.returncode,p.stderr
 p=subprocess.run([cmd[-1]],capture_output=True,text=True);(a.out/'run.log').write_text(p.stdout+p.stderr);assert not p.returncode,p.stdout+p.stderr;print(p.stdout,end='')
 rejected=[]
 mutations={
  'helper_index_word':('if (which >= 0)', 'which = (WORD)which; if (which >= 0)'),
  'file_doubleclick_word':(next(l for l in text.splitlines() if l.startswith('struct Node*FileDoubleClick')), next(l for l in text.splitlines() if l.startswith('struct Node*FileDoubleClick')).replace('(LONG)active','(WORD)active')),
  'file_alt_word':('delta *= total;', 'delta = (WORD)(delta * total);'),
  'file_jump_word':('LONG i;\n        WORD len;', 'WORD i;\n        WORD len;'),
  'font_activate_word':('fontnode = (struct ASLLVFontReqNode *)FindListNode(&udata->NameListviewList, which);', 'fontnode = (struct ASLLVFontReqNode *)FindListNode(&udata->NameListviewList, (WORD)which);'),
  'mode_activate_word':('dispmode = (struct DisplayMode *)FindListNode(&udata->ListviewList, which);', 'dispmode = (struct DisplayMode *)FindListNode(&udata->ListviewList, (WORD)which);'),
  'size_index_word':('LONG                    sizelvindex = 0;', 'WORD                    sizelvindex = 0;'),
  'restore_index_word':('LONG                    i = 0;', 'WORD                    i = 0;'),
  'avail_count_word':('ULONG               numentries;', 'WORD                numentries;'),
  'negative_size_word':('QUAD size  = -(QUAD)which;', 'LONG size  = -which;'),
  'selected_count_word':('LONG numselected = 0, count = 0;', 'WORD numselected = 0; LONG count = 0;'),
  'returned_count_word':('LONG numselected = 0, count = 0;', 'LONG numselected = 0; WORD count = 0;'),
  'partial_names_success':('if (failed)', 'if (FALSE)'),
 }
 for name,(old,new) in mutations.items():
  assert old in text,name
  path=a.out/(name+'.c');path.write_text(text.replace(old,new,1));mutcmd=cmd.copy();mutcmd[-3]=str(path);mutcmd[-1]=str(a.out/name)
  p=subprocess.run(mutcmd,capture_output=True,text=True);assert not p.returncode,p.stderr
  p=subprocess.run([mutcmd[-1]],capture_output=True,text=True);(a.out/(name+'.log')).write_text(p.stdout+p.stderr);assert p.returncode,'survived '+name
  rejected.append(name)
 print('Rejected actual consumer mutations:',','.join(rejected))
 (a.out/'result.json').write_text(json.dumps({'status':'PASS','scope':__doc__,'command':cmd,'rejected_mutations':rejected,'actual_double_click_scope':'exact lookup predicates extracted from production handlers; outer event dispatch and callbacks are not executed'},indent=2)+'\n')
if __name__=='__main__':main()
