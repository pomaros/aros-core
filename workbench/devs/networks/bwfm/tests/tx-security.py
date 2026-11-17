#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Exercise the production TX port gate and buffer bounds on HOST.

--reject REV must fail the same assertions. No radio/firmware claim.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
SOURCE = 'workbench/devs/networks/bwfm/bwfm_dev.c'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--reject')
a = p.parse_args()

PRELUDE = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t UBYTE;
typedef uint16_t UWORD;
typedef uint32_t ULONG;
typedef int BOOL;
#define ETH_ALEN 6
#define ETH_HLEN 14
#define ETH_FRAME_LEN 1518
#define SANA2IOF_RAW 1
#define S2ERR_OUTOFSERVICE 1
#define S2WERR_UNIT_OFFLINE 2
#define S2ERR_NO_RESOURCES 3
#define S2WERR_BUFF_ERROR 4
#define S2ERR_TX_FAILURE 5
#define S2WERR_GENERIC_ERROR 6
#define S2EVENT_ERROR 1
#define S2EVENT_SOFTWARE 2
#define S2EVENT_BUFF 4
#define S2EVENT_TX 8
#define S2EVENT_HARDWARE 16
#define D(x)
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define AROS_WORD2BE(x) __builtin_bswap16((uint16_t)(x))
#else
#define AROS_WORD2BE(x) ((uint16_t)(x))
#endif
#define CopyMem(s,d,n) memcpy((d),(s),(n))
struct bwfm_ethhdr { UBYTE h_dest[6],h_source[6]; UWORD h_proto; };
struct bwfm_opener { BOOL (*tx)(void *,void *,ULONG); };
struct bwfm_unit {
 int online,joined,keys_pending,lock;
 UBYTE hwaddr[6]; struct { ULONG PacketsSent; } stats;
};
struct bwfm_tracker { struct { ULONG PacketsSent,BytesSent; } stats; };
struct IOSana2Req {
 struct { int io_Error; unsigned io_Flags; } ios2_Req;
 unsigned ios2_WireError;
 void *ios2_BufferManagement,*ios2_Data;
 UBYTE ios2_DstAddr[6]; ULONG ios2_PacketType,ios2_DataLength;
};
static unsigned copies,sends;
static int copy_ok;
static BOOL copy_tx(void *d,void *s,ULONG n) {
 copies++; if(!copy_ok)return 0; assert(n<=ETH_FRAME_LEN);memcpy(d,s,n);return 1;
}
static int BWFMTxData(void *p,ULONG n) {(void)p;assert(n>=14&&n<=1518);sends++;return 0;}
static void ObtainSemaphore(int *p) {assert(!*p);*p=1;}
static void ReleaseSemaphore(int *p) {assert(*p);*p=0;}
static void report_events(struct bwfm_unit *u,unsigned e) {(void)u;(void)e;}
static struct bwfm_tracker *find_tracker(struct bwfm_unit *u,ULONG t) {(void)u;(void)t;return NULL;}
'''

CHECKS = r'''
static void check(int online,int joined,int pending,int raw,unsigned type,
                  ULONG length,int hook_ok,int error,unsigned expected_copies) {
 UBYTE data[ETH_FRAME_LEN]={0};
 struct bwfm_opener opener={copy_tx};
 struct bwfm_unit unit={.online=online,.joined=joined,.keys_pending=pending};
 struct IOSana2Req req={0};
 data[12]=(UBYTE)(type>>8);data[13]=(UBYTE)type;
 req.ios2_BufferManagement=&opener;req.ios2_Data=data;
 req.ios2_DataLength=length;req.ios2_PacketType=raw?0x888e:type;
 req.ios2_Req.io_Flags=raw?SANA2IOF_RAW:0;
 copies=sends=0;copy_ok=hook_ok;
 tx_request(&unit,&req);
 assert(req.ios2_Req.io_Error==error);
 assert(copies==expected_copies && sends==(unsigned)(error==0));
 assert(unit.stats.PacketsSent==sends && !unit.lock);
}
int main(void) {
 /* ONLINE before association must not create a firmware TX flow. */
 check(1,0,0,0,0x86dd,40,1,S2ERR_OUTOFSERVICE,1);
 check(1,0,0,0,0x888e,40,1,S2ERR_OUTOFSERVICE,1);
 check(0,1,0,0,0x888e,40,1,S2ERR_OUTOFSERVICE,0);
 check(1,1,1,0,0x888e,40,1,0,1);
 check(1,1,1,0,0x0800,40,1,S2ERR_OUTOFSERVICE,1);
 check(1,1,1,1,0x888e,64,1,0,1);
 /* A RAW caller's claimed PacketType must not bypass the actual header. */
 check(1,1,1,1,0x0800,64,1,S2ERR_OUTOFSERVICE,1);
 check(1,1,0,0,0x0800,1504,1,0,1);
 check(1,1,0,1,0x86dd,1518,1,0,1);
 check(1,1,0,0,0x0800,1505,1,S2ERR_NO_RESOURCES,0);
 check(1,1,0,0,0x0800,UINT32_MAX,1,S2ERR_NO_RESOURCES,0);
 check(1,1,0,1,0x0800,13,1,S2ERR_NO_RESOURCES,0);
 check(1,1,0,1,0x0800,1519,1,S2ERR_NO_RESOURCES,0);
 check(1,1,0,0,0x0800,40,0,S2ERR_NO_RESOURCES,1);
 puts("PASS TX association, EAPOL-only handshake, RAW header, lengths and copy refusal");
}
'''

versions = [('current', (ROOT / SOURCE).read_text())]
if a.reject:
    versions.append(('negative', subprocess.check_output(
        ['git', '-C', str(ROOT), 'show', f'{a.reject}:{SOURCE}'], text=True)))
with tempfile.TemporaryDirectory(prefix='bwfm-tx-') as tmp:
    folder = Path(tmp)
    for label, source in versions:
        start = source.index('static void tx_request(')
        end = source.index('\n}\n', start) + 3
        cfile, binary = folder / f'{label}.c', folder / label
        cfile.write_text(PRELUDE + source[start:end] + CHECKS)
        subprocess.run(['clang', '-std=c11', '-O1', '-g', '-Wall', '-Wextra',
                        '-Werror', '-fsanitize=address,undefined', str(cfile),
                        '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        if label == 'current':
            assert result.returncode == 0, result.stdout + result.stderr
            print(result.stdout.strip())
        else:
            assert result.returncode < 0 and 'Assertion' in result.stderr, result
            print('PASS negative control rejects pre-association TX')
