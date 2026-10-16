/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL), Version 1.1.
 * Native keymap loading, conversion and coherent switching on pinned APs.
 * No storage writes. Context/code survive until every child is reaped. */
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/kernel.h>
#include <proto/keymap.h>
#include <proto/kms.h>
#include <aros/debug.h>
#include <string.h>
#include <stdint.h>

APTR KernelBase;
struct Library *KeymapBase;
struct KMSLibrary *KMSBase;
#define WORKERS 4
#define ROUNDS 128
#define REPORT(...) do { Printf(__VA_ARGS__); bug(__VA_ARGS__); } while (0)
struct Group { ULONG phase, stop; struct KeyMap *italian, *french; };
struct Worker {
    struct Group *group;
    struct Task *parent, *task;
    ULONG parent_signal, go_signal, cpu, index, tid, first, last, done, error;
    ULONG loads, conversions, switches, seen;
    uint64_t begin, end;
    struct KeyMapNode *node;
};
static uint64_t counter(void) {
    uint64_t value; __asm__ volatile("mrs %0,cntvct_el0":"=r"(value)); return value;
}
static BOOL convert(struct KeyMap *map) {
    UBYTE pairs[4]; char output;
    LONG count=MapANSI("aA",2,(STRPTR)pairs,2,map);
    if(count!=2) return FALSE;
    for(unsigned i=0;i<2;i++) {
        struct InputEvent event={0};event.ie_Class=IECLASS_RAWKEY;
        event.ie_Code=pairs[i*2];event.ie_Qualifier=pairs[i*2+1];
        if(MapRawKey(&event,&output,1,map)!=1 || output!="aA"[i]) return FALSE;
    }
    return TRUE;
}
static LONG Work(void) {
    struct Task *self=FindTask(NULL);struct Worker *w=self->tc_UserData;
    struct Group *g=w->group;BYTE bit=AllocSignal(-1);
    w->task=self;w->first=KrnGetCPUNumber();
    if(bit>=0){w->go_signal=1UL<<bit;SetSignal(0,w->go_signal);}
    __atomic_store_n(&w->tid,self->tc_UnionETask.tc_ETask->et_UniqueID,__ATOMIC_RELEASE);
    Signal(w->parent,w->parent_signal);
    if(bit<0){w->error=1;goto done;}
    Wait(w->go_signal);w->begin=counter();
    if(g->phase==0) {
        w->node=OpenKeymap("DEVS:Keymaps/pc105_i");
        if(!w->node){w->error=2;goto finished;}
        for(unsigned i=0;i<ROUNDS;i++) {
            if(OpenKeymap("pc105_i")!=w->node){w->error=3;break;}
            w->loads++;
            if(!convert(&w->node->kn_KeyMap)){w->error=4;break;}
            w->conversions++;
        }
    } else if(w->index==0) {
        for(unsigned i=0;i<64;i++) {
            ConfigureKeyMapSwitcher((i&1)?g->italian:g->french,0,0x59);
            w->switches++;Delay(1);
        }
        __atomic_store_n(&g->stop,1,__ATOMIC_RELEASE);
    } else {
        uint64_t hz;__asm__ volatile("mrs %0,cntfrq_el0":"=r"(hz));
        struct InputEvent event={0};event.ie_Class=IECLASS_RAWKEY;
        event.ie_Code=w->index==1?0x59:0x20;
        while(!__atomic_load_n(&g->stop,__ATOMIC_ACQUIRE)) {
            if(w->index==1) {
                if(HandleKeyMapSwitcher(&event))w->switches++;
            } else {
                char out=0;UBYTE pairs[2];
                if(MapRawKey(&event,&out,1,g->italian)!=1 || (out!='a'&&out!='q')){w->error=5;break;}
                w->seen|=out=='a'?1:2;
                if(MapANSI("a",1,(STRPTR)pairs,1,g->italian)!=1 ||
                    pairs[1]!=0 || (pairs[0]!=0x20 && pairs[0]!=0x10)){w->error=6;break;}
                w->conversions++;
            }
            if(counter()-w->begin>hz*10){w->error=7;break;}
        }
    }
finished:
    w->end=counter();FreeSignal(bit);
done:
    w->last=KrnGetCPUNumber();__atomic_store_n(&w->done,1,__ATOMIC_RELEASE);
    return w->error?RETURN_FAIL:RETURN_OK;
}
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
        if(!CreateNewProcTags(NP_Entry,(IPTR)Work,NP_Name,(IPTR)"SMP-KeymapWorker",
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
        if((IPTR)child<=CHILD_ACTIVE){REPORT("[SMP-Keymap] FAIL unreaped; retaining context\n");for(;;)Wait(0);}
        ChildFree(child->et_UniqueID);
        if(!w->done||w->error||w->first!=w->cpu||w->last!=w->cpu)ok=FALSE;
        if(g->phase==0 && (w->loads!=ROUNDS||w->conversions!=ROUNDS||!w->node))ok=FALSE;
        if(g->phase==1 && ((i==0 && w->switches!=64)||(i==1 && !w->switches)||(i>=2 && (!w->conversions||w->seen!=3))))ok=FALSE;
        if(w->begin>begin)begin=w->begin;if(w->end<end)end=w->end;
        REPORT("[SMP-Keymap] worker phase=%lu cpu=%lu first=%lu last=%lu node=%p loads=%lu conversions=%lu switches=%lu seen=%lu error=%lu begin=%llu end=%llu reaped=1\n",
            (unsigned long)g->phase,(unsigned long)w->cpu,(unsigned long)w->first,(unsigned long)w->last,w->node,
            (unsigned long)w->loads,(unsigned long)w->conversions,(unsigned long)w->switches,(unsigned long)w->seen,
            (unsigned long)w->error,(unsigned long long)w->begin,(unsigned long long)w->end);
    }
    FreeSignal(bit);if(end<=begin)ok=FALSE;
    REPORT("[SMP-Keymap] phase=%lu created=%lu overlap=%llu result=%s\n",(unsigned long)g->phase,
        (unsigned long)created,(unsigned long long)(end>begin?end-begin:0),ok?"PASS":"FAIL");
    return ok;
}
int main(int argc,char **argv) {
    LONG rc=RETURN_FAIL;
    KeymapBase=OpenLibrary("keymap.library",43);
    KMSBase=(struct KMSLibrary *)OpenLibrary("kms.library",4);
    if(!KeymapBase||!KMSBase)goto cleanup;
    if(argc==2 && !strcmp(argv[1],"OFF")) {
        ConfigureKeyMapSwitcher(NULL,KEYMAP_SWITCH_DISABLED,KEYMAP_SWITCH_NOKEY);
        REPORT("[SMP-Keymap] hotkey=disabled\n");rc=RETURN_OK;goto cleanup;
    }
    if(argc==2 && !strcmp(argv[1],"HOTKEY")) {
        struct KeyMapNode *it=OpenKeymap("pc105_i");if(!it)goto cleanup;
        ConfigureKeyMapSwitcher(&it->kn_KeyMap,IEQUALIFIER_LALT,0x59);
        REPORT("[SMP-Keymap] hotkey=left-alt-F10 alternate=pc105_i\n");rc=RETURN_OK;goto cleanup;
    }
    if(argc!=1)goto cleanup;
    KernelBase=OpenResource("kernel.resource");
    if(!KernelBase||KrnGetCPUCount()!=8||KrnGetCPUNumber()!=0||!(FindTask(NULL)->tc_Flags&TF_ETASK))goto cleanup;
    struct KeyMapResource *registry=OpenResource("keymap.resource");if(!registry)goto cleanup;
    ObtainSemaphore(&registry->kr_Semaphore);
    BOOL absent=FindName(&registry->kr_List,"pc105_i")==NULL;
    ReleaseSemaphore(&registry->kr_Semaphore);
    REPORT("[SMP-Keymap] start workers=4 rounds=128 absent=%lu\n",(unsigned long)absent);
    if(!absent)goto cleanup;
    ConfigureKeyMapSwitcher(NULL,KEYMAP_SWITCH_DISABLED,KEYMAP_SWITCH_NOKEY);
    struct Group group={0};struct Worker loading[WORKERS]={0},switching[WORKERS]={0};
    if(!wave(&group,loading))goto restore;
    ULONG matches=0;BOOL equal=TRUE;
    ObtainSemaphore(&registry->kr_Semaphore);
    for(struct Node *n=registry->kr_List.lh_Head;n->ln_Succ;n=n->ln_Succ)
        if(!strcmp(n->ln_Name,"pc105_i"))matches++;
    ReleaseSemaphore(&registry->kr_Semaphore);
    for(unsigned i=1;i<WORKERS;i++)if(loading[i].node!=loading[0].node)equal=FALSE;
    REPORT("[SMP-Keymap] registry count=%lu same=%lu\n",(unsigned long)matches,(unsigned long)equal);
    if(matches!=1||!equal)goto restore;
    struct KeyMapNode *fr=OpenKeymap("pc105_f");if(!fr)goto restore;
    group.phase=1;group.italian=&loading[0].node->kn_KeyMap;group.french=&fr->kn_KeyMap;
    if(wave(&group,switching))rc=RETURN_OK;
restore:
    ConfigureKeyMapSwitcher(NULL,KEYMAP_SWITCH_DISABLED,KEYMAP_SWITCH_NOKEY);
    REPORT("[SMP-Keymap] result=%s restored=disabled\n",rc==RETURN_OK?"PASS":"FAIL");
cleanup:
    if(KMSBase)CloseLibrary(&KMSBase->kms_Lib);
    if(KeymapBase)CloseLibrary(KeymapBase);
    return rc;
}
