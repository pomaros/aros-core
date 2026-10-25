/*
 * Copyright (C) 2026 John Knipper
 * Licensed under the AROS Public License (APL) Version 1.1
 */
#include <exec/types.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/keyring.h>
#include <proto/rexxsyslib.h>
#include <rexx/storage.h>
#include <rexx/rxslib.h>
#include <libraries/keyring.h>
#include <dos/dostags.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "secretprobe.h"
#include "probeexport.h"

/* This exercises production ownership and sealing. Leak-corpus qualification
 * belongs to the host verifier, which must inspect four real artifacts. */
static void erase(void *p,size_t n) { volatile unsigned char *b=p; while(n--) *b++=0; }
static int sealed_file(const unsigned char *marker,size_t length)
{
    FILE *f=fopen("HOME:Keyring.vault","rb");
    unsigned char *b; size_t n,i; int ok=0;
    if(!f) return 0;
    b=malloc(512*1024); if(!b) { fclose(f); return 0; }
    n=fread(b,1,512*1024,f);
    ok=n>length && fgetc(f)==EOF && !ferror(f);
    for(i=0;ok && i+length<=n;i++) if(!memcmp(b+i,marker,length)) ok=0;
    erase(b,n); free(b); if(fclose(f)) ok=0; return ok;
}
static int rexx_transcript(void)
{
    struct RxsLib *RexxSysBase=(struct RxsLib *)OpenLibrary("rexxsyslib.library",0);
    struct MsgPort *reply=NULL,*host=NULL; struct RexxMsg *message=NULL;
    FILE *transcript=NULL; const char *request="FETCH acceptance-marker";
    int valid=0,refused=0,ok=0;
    if(!RexxSysBase) return 0;
    reply=CreateMsgPort(); if(!reply) goto done;
    message=CreateRexxMsg(reply,NULL,"KEYRING"); if(!message) goto done;
    message->rm_Action=RXCOMM|RXFF_RESULT;
    message->rm_Args[0]=(IPTR)CreateArgstring(request,strlen(request));
    if(!message->rm_Args[0]) goto done;
    valid=IsRexxMsg(message)!=0;
    /* There is no ARexx secret interface. Observe actual transport refusal;
     * no interpreter or command reply is simulated by this test. */
    Forbid(); host=FindPort("KEYRING"); Permit();
    refused=valid && !host;
    transcript=fopen(KR_PROOF_REXX,"w");
    if(!transcript) { kr_probe_printf("[KEYRINGPROBE] stage=rexx-output result=FAIL ioerr=%ld\n",(long)IoErr());goto done; }
    fprintf(transcript,"surface=ARexx-protocol\ninterpreter=absent\nrequest=%s\nvalid-rexx-message=%d\nhost-port=%s\nsent=0\nrefused=%d\nsecret-bytes-returned=0\n",request,valid,host?"present":"absent",refused);
    ok=refused && !ferror(transcript);
    if(fclose(transcript)) ok=0;
    transcript=NULL;
done:
    if(transcript) fclose(transcript);
    if(message) {
        if(message->rm_Args[0]) DeleteArgstring((STRPTR)message->rm_Args[0]);
        DeleteRexxMsg(message);
    }
    if(reply) DeleteMsgPort(reply);
    CloseLibrary((struct Library *)RexxSysBase); return ok;
}
int main(int argc,char **argv)
{
    struct Library *KeyringBase=OpenLibrary(KEYRINGNAME,1);
    const char *name="acceptance-marker";
    unsigned char marker[40],out[64]; ULONG n=sizeof(out); int rc=20,locked=0,cross=0,stored=0,fetch=0,del=0,sealed=0,rexx=0,exported=0;
    FILE *json;
    struct secret_probe recorder;
    int input_qualified=0; ULONG recorder_events=0;
    int crash=argc>0 && (strstr(FilePart(argv[0]),"Crash") || strstr(FilePart(argv[0]),"crash"));
    if(!KeyringBase) { kr_probe_printf("[KEYRINGPROBE] result=FAIL reason=library-unavailable\n"); return 20; }
    if(argc>1 && !strcmp(argv[1],"PEER")) {
        int result=KR_Fetch(name,out,&n);
        int denied=result==KR_ERR_OWNER || result==KR_ERR_NOTFOUND;
        BPTR f=Open("RAM:keyring-peer.result",MODE_NEWFILE);
        if(f) { Write(f,denied?"1":"0",1); Close(f); }
        erase(out,sizeof(out)); CloseLibrary(KeyringBase); return denied?0:20;
    }
    if(!kr_probe_directory(KR_PROOF_DIR)) {
        kr_probe_printf("[KEYRINGPROBE] result=FAIL reason=proof-directory ioerr=%ld\n",(long)IoErr());
        CloseLibrary(KeyringBase);return 20;
    }
    KR_Lock();
    locked=KR_Fetch(name,out,&n)==KR_ERR_LOCKED;
    /* Explicit interactive unlock accepts no passphrase argument. */
    input_qualified=secret_probe_begin(&recorder);
    kr_probe_printf("[KEYRINGPROBE] unlock-requested=1\n"); fflush(stdout);
    if(KR_Unlock()==0) {
        recorder_events=secret_probe_end(&recorder);
        ULONG i; char command[1100]; BPTR peer,exe; char path[1024];
        for(i=0;i<sizeof(marker);i++) marker[i]=(unsigned char)('A'+(i*7+3)%26);
        stored=KR_Store(name,marker,sizeof(marker))==0;
        n=sizeof(out); fetch=KR_Fetch(name,out,&n)==0 && n==sizeof(marker) && !memcmp(out,marker,n);
        /* Harness installs the exact probe binary under a second path. The
         * path changes the derived owner even when the executable hash agrees. */
        exe=Lock("PROGDIR:keyringpeer",ACCESS_READ);
        if(exe) {
            if(NameFromLock(exe,path,sizeof(path))) {
                snprintf(command,sizeof(command),"\"%s\" PEER",path);
                DeleteFile("RAM:keyring-peer.result");
                if(SystemTags(command,SYS_Asynch,FALSE,TAG_DONE)==0) {
                    char result=0; peer=Open("RAM:keyring-peer.result",MODE_OLDFILE);
                    if(peer) { cross=Read(peer,&result,1)==1 && result=='1'; Close(peer); }
                }
            }
            UnLock(exe);
        }
        sealed=sealed_file(marker,sizeof(marker));
        rexx=rexx_transcript();
        exported=kr_probe_export("HOME:Keyring.vault","sealed.vault") &&
            kr_probe_export(KR_PROOF_REXX,"keyring-rexx.log") &&
            kr_probe_export_envarc();
        if(!crash) { del=KR_Delete(name)==0; KR_Lock(); }
        rc=stored&&fetch&&cross&&locked&&(del||crash)&&sealed&&input_qualified&&!recorder_events&&exported&&rexx?0:20;
    }
    if(recorder.request) recorder_events=secret_probe_end(&recorder);
    if(!crash) { erase(marker,sizeof(marker)); erase(out,sizeof(out)); }
    json=fopen(KR_PROOF_JSON,"w");
    if(json) {
        fprintf(json,"{\"schema\":1,\"authority\":\"containment\",\"library\":true,\"stored\":%d,\"fetch\":%d,\"cross_owner_refused\":%d,\"locked_refused\":%d,\"delete\":%d,\"sealed_file\":%d,\"forced_crash_requested\":%d,\"arexx_protocol_refused\":%d,\"arexx_interpreter\":\"absent\",\"corpora_exported\":%d,\"input_injection_refused\":%d,\"recorder_positive_control\":%d,\"recorder_password_events\":%lu,\"api_result\":\"%s\",\"SAFE6\":\"UNVERIFIED\",\"leak_corpora_required\":[\"logs\",\"ENVARC\",\"ARexx\",\"forced-crash\"]}\n",stored,fetch,cross,locked,del,sealed,crash,rexx,exported,recorder.injection_refused,recorder.positive_control,(unsigned long)recorder_events,rc?"FAIL":"PASS");
        if(fclose(json)) rc=20;
    } else { kr_probe_printf("[KEYRINGPROBE] stage=json-output result=FAIL ioerr=%ld\n",(long)IoErr());rc=20; }
    if(!kr_probe_export(KR_PROOF_JSON,"keyring.json")) rc=20;
    else if(exported) kr_probe_export_summary();
    if(crash && !rc) {
        kr_probe_printf("[KEYRINGPROBE] crash-requested=1 authority=containment dump=diagnostic\n");
        fflush(stdout);
        /* Deliberate exception with the vault unlocked and fetched marker
         * live in caller memory. This tests the OS diagnostic dump corpus. */
        __asm__ volatile("" : : "r"(marker), "r"(out) : "memory");
        __builtin_trap();
    }
    erase(marker,sizeof(marker)); erase(out,sizeof(out));
    kr_probe_printf("[KEYRINGPROBE] result=%s authority=containment stored=%d cross-owner-refused=%d locked-refused=%d SAFE6=UNVERIFIED\n",rc?"FAIL":"PASS",stored,cross,locked);
    CloseLibrary(KeyringBase); return rc;
}
