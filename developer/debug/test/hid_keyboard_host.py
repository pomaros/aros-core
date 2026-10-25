#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1
"""Exercise actual VM USB keyboard decoder/routing with narrow HIDD adapters."""
import os,pathlib,re,subprocess,tempfile,json,hashlib
root=pathlib.Path(__file__).resolve().parents[3]; source=(root/'rom/usb/classes/hid/hidkeyboard.c').read_text()
def function(name):
 m=re.search(r'(?m)^(?:static )?(?:BOOL|int|void) '+name+r'\(',source);assert m,name
 start=m.start();brace=source.index('{',m.end());depth=1;i=brace+1
 while depth:
  if source[i]=='{':depth+=1
  if source[i]=='}':depth-=1
  i+=1
 return source[start:i]
head=r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
typedef uint8_t UBYTE;typedef uint16_t UWORD;typedef uint32_t ULONG;typedef int32_t LONG;typedef uintptr_t IPTR;typedef int BOOL;typedef void OOP_Object;typedef void OOP_Class;
#define TRUE 1
#define FALSE 0
#define RPF_MAIN_VARIABLE 2
#define RPF_MAIN_CONST 1
#define RPF_MAIN_RELATIVE 4
#define REPORT_MAIN_INPUT 128
#define IECODE_UP_PREFIX 128
#define aHidd_Name (attribute_base+1)
#define aHidd_HardwareName (attribute_base+2)
#define TAG_DONE 0
struct TagItem {IPTR a,b;};
struct NepHidItem {ULONG nhi_Type,nhi_Flags,nhi_Usage,nhi_MapSize,*nhi_UsageMap,nhi_Offset,nhi_Size,nhi_Count;LONG nhi_LogicalMin,nhi_LogicalMax;BOOL nhi_IsSigned;};
struct Node {struct NepHidReport *ln_Succ;};
struct NepHidReport {struct Node nhr_Node;ULONG nhr_ReportID,nhr_InItemCount,nhr_ReportInSize;struct NepHidItem **nhr_InItemMap;UBYTE nhr_PhysicalKeys[32];};
struct NepHidBase {void *nh_KeyboardHW,*nh_KeyboardClass;};
struct NepClassHid {struct NepHidBase *nch_ClsBase;void *nch_KeyboardDevice;ULONG nch_MaxInSize;struct {void *lh_Head;} nch_HidReports;};
struct pHidd_Kbd_Event {UWORD flags,code;};
static unsigned attribute_base;static unsigned nevents,registered,removed;static UWORD events[256];static int backend=1;
static void Disable(void){}static void Enable(void){}
static BOOL keyboard_init(struct NepHidBase *b){if(backend)attribute_base=1000;return backend;}
static void *HW_AddDriver(void *a,void *b,struct TagItem *t){assert(t[0].a==1001&&t[1].a==1002);registered++;return (void *)1;}
static void HW_RemoveDriver(void *a,void *b){removed++;}
static void HW_Input_PushEvent(void *a,void *b,struct pHidd_Kbd_Event *e){assert(a&&b);assert(e->flags==0);assert(nevents<256);events[nevents++]=e->code;}
'''
table=source[source.index('static const UBYTE physical_keymap'):];table=table[:table.index('};')+2]
body='\n'.join(function(n) for n in ['nKeyboardItem','keyboard_bits','keyboard_decode'])+'\n'+table+'\n'+'\n'.join(function(n) for n in ['keyboard_raw_state','keyboard_push','keyboard_diff','nKeyboardReport','nKeyboardDetach'])
tests=r'''
int main(void){
 struct NepHidBase base={(void *)1,(void *)1};struct NepClassHid dev={.nch_ClsBase=&base,.nch_MaxInSize=8};
 struct NepHidReport r={0},other={0},end={0};struct NepHidItem mods[8]={{0}},array={0},nonkey={0};struct NepHidItem *items[10];ULONG map[102];UBYTE bytes[8]={0};BOOL valid;ULONG v;unsigned i;
 for(i=0;i<102;i++)map[i]=0x70000+i;
 for(i=0;i<8;i++){mods[i]=(struct NepHidItem){.nhi_Type=128,.nhi_Flags=2,.nhi_Usage=0x700e0+i,.nhi_Offset=i,.nhi_Size=1,.nhi_LogicalMax=1};items[i]=&mods[i];}
 array=(struct NepHidItem){.nhi_Type=128,.nhi_MapSize=102,.nhi_UsageMap=map,.nhi_Offset=16,.nhi_Size=8,.nhi_Count=6,.nhi_LogicalMax=101};items[8]=&array;
 r.nhr_InItemMap=items;r.nhr_InItemCount=9;r.nhr_ReportInSize=64;r.nhr_Node.ln_Succ=&other;other.nhr_Node.ln_Succ=&end;dev.nch_HidReports.lh_Head=&r;
 /* Bounds: exact end, bitpastend, width, length; no read is permitted. */
 assert(!keyboard_bits(bytes,1,9,1,&v));assert(!keyboard_bits(bytes,1,8,1,&v));assert(!keyboard_bits(bytes,1,7,2,&v));assert(!keyboard_bits(bytes,1,0,33,&v));assert(!keyboard_bits(bytes,0,0,1,&v));assert(keyboard_bits(bytes,1,7,1,&v));
 bytes[0]=2;bytes[2]=4;assert(nKeyboardReport(&dev,&r,bytes,8,&valid)&&valid);assert(registered==1&&nevents==2&&events[0]==0x60&&events[1]==0x20);
 assert(nKeyboardReport(&dev,&r,bytes,8,&valid)&&valid&&nevents==2); /* held != repeat */
 /* Mixed report: a valid nonkeyboard field may proceed, a span beyond
  * received bytes must prevent every legacy composite action. */
 nonkey=(struct NepHidItem){.nhi_Type=128,.nhi_Flags=2,.nhi_Usage=0x90001,.nhi_Offset=8,.nhi_Size=1,.nhi_LogicalMax=1};items[9]=&nonkey;r.nhr_InItemCount=10;
 assert(nKeyboardReport(&dev,&r,bytes,8,&valid)&&valid&&nevents==2);
 nonkey.nhi_Offset=64;assert(nKeyboardReport(&dev,&r,bytes,8,&valid)&&!valid&&nevents==2);r.nhr_InItemCount=9;
 bytes[2]=5;assert(nKeyboardReport(&dev,&r,bytes,8,&valid)&&valid);assert(nevents==4&&events[2]==0xa0&&events[3]==0x35);
 bytes[2]=1;assert(nKeyboardReport(&dev,&r,bytes,8,&valid)&&!valid&&nevents==4); /* rollover atomic */
 bytes[2]=6;assert(nKeyboardReport(&dev,&r,bytes,3,&valid)&&!valid&&nevents==4); /* truncated claimed */
 array.nhi_Offset=65;assert(nKeyboardReport(&dev,&r,bytes,8,&valid)&&!valid);array.nhi_Offset=16;
 array.nhi_Count=0;assert(nKeyboardReport(&dev,&r,bytes,8,&valid)&&!valid);array.nhi_Count=6;
 array.nhi_IsSigned=1;assert(nKeyboardReport(&dev,&r,bytes,8,&valid)&&!valid);array.nhi_IsSigned=0;
 bytes[2]=255;assert(nKeyboardReport(&dev,&r,bytes,8,&valid)&&!valid&&nevents==4);
 /* Array duplicate usages produce one key; OS handles repeating. */
 bytes[2]=5;bytes[3]=5;assert(nKeyboardReport(&dev,&r,bytes,8,&valid)&&valid&&nevents==4);
 memset(bytes,0,8);assert(nKeyboardReport(&dev,&r,bytes,8,&valid)&&valid);assert(nevents==6&&events[4]==0xe0&&events[5]==0xb5);
 /* Two HID reports/right+left Control aliases must not release each other. */
 bytes[0]=1;assert(nKeyboardReport(&dev,&r,bytes,8,&valid)&&valid);assert(events[nevents-1]==0x63);
 other.nhr_InItemMap=items;other.nhr_InItemCount=9;other.nhr_ReportInSize=64;
 bytes[0]=16;assert(nKeyboardReport(&dev,&other,bytes,8,&valid)&&valid);i=nevents;
 bytes[0]=0;assert(nKeyboardReport(&dev,&r,bytes,8,&valid)&&valid&&nevents==i);
 assert(nKeyboardReport(&dev,&other,bytes,8,&valid)&&valid&&nevents==i+1&&events[i]==0xe3);
 /* Non-keyboard report is unclaimed and keeps legacy route. */
 for(i=0;i<8;i++)mods[i].nhi_Usage=0x90001+i;for(i=0;i<102;i++)map[i]=0x90000+i;
 assert(!nKeyboardReport(&dev,&r,bytes,8,&valid)&&!valid);
 /* Missing HIDD backend never falls back to keyboard action injection. */
 for(i=0;i<102;i++)map[i]=0x70000+i;backend=0;dev.nch_KeyboardDevice=NULL;bytes[2]=4;
 assert(nKeyboardReport(&dev,&r,bytes,8,&valid)&&valid);backend=1;
 assert(nKeyboardReport(&dev,&r,bytes,8,&valid)&&valid);i=nevents;nKeyboardDetach(&dev);assert(nevents==i+1&&events[i]==0xa0&&removed==1&&dev.nch_KeyboardDevice==NULL);
 puts("PASS production USB physical keyboard report/hold/rollover/bounds/aliases/disconnect");return 0;
}
'''
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
with tempfile.TemporaryDirectory(prefix='hid-keyboard-host-') as td:
 p=pathlib.Path(td); c=p/'test.c';binary=p/'test';c.write_text(head+body+tests)
 flags=['clang','-std=c11','-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-Wno-incompatible-pointer-types','-Wno-pointer-sign']
 subprocess.run(flags+[str(c),'-o',str(binary)],check=True);subprocess.run([str(binary)],env=env,check=True)
 for name,old,new in [('bounds','offset/8>=length','offset/8>length'),('rollover','if(usage>=1 && usage<=3) return 0;','if(usage>=1 && usage<=3) continue;'),('classification','if(!claimed) return FALSE;','if(!claimed) return TRUE;')]:
  assert old in body;c.write_text(head+body.replace(old,new)+tests);subprocess.run(flags+[str(c),'-o',str(binary)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
  result=subprocess.run([str(binary)],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE);assert result.returncode!=0,name
 # Recreate former first-device tag construction before obtaining attr bases.
 mutated=body.replace('if(!keyboard_init(nh)) return TRUE;','',1).replace('nch->nch_KeyboardDevice=HW_AddDriver','if(!keyboard_init(nh)) return TRUE; nch->nch_KeyboardDevice=HW_AddDriver',1)
 c.write_text(head+mutated+tests);subprocess.run(flags+[str(c),'-o',str(binary)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
 result=subprocess.run([str(binary)],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE);assert result.returncode!=0,'first-device attribute bases'
 start=body.index('    /* The legacy non-keyboard decoder');end=body.index('    *valid=TRUE;',start)
 mutated=body[:start]+body[end:]
 c.write_text(head+mutated+tests);subprocess.run(flags+[str(c),'-o',str(binary)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
 result=subprocess.run([str(binary)],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE);assert result.returncode!=0,'mixed report span'
 print('PASS five compiled runtime negative controls rejected')
s=(root/'rom/usb/classes/hid/hid.class.c').read_text();start=s.index('/* Only the successful interrupt-IN');assert s.index('nKeyboardReport',start)<s.index('nParseWacom',start);assert 'if(nKeyboardItem(nhi)) return FALSE;' in s
assert 'physical_valid && field<' in s and 'continue;' in s[start:s.index('if(nch->nch_WacomMode)',start)]
print(json.dumps({'result':'PASS','source_sha256':hashlib.sha256(source.encode()).hexdigest(),'scope':'extracted production decoder/report/diff and narrow HIDD registration adapter; no guest execution'}))
