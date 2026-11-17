#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Exercise production association queue/worker ownership and secret lifetime."""
from pathlib import Path
import argparse,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[5]
p=argparse.ArgumentParser();p.add_argument('--reject');a=p.parse_args()
source='workbench/devs/networks/bwfm/bwfm_dev.c'
s=(subprocess.check_output(['git','show',a.reject+':'+source],cwd=ROOT,text=True) if a.reject else (ROOT/source).read_text())
def function(name):
 start=s.index('static void '+name+'(')
 # do_associate has a forward declaration before its definition.
 while s.find(';',start,s.index('{',start))>=0:start=s.index('static void '+name+'(',start+1)
 brace=s.index('{',start);i=brace+1;depth=1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[start:i]
pre=r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t UBYTE;typedef uint32_t ULONG;typedef int32_t LONG;typedef uintptr_t IPTR;typedef void *APTR;
#include <devices/sana2wireless.h>
#include <resources/bwfm.h>
#define TRUE 1
#define FALSE 0
#define ETH_ALEN 6
#define S2ERR_BAD_ARGUMENT 5
#define S2WERR_GENERIC_ERROR 1
#define BWFM_C_DISASSOC 52
#define D(x)
#define CopyMem(s,d,n) memcpy(d,s,n)
struct bwfm_opener {int unused;};
struct bwfm_unit {
 int lock,assoc_pending,autojoin_active,autojoin_retry,joined,keys_pending;
 ULONG assoc_ssidlen,assoc_passlen,assoc_ielen,wireless_caps,held_eapol_len;
 UBYTE assoc_ssid[33],assoc_pass[64],assoc_psk[32],assoc_ie[64],assoc_bssid[6];
 int assoc_haspsk,assoc_hasbssid;struct bwfm_opener *assoc_opener;
};
struct IOSana2Req {void *ios2_Data,*ios2_BufferManagement;struct {int io_Error;} ios2_Req;int ios2_WireError;};
struct pump {void *ctrl_task;ULONG ctrl_sig;};static struct pump base={(void*)1,1};static struct pump *PumpBase=&base;
static int wakes,joins,psk_joins,connects,handshake,join_result,disassocs;
static UBYTE expected_psk[32];
static void wifi_secret_wipe(void *p,size_t n){volatile UBYTE *b=p;while(n--)*b++=0;}
static int zero(const void *p,size_t n){const UBYTE *b=p;while(n--)if(*b++)return 0;return 1;}
static IPTR GetTagData(IPTR tag,IPTR def,struct TagItem *t){for(;t->ti_Tag;t++)if(t->ti_Tag==tag)return t->ti_Data;return def;}
static void ObtainSemaphore(int *p){assert(!*p);*p=1;}
static void ReleaseSemaphore(int *p){assert(*p);*p=0;}
static void Signal(void *t,ULONG s){assert(t && s);wakes++;}
static int BWFMIoctl(int cmd,int set,void *p,int n){assert(cmd==52 && set && !p && !n);disassocs++;return 0;}
static void link_down(struct bwfm_unit *u){u->joined=0;}
static void link_up(struct bwfm_unit *u,int h){u->joined=1;connects++;handshake=h;}
static int BWFMJoin(UBYTE *s,ULONG n,UBYTE *p,ULONG pn,UBYTE *ie,ULONG in,UBYTE *b){assert(n==7 && !memcmp(s,"network",7));assert(in==4 && ie[0]==48);assert(b && b[0]==2);if(pn)assert(pn==15 && !memcmp(p,"test passphrase",15));else assert(!p);joins++;return join_result;}
static int BWFMJoinPSK(UBYTE *s,ULONG n,UBYTE *p,UBYTE *ie,ULONG in,UBYTE *b){assert(n==7 && !memcmp(s,"network",7));assert(!memcmp(p,expected_psk,32));assert(in==4 && ie[0]==48);assert(b && b[0]==2);psk_joins++;return join_result;}
'''
checks=r'''
int main(void){
 struct bwfm_unit u={0};struct IOSana2Req r={0};UBYTE ie[]={48,2,1,0},bssid[6]={2};
 for(unsigned i=0;i<32;i++)expected_psk[i]=(UBYTE)(i*7);
 struct TagItem tags[]={{S2INFO_SSID,(IPTR)"network"},{S2INFO_WPAInfo,(IPTR)ie},{S2INFO_BSSID,(IPTR)bssid},{S2INFO_PSK,(IPTR)expected_psk},{TAG_END,0}};r.ios2_Data=tags;
 /* Unsupported offload must not enqueue a job or leak a key. */
 queue_associate(&u,&r);assert(r.ios2_Req.io_Error==S2ERR_BAD_ARGUMENT && !wakes && !u.assoc_pending);
 r.ios2_Req.io_Error=0;u.wireless_caps=S2WCAP_4WAY_PSK;u.joined=1;
 queue_associate(&u,&r);assert(!r.ios2_Req.io_Error && u.assoc_pending && !u.joined && wakes==1 && !memcmp(u.assoc_psk,expected_psk,32));
 memset(expected_psk,0xcc,32);assert(u.assoc_psk[0]==0);for(unsigned i=0;i<32;i++)expected_psk[i]=(UBYTE)(i*7);
 do_associate(&u);assert(psk_joins==1 && !joins && connects==1 && !handshake && !u.assoc_pending && zero(u.assoc_psk,32) && zero(u.assoc_pass,64));
 do_associate(&u);assert(psk_joins==1 && connects==1);
 /* Passphrase and host handshake are separate worker paths. */
 tags[3].ti_Tag=S2INFO_Passphrase;tags[3].ti_Data=(IPTR)"test passphrase";queue_associate(&u,&r);do_associate(&u);assert(joins==1 && connects==2 && !handshake && zero(u.assoc_pass,64));
 tags[3].ti_Tag=TAG_IGNORE;u.wireless_caps=0;queue_associate(&u,&r);do_associate(&u);assert(joins==2 && connects==3 && handshake);
 /* Resource refusal cannot publish CONNECT; queued secrets are cleared. */
 u.wireless_caps=S2WCAP_4WAY_PSK;tags[3].ti_Tag=S2INFO_PSK;tags[3].ti_Data=(IPTR)expected_psk;join_result=-1;queue_associate(&u,&r);do_associate(&u);assert(connects==3 && !u.joined && zero(u.assoc_psk,32));
 /* Cancellation wipes a not-yet-consumed secret. */
 queue_associate(&u,&r);struct TagItem cancel[]={{S2INFO_Disassociate,1},{TAG_END,0}};r.ios2_Data=cancel;queue_associate(&u,&r);assert(disassocs==1 && !u.assoc_pending && zero(u.assoc_psk,32));
 r.ios2_Data=tags;int old=wakes;char longssid[34];memset(longssid,'x',33);longssid[33]=0;tags[0].ti_Data=(IPTR)longssid;r.ios2_Req.io_Error=0;queue_associate(&u,&r);assert(r.ios2_Req.io_Error==S2ERR_BAD_ARGUMENT && wakes==old);
 tags[0].ti_Data=(IPTR)"network";tags[3].ti_Tag=S2INFO_Passphrase;tags[3].ti_Data=(IPTR)"short";r.ios2_Req.io_Error=0;queue_associate(&u,&r);assert(r.ios2_Req.io_Error==S2ERR_BAD_ARGUMENT && wakes==old);
 char longpass[65];memset(longpass,'p',64);longpass[64]=0;tags[3].ti_Data=(IPTR)longpass;r.ios2_Req.io_Error=0;queue_associate(&u,&r);assert(r.ios2_Req.io_Error==S2ERR_BAD_ARGUMENT && wakes==old);
 tags[3].ti_Tag=TAG_IGNORE;ie[1]=255;r.ios2_Req.io_Error=0;queue_associate(&u,&r);assert(r.ios2_Req.io_Error==S2ERR_BAD_ARGUMENT && wakes==old);
 assert(!u.lock);puts("bwfm association jobs PASS: 12 cases");return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
 t=Path(tmp);(t/'exec').mkdir();(t/'utility').mkdir()
 (t/'exec/types.h').write_text('')
 (t/'utility/tagitem.h').write_text('#define TAG_USER 0x80000000u\n#define TAG_END 0\n#define TAG_IGNORE 1\nstruct TagItem {uintptr_t ti_Tag,ti_Data;};\n')
 f=t/'test.c';f.write_text(pre+function('queue_associate')+'\n'+function('do_associate')+checks)
 subprocess.run(['cc','-std=c11','-O1','-g','-fsanitize=address,undefined','-I'+str(t),'-I'+str(ROOT/'compiler/include'),str(f),'-o',str(t/'test')],check=True)
 r=subprocess.run([str(t/'test')],capture_output=True,text=True)
 if a.reject:assert r.returncode,'negative unexpectedly passed';print('association-job negative rejected:',a.reject)
 else:assert not r.returncode,r.stdout+r.stderr;print(r.stdout.strip())
