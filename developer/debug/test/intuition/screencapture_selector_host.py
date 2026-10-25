#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Exercise the production USB SCREEN_OPEN handler and Exec spawn command."""
from pathlib import Path
import os
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[4]
ENGINE = ROOT / 'arch/aarch64-apple/usb/dwc3dev'


def function(source, name):
    start = source.index('static int ' + name + '(')
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


HARNESS = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "usblink_service.h"

typedef intptr_t BPTR;
typedef int32_t LONG;
typedef int8_t BYTE;
typedef const char *CONST_STRPTR;
enum { MODE_OLDFILE=1, MODE_NEWFILE, SYS_Input, SYS_Output, SYS_Asynch, TAG_END };
#define TRUE 1
struct Task { struct { BYTE ln_Pri; } tc_Node; } task;
static BYTE client_priority=7;
static unsigned opened, started;
static char last_command[400];
static BPTR Open(CONST_STRPTR name, LONG mode)
{ assert(!strcmp(name,"NIL:")); assert(mode==1 || mode==2); opened++; return 1; }
static void Close(BPTR p) { assert(p==1); }
static struct Task *FindTask(void *p) { assert(!p); return &task; }
static BYTE SetTaskPri(struct Task *t, BYTE pri)
{ BYTE old=t->tc_Node.ln_Pri; t->tc_Node.ln_Pri=pri; return old; }
static LONG SystemTags(CONST_STRPTR command, ...)
{ assert(task.tc_Node.ln_Pri==7); started++; strcpy(last_command,command); return 0; }
static void note(const char *line) { assert(strstr(line,"SERVICE start")); }
ENV_SPAWN

