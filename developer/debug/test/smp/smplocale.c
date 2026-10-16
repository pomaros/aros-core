/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL), Version 1.1.
 * Native locale/catalog operations on APs. Only RAM files are written.
 * Child context and code survive until every worker is reaped. */
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <libraries/locale.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/kernel.h>
#include <proto/locale.h>
#include <proto/utility.h>
#include <aros/debug.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
APTR KernelBase;
struct LocaleBase *LocaleBase;
#define WORKERS 4
#define ROUNDS 32
#define REPORT(...) do { Printf(__VA_ARGS__); bug(__VA_ARGS__); } while (0)
struct Group { ULONG phase, stop; const char *prefs, *catalog; struct Locale *a,*b; };
struct Worker {
 struct Group *group; struct Task *parent,*task;
 ULONG parent_signal,go_signal,cpu,index,tid,first,last,done,error;
 ULONG loads,conversions,switches,seen,versions;
 uint64_t begin,end; struct Catalog *node;
};
static uint64_t counter(void) { uint64_t v; __asm__ volatile("mrs %0,cntvct_el0":"=r"(v));return v; }
static BOOL convert(struct Locale *l) {
 return ConvToUpper(l,'a')=='A' && StrnCmp(l,"Alpha","beta",-1,SC_ASCII)<0 && Stricmp("Alpha","beta")<0;
}
static struct Catalog *open_catalog(struct Group *g,ULONG version) {
 struct TagItem tags[]={{OC_BuiltInLanguage,0},{OC_Language,(IPTR)"french"},{OC_Version,version},{TAG_DONE,0}};
 return OpenCatalogA(g->a,g->catalog,tags);
}
static LONG Work(void) {
 struct Task *self=FindTask(NULL);struct Worker *w=self->tc_UserData;struct Group *g=w->group;
 BYTE bit=AllocSignal(-1);w->task=self;w->first=KrnGetCPUNumber();
 if(bit>=0){w->go_signal=1UL<<bit;SetSignal(0,w->go_signal);}
 __atomic_store_n(&w->tid,self->tc_UnionETask.tc_ETask->et_UniqueID,__ATOMIC_RELEASE);
 Signal(w->parent,w->parent_signal);
 if(bit<0){w->error=1;goto done;}
 Wait(w->go_signal);w->begin=counter();
 if(g->phase==0) {
  w->node=open_catalog(g,7);
  if(!w->node){w->error=2;goto finished;}
  struct Catalog *wrong=open_catalog(g,8);
  if(wrong){CloseCatalog(wrong);w->error=3;goto finished;}w->versions=1;
  for(unsigned i=0;i<ROUNDS;i++) {
   struct Locale *l=OpenLocale(g->prefs);if(!l){w->error=4;break;}
   BOOL good=convert(l);CloseLocale(l);if(!good){w->error=5;break;}w->loads++;
   struct Catalog *c=open_catalog(g,7);
   good=c==w->node && !strcmp(GetCatalogStr(c,1,"missing"),"native-smp");
   if(c)CloseCatalog(c);if(!good){w->error=6;break;}w->conversions++;
  }
 } else if(w->index==0) {
  for(unsigned i=0;i<32;i++){CloseLocale(LocalePrefsUpdate((i&1)?g->a:g->b));w->switches++;Delay(1);}
  __atomic_store_n(&g->stop,1,__ATOMIC_RELEASE);
 } else {
  uint64_t hz;__asm__ volatile("mrs %0,cntfrq_el0":"=r"(hz));
  while(!__atomic_load_n(&g->stop,__ATOMIC_ACQUIRE)) {
   struct Locale *l=OpenLocale(NULL);if(!l){w->error=7;break;}
   if(l==g->a)w->seen|=1;else if(l==g->b)w->seen|=2;else w->error=8;
   if(!convert(l))w->error=9;CloseLocale(l);if(w->error)break;w->conversions++;
   if(counter()-w->begin>hz*10){w->error=10;break;}
  }
 }
finished:w->end=counter();FreeSignal(bit);
done:w->last=KrnGetCPUNumber();__atomic_store_n(&w->done,1,__ATOMIC_RELEASE);
 return w->error?RETURN_FAIL:RETURN_OK;
}
static const UBYTE catalog_bytes[]={70,79,82,77,0,0,0,90,67,84,76,71,70,86,69,82,0,0,0,34,36,86,69,82,58,32,115,109,112,46,99,97,116,97,108,111,103,32,55,46,48,32,40,52,46,49,48,46,50,48,50,54,41,0,76,65,78,71,0,0,0,7,102,114,101,110,99,104,0,0,83,84,82,83,0,0,0,20,0,0,0,1,0,0,0,11,110,97,116,105,118,101,45,115,109,112,0,0};
static BOOL wave(struct Group *g,struct Worker *workers) {
    static const ULONG cpus[WORKERS]={1,3,4,7};
    BYTE bit=AllocSignal(-1);unsigned created=0;BOOL ok=TRUE;
    uint64_t begin=0,end=UINT64_MAX;
    if(bit<0)return FALSE;
    for(unsigned i=0;i<WORKERS;i++) {
        struct Worker *w=&workers[i];w->group=g;w->index=i;w->cpu=cpus[i];
        w->parent=FindTask(NULL);w->parent_signal=1UL<<bit;
        APTR affinity=KrnAllocCPUMask();if(!affinity)break;
        KrnClearCPUMask(affinity);KrnGetCPUMask(w->cpu,affinity);
        if(!CreateNewProcTags(NP_Entry,(IPTR)Work,NP_Name,(IPTR)"SMP-LocaleWorker",
            NP_UserData,(IPTR)w,NP_StackSize,256*1024,NP_Affinity,(IPTR)affinity,
            NP_Cli,TRUE,NP_NotifyOnDeath,TRUE,TAG_DONE)) {KrnFreeCPUMask(affinity);break;}
        created++;
    }
    for(unsigned i=0;i<created;i++)
        while(!__atomic_load_n(&workers[i].tid,__ATOMIC_ACQUIRE))Wait(1UL<<bit);
    if(created!=WORKERS){ok=FALSE;__atomic_store_n(&g->stop,1,__ATOMIC_RELEASE);}
    for(unsigned i=0;i<created;i++)if(workers[i].go_signal)Signal(workers[i].task,workers[i].go_signal);
    for(unsigned i=0;i<created;i++) {
        struct Worker *w=&workers[i];struct ETask *child=(struct ETask *)ChildWait(w->tid);
        if((IPTR)child<=CHILD_ACTIVE){REPORT("[SMP-Locale] FAIL unreaped; retaining context\n");for(;;)Wait(0);}
        ChildFree(child->et_UniqueID);
        if(!w->done||w->error||w->first!=w->cpu||w->last!=w->cpu)ok=FALSE;
        if(g->phase==0 && (w->loads!=ROUNDS||w->conversions!=ROUNDS||!w->node||w->versions!=1))ok=FALSE;
        if(g->phase==1 && ((i==0 && w->switches!=32)||(i>0 && (!w->conversions||w->seen!=3))))ok=FALSE;
        if(w->begin>begin)begin=w->begin;if(w->end<end)end=w->end;
        REPORT("[SMP-Locale] worker phase=%lu cpu=%lu first=%lu last=%lu node=%p loads=%lu conversions=%lu switches=%lu seen=%lu versions=%lu error=%lu begin=%llu end=%llu reaped=1\n",
            (unsigned long)g->phase,(unsigned long)w->cpu,(unsigned long)w->first,(unsigned long)w->last,w->node,
            (unsigned long)w->loads,(unsigned long)w->conversions,(unsigned long)w->switches,(unsigned long)w->seen,
            (unsigned long)w->versions,(unsigned long)w->error,(unsigned long long)w->begin,(unsigned long long)w->end);
    }
    FreeSignal(bit);if(end<=begin)ok=FALSE;
    REPORT("[SMP-Locale] phase=%lu created=%lu overlap=%llu result=%s\n",(unsigned long)g->phase,
        (unsigned long)created,(unsigned long long)(end>begin?end-begin:0),ok?"PASS":"FAIL");
    return ok;
}
int main(int argc,char **argv) {
 LONG rc=RETURN_FAIL;struct Locale *saved=NULL;struct Group group={0};
 struct Worker loading[WORKERS]={0},switching[WORKERS]={0};char path[80]={0};
 BOOL wrote=FALSE,restored=FALSE;
 if(argc!=2)return RETURN_FAIL;
 LocaleBase=(struct LocaleBase *)OpenLibrary("locale.library",51);if(!LocaleBase)return RETURN_FAIL;
 KernelBase=OpenResource("kernel.resource");
 if(!KernelBase||KrnGetCPUCount()!=8||KrnGetCPUNumber()!=0||!(FindTask(NULL)->tc_Flags&TF_ETASK))goto cleanup;
 saved=OpenLocale(NULL);group.a=OpenLocale(argv[1]);group.b=OpenLocale(argv[1]);
 if(!saved||!group.a||!group.b)goto cleanup;
 if(strcmp(group.a->loc_LanguageName,"english.language")||strcmp(group.b->loc_LanguageName,"english.language"))goto cleanup;
 group.prefs=argv[1];snprintf(path,sizeof(path),"RAM:SMP-Locale-%lu.catalog",(unsigned long)FindTask(NULL)->tc_UnionETask.tc_ETask->et_UniqueID);
 group.catalog=path;BPTR file=Open(path,MODE_NEWFILE);if(!file)goto cleanup;
 wrote=TRUE;LONG written=Write(file,catalog_bytes,sizeof(catalog_bytes));BOOL closed=Close(file);
 if(written!=sizeof(catalog_bytes)||!closed)goto cleanup;
 REPORT("[SMP-Locale] start workers=4 rounds=32 a=%p b=%p saved=%p\n",group.a,group.b,saved);
 if(!wave(&group,loading))goto restore;
 BOOL same=TRUE;for(unsigned i=1;i<WORKERS;i++)if(loading[i].node!=loading[0].node)same=FALSE;
 REPORT("[SMP-Locale] catalog same=%lu version=7\n",(unsigned long)same);
 if(!same)goto restore;
 CloseLocale(LocalePrefsUpdate(group.a));group.phase=1;
 if(wave(&group,switching))rc=RETURN_OK;
restore:
 CloseLocale(LocalePrefsUpdate(saved));
 struct Locale *check=OpenLocale(NULL);restored=check==saved;if(check)CloseLocale(check);
 if(!restored)rc=RETURN_FAIL;
 REPORT("[SMP-Locale] result=%s restored=%lu\n",rc==RETURN_OK?"PASS":"FAIL",(unsigned long)restored);
cleanup:
 for(unsigned i=0;i<WORKERS;i++)if(loading[i].node)CloseCatalog(loading[i].node);
 if(group.a)CloseLocale(group.a);if(group.b)CloseLocale(group.b);if(saved)CloseLocale(saved);
 if(wrote)DeleteFile(path);
 CloseLibrary((struct Library *)LocaleBase);return rc;
}
