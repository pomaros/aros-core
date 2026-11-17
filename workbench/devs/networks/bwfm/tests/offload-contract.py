#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Run the actual SANA-II supplicant capability/association bodies on HOST."""
from pathlib import Path
import argparse, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[5]
p=argparse.ArgumentParser();p.add_argument('--reject');a=p.parse_args()
source='workbench/network/WirelessManager/src/drivers/driver_sana2.c'
s=(subprocess.check_output(['git','show',a.reject+':'+source],cwd=ROOT,text=True)
   if a.reject else (ROOT/source).read_text())
def function(name):
    start=s.index('static int '+name+'(');brace=s.index('{',start);depth=1;i=brace+1
    while depth:
        depth+=(s[i]=='{')-(s[i]=='}');i+=1
    return s[start:i]
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef uint8_t u8; typedef uint32_t ULONG; typedef uintptr_t UPINT; typedef void *APTR;
#include <devices/sana2wireless.h>
#define MAX_SSID_LEN 32
#define PUDDLE_SIZE 2000
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 2
#define IEEE80211_MODE_IBSS 1
#define WPA_ALG_WEP 1
#define WPA_ALG_NONE 0
#define WPA_DRIVER_CAPA_ENC_WEP40 1
#define WPA_DRIVER_CAPA_ENC_WEP104 2
#define WPA_DRIVER_CAPA_ENC_TKIP 4
#define WPA_DRIVER_CAPA_ENC_CCMP 8
#define WPA_DRIVER_CAPA_KEY_MGMT_WPA 1
#define WPA_DRIVER_CAPA_KEY_MGMT_WPA_PSK 2
#define WPA_DRIVER_CAPA_KEY_MGMT_WPA2 4
#define WPA_DRIVER_CAPA_KEY_MGMT_WPA2_PSK 8
#define WPA_DRIVER_FLAGS_USER_SPACE_MLME 1
#define WPA_DRIVER_FLAGS_4WAY_HANDSHAKE 2
#define os_memset memset
#define CopyMem(s,d,n) memcpy(d,s,n)
struct IOSana2Req {struct {int io_Command;} ios2_Req;void *ios2_Data,*ios2_StatData;ULONG ios2_DataLength;};
struct wpa_driver_sana2_data {struct IOSana2Req *request;int hard_mac;char ssid[33];};
struct wpa_driver_capa {unsigned enc,key_mgmt,flags;};
struct wpa_driver_associate_params {const u8 *ssid,*bssid,*wpa_ie,*psk;const char *passphrase;size_t ssid_len,wpa_ie_len;int mode,freq,auth_alg,wep_tx_keyidx;size_t wep_key_len[4];const u8 *wep_key[4];};
static const u8 broadcast_addr[6]={255,255,255,255,255,255};
static int query_error,short_reply,alloc_fail,associate_error,seen_psk,seen_pass,seen_ie,query_count,associate_count;
static unsigned query_flags,allocations;
static u8 expected_psk[32];
static void *os_zalloc(size_t n) {if(alloc_fail)return NULL;void *p=calloc(1,n);assert(p);allocations++;return p;}
static void os_free(void *p) {if(p){assert(allocations);allocations--;free(p);}}
static void *CreatePool(unsigned a,unsigned b,unsigned c) {(void)a;(void)b;(void)c;return (void*)1;}
static void DeletePool(void *p) {(void)p;}
static struct TagItem *CloneTagItems(struct TagItem *t) {size_t n=0;while(t[n].ti_Tag!=TAG_END)n++;struct TagItem *p=malloc((n+1)*sizeof(*p));assert(p);memcpy(p,t,(n+1)*sizeof(*p));return p;}
static void FreeTagItems(struct TagItem *p) {free(p);}
static int wpa_driver_sana2_set_key(void *a,void *b,int c,const u8 *d,int e,int f,const u8 *g,size_t h,const u8 *i,size_t j) {(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;(void)j;return 0;}
static int DoIO(void *v) {
 struct IOSana2Req *r=v;
 if(r->ios2_Req.io_Command==S2_GETCRYPTTYPES){static u8 types[]={S2ENC_CCMP};r->ios2_StatData=types;r->ios2_DataLength=1;return 0;}
 if(r->ios2_Req.io_Command==S2_GETWIRELESSCAPS){struct Sana2WirelessCaps *c=r->ios2_StatData;query_count++;assert(c->SizeAvailable==sizeof(*c));c->Flags=query_flags;c->SizeSupplied=short_reply?0:sizeof(*c);return query_error;}
 assert(r->ios2_Req.io_Command==S2_SETOPTIONS);associate_count++;
 for(struct TagItem *t=r->ios2_Data;t->ti_Tag!=TAG_END;t++) {
  if(t->ti_Tag==S2INFO_PSK){assert(!memcmp((void*)t->ti_Data,expected_psk,32));seen_psk++;}
  if(t->ti_Tag==S2INFO_Passphrase){assert(!strcmp((void*)t->ti_Data,"test passphrase"));seen_pass++;}
  if(t->ti_Tag==S2INFO_WPAInfo && t->ti_Data)seen_ie++;
 }
 return associate_error;
}
'''
checks=r'''
int main(void) {
 struct IOSana2Req req={0};struct wpa_driver_sana2_data d={.request=&req,.hard_mac=1};struct wpa_driver_capa c;
 query_flags=S2WCAP_4WAY_PSK;assert(!wpa_driver_sana2_get_capa(&d,&c));assert(c.flags&WPA_DRIVER_FLAGS_4WAY_HANDSHAKE);
 query_flags=0;assert(!wpa_driver_sana2_get_capa(&d,&c));assert(!(c.flags&WPA_DRIVER_FLAGS_4WAY_HANDSHAKE));
 query_flags=S2WCAP_4WAY_PSK;query_error=-1;assert(!wpa_driver_sana2_get_capa(&d,&c));assert(!(c.flags&WPA_DRIVER_FLAGS_4WAY_HANDSHAKE));
 query_error=0;short_reply=1;assert(!wpa_driver_sana2_get_capa(&d,&c));assert(!(c.flags&WPA_DRIVER_FLAGS_4WAY_HANDSHAKE));short_reply=0;
 d.hard_mac=0;query_count=0;assert(!wpa_driver_sana2_get_capa(&d,&c));assert(c.flags==WPA_DRIVER_FLAGS_USER_SPACE_MLME && !query_count);
 u8 ie[]={48,2,1,0};for(unsigned i=0;i<32;i++)expected_psk[i]=(u8)(i*7);
 struct wpa_driver_associate_params p={.ssid=(u8*)"network",.ssid_len=7,.freq=2412,.wpa_ie=ie,.wpa_ie_len=sizeof(ie),.psk=expected_psk};
 assert(!wpa_driver_sana2_associate(&d,&p));assert(seen_psk==1 && !seen_pass && seen_ie==1 && !allocations);assert(!strcmp(d.ssid,"network"));
 p.psk=NULL;p.passphrase="test passphrase";assert(!wpa_driver_sana2_associate(&d,&p));assert(seen_pass==1 && seen_psk==1 && !allocations);
 p.passphrase=NULL;assert(!wpa_driver_sana2_associate(&d,&p));assert(seen_pass==1 && seen_psk==1 && !allocations);
 associate_error=-7;assert(wpa_driver_sana2_associate(&d,&p)==-1 && !allocations);associate_error=0;
 int count=associate_count;p.ssid_len=33;assert(wpa_driver_sana2_associate(&d,&p)==-1 && associate_count==count && !allocations);
 p.ssid_len=7;alloc_fail=1;assert(wpa_driver_sana2_associate(&d,&p)==-1 && associate_count==count && !allocations);
 puts("sana2 offload contract PASS: 11 cases");return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
 t=Path(tmp);(t/'exec').mkdir();(t/'utility').mkdir()
 (t/'exec/types.h').write_text('typedef int32_t LONG;\n')
 (t/'utility/tagitem.h').write_text('#define TAG_USER 0x80000000u\n#define TAG_IGNORE 1\n#define TAG_END 0\nstruct TagItem {uintptr_t ti_Tag,ti_Data;};\n')
 f=t/'test.c';f.write_text(pre+function('wpa_driver_sana2_associate')+'\n'+function('wpa_driver_sana2_get_capa')+checks)
 subprocess.run(['cc','-std=c11','-O1','-g','-fsanitize=address,undefined','-I'+str(t),'-I'+str(ROOT/'compiler/include'),str(f),'-o',str(t/'test')],check=True)
 result=subprocess.run([str(t/'test')],capture_output=True,text=True)
 if a.reject:
  assert result.returncode, 'negative control unexpectedly passed'
  print('sana2 offload negative control rejected:',a.reject)
 else:
  assert not result.returncode,result.stdout+result.stderr
  print(result.stdout.strip())