static size_t io_read(void *ctx,uint8_t *p,size_t n)
{ (void)ctx;(void)p;(void)n;return 0; }
static size_t io_space(void *ctx) { (void)ctx;return 0; }
static void io_write(void *ctx,const uint8_t *p,size_t n)
{ (void)ctx;(void)p;(void)n; }
static void reply(void *ctx,struct usblink_request *r) { (void)ctx;(void)r; }
static uint64_t now(void *ctx) { (void)ctx;return 10; }
static uint8_t ring[USBLINK_STREAM_BUFFER];
static struct usblink link;
static struct usblink_service service;
static char selected[USBLINK_SCREEN_MAX+1];
static unsigned spawn_calls;
static int spawn_result=1;
static int spawn(void *ctx,uint32_t op,const char *argument,uint32_t token)
{
    (void)ctx;
    assert(op==USBLINK_OP_SCREEN && token);
    spawn_calls++;
    strcpy(selected,argument);
    return spawn_result && env_spawn(NULL,op,argument,token);
}
static void fresh(void)
{
    struct usblink_io io={io_read,io_space,io_write,NULL};
    struct usblink_service_env env={spawn,reply,now,NULL};
    usblink_init(&link,&io,1);
    usblink_service_init(&service,&link,&env,ring,9);
    link.session=0x11;
    spawn_calls=opened=started=0;
    selected[0]=last_command[0]=0;
    task.tc_Node.ln_Pri=-2;
}
static uint32_t u32(unsigned at)
{
    const uint8_t *p=link.out+at;
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void screen(const uint8_t *payload,uint32_t n,int valid)
{
    fresh();
    assert(link.service(link.service_ctx,&link,USBLINK_SCREEN_OPEN,payload,n)==USBLINK_HANDLED);
    assert(link.out_len && link.out[5]==USBLINK_OPEN_ACK);
    assert(u32(USBLINK_HEADER)==USBLINK_OP_SCREEN);
    assert(u32(USBLINK_HEADER+4)==(valid?USBLINK_OPEN_STARTED:USBLINK_OPEN_BAD_ARGUMENT));
    assert(spawn_calls==(unsigned)valid && started==(unsigned)valid && opened==(unsigned)(2*valid));
    if(valid) {
        char expected[400];
        assert(strlen(selected)==n && !memcmp(selected,payload,n));
        assert(service.op==USBLINK_OP_SCREEN && service.token && task.tc_Node.ln_Pri==-2);
        snprintf(expected,sizeof expected,"C:ScreenCapture TO RAM:usblink-screen-%08lx.ppm EXPORT %lu",
                 (unsigned long)service.token,(unsigned long)service.token);
        if(n) { strcat(expected," SCREEN \"");strcat(expected,selected);strcat(expected,"\""); }
        assert(!strcmp(last_command,expected));
    } else assert(!service.op && !service.token);
}
int main(void)
{
    uint8_t payload[128];
    unsigned i,valid;
    screen((const uint8_t *)"",0,1);
    screen((const uint8_t *)"ScaleLegacy_200.test-1",22,1);
    for(i=0;i<256;i++) {
        payload[0]=(uint8_t)i;
        valid=(i>='A'&&i<='Z')||(i>='a'&&i<='z')||(i>='0'&&i<='9')||i=='_'||i=='.'||i=='-';
        screen(payload,1,valid);
    }
    memset(payload,'A',sizeof payload);
    screen(payload,127,1);
    screen(payload,128,0);
    payload[1]=0;screen(payload,127,0);
    payload[1]=';';screen(payload,127,0);
    payload[1]=0x80;screen(payload,127,0);
    /* Oversize rejection occurs before touching even one payload byte. */
    fresh();
    assert(link.service(link.service_ctx,&link,USBLINK_SCREEN_OPEN,NULL,UINT32_MAX)==USBLINK_HANDLED);
    assert(!spawn_calls && u32(USBLINK_HEADER+4)==USBLINK_OPEN_BAD_ARGUMENT);
    /* Busy/backpressure and spawn failures preserve the existing lifecycle. */
    fresh();service.op=USBLINK_OP_EXPORT;service.session=link.session;
    link.service(link.service_ctx,&link,USBLINK_SCREEN_OPEN,(const uint8_t *)"X",1);
    assert(!spawn_calls && u32(USBLINK_HEADER+4)==USBLINK_OPEN_BUSY);
    fresh();service.out_len=1;
    assert(link.service(link.service_ctx,&link,USBLINK_SCREEN_OPEN,(const uint8_t *)"X",1)==USBLINK_BUSY);
    assert(!spawn_calls);
    fresh();spawn_result=0;
    link.service(link.service_ctx,&link,USBLINK_SCREEN_OPEN,(const uint8_t *)"X",1);
    assert(spawn_calls==1 && !service.op && u32(USBLINK_HEADER+4)==USBLINK_OPEN_SPAWN_FAILED);
    /* Defense at the actual Shell boundary is independent of the wire guard. */
    fresh();assert(!env_spawn(NULL,USBLINK_OP_SCREEN,"unsafe;name",1));assert(!opened && !started);
    {
        char too_long[129];memset(too_long,'a',128);too_long[128]=0;
        assert(!env_spawn(NULL,USBLINK_OP_SCREEN,too_long,1));assert(!opened && !started);
    }
    puts("screen-selector production handler + spawn result=PASS exhaustive_bytes=256 bounds=127/128 lifecycle=PASS");
    return 0;
}
'''


def main():
    source = (ENGINE / 'usblink_service.c').read_text()
    spawn = function((ENGINE / 'usbdev_service.c').read_text(), 'env_spawn')
    cc = os.environ.get('CC', 'clang')
    with tempfile.TemporaryDirectory(prefix='screen-selector-') as temp:
        temp = Path(temp)
        harness = temp / 'test.c'
        harness.write_text(HARNESS.replace('ENV_SPAWN', spawn))
        units = [ENGINE / name for name in ('usblink.c', 'usblink_sha256.c', 'usblink_input.c')]

        def run(text, label):
            unit = temp / 'service.c'
            unit.write_text(text)
            exe = temp / label
            subprocess.run([cc, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-iquote', str(ENGINE), str(harness), str(unit),
                            *map(str, units), '-o', str(exe)], check=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            if label == 'production':
                print(result.stdout, end='')
                if result.returncode:
                    raise RuntimeError(result.stderr)
            elif result.returncode == 0:
                raise AssertionError('negative control survived: ' + label)
            else:
                print('negative-control', label, 'rejected')

        run(source, 'production')
        old = "if (!usblink_screen_ok(p, n))"
        assert old in source
        run(source.replace(old, 'if (n != 0)'), 'old-empty-only')
        run(source.replace('start(s, USBLINK_OP_SCREEN, screen);',
                           'start(s, USBLINK_OP_SCREEN, "");'), 'selector-discarded')
        # Guard bypass must be caught in the protocol path, before spawn.
        run(source.replace(old, 'if (n > USBLINK_SCREEN_MAX)'), 'unsafe-byte-allowed')
        print('screen-selector host ASan/UBSan result=PASS negative_controls=3')


if __name__ == '__main__':
    main()
