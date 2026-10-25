#!/usr/bin/env python3
# Copyright (C) 2026 John Knipper
# Licensed under the AROS Public License (APL) Version 1.1
"""Compile the production secret-input command and capture paths under sanitizers."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
source = (root / 'rom/devs/input/processevents.c').read_text()
clear = source[source.index('static void ClearSecretInput'):source.index('/* Complete the private keyboard request')]
reset = source[source.index('static void ResetSecretKeyboard'):source.index('/* Forwards a chain')]
mask = source[source.index('        if (InputDevice->SecretOwner)\n', source.index('VOID ForwardQueuedEvents')):source.index('        ForeachNode', source.index('VOID ForwardQueuedEvents'))]
watchdog = source[source.index('            if (InputDevice->SecretOwner && ++InputDevice->SecretTicks'):source.index('            timer_ie.ie_NextEvent', source.index('An abandoned owner'))]
repeat_guard = source[source.index('        if (!InputDevice->Stopped && !InputDevice->SecretOwner &&'):source.index('        {', source.index('        if (!InputDevice->Stopped && !InputDevice->SecretOwner &&'))]
commands = source[source.index('                ioreq->io_Error = 0;'):source.index('                case CMD_STOP:')]
capture = source[source.index('            if (InputDevice->SecretOwner)\n', source.index('wakeupsigs & kbdsig')):source.index('            /* Add event to queue */\n            AddEQTail(kbdie', source.index('wakeupsigs & kbdsig'))]
code = r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint8_t UBYTE; typedef unsigned long ULONG;
#define TRUE 1
#define FALSE 0
#define NULLPORT ((void*)0)
#define CMD_CLEAR 9
#define KEY_QUALIFIERS 0x00ffu
#define IND_ADDEVENT 1
#define IND_WRITEEVENT 2
#define IND_SECRETBEGIN 3
#define IND_SECRETREAD 4
#define IND_SECRETEND 5
#define IOERR_UNITBUSY -1
#define IOERR_BADADDRESS -2
#define IOERR_BADLENGTH -3
struct InputEvent { struct InputEvent *ie_NextEvent; unsigned ie_Code, ie_Qualifier; };
struct MsgPort { unsigned mp_SigBit; };
struct Message { struct MsgPort *mn_ReplyPort; };
struct IORequest { int unused; };
struct IOStdReq { struct Message io_Message; int io_Command,io_Error; size_t io_Actual,io_Length; void *io_Data; };
struct inputbase { void *SecretOwner; struct InputEvent SecretEvents[32]; UBYTE SecretHead,SecretCount; int SecretOverflow; ULONG SecretTicks; int Stopped; };
static int repeats,forwards,replies;
static char order[512]; static unsigned orderlen,signals=1; static int completed;
static struct InputEvent *wipe_target;
static void step(char ch) { assert(orderlen+1<sizeof(order)); order[orderlen++]=ch; order[orderlen]=0; }
static int CheckIO(struct IORequest *r) { (void)r; step('C'); return completed; }
static void AbortIO(struct IORequest *r) { (void)r; step('A'); }
static void WaitIO(struct IORequest *r) { (void)r; step('W'); }
static void DoIO(struct IORequest *r) {
    struct IOStdReq *q=(struct IOStdReq*)r; struct InputEvent zero={0};
    assert(q->io_Command==CMD_CLEAR); assert(!memcmp(wipe_target,&zero,sizeof(zero))); step('D');
}
static unsigned SetSignal(unsigned value,unsigned mask) { unsigned old=signals; signals=(signals&~mask)|(value&mask); step('S'); return old; }
static void send_kbd(struct IOStdReq *r,struct InputEvent *event) { (void)r;(void)event; step('R'); }
static struct MsgPort kport={0};
static struct IOStdReq keyboard_request;
static struct InputEvent keyboard_event;
static void setup_reset(void) { memset(order,0,sizeof(order)); orderlen=0; signals=1; keyboard_request.io_Message.mn_ReplyPort=&kport; keyboard_event.ie_Code=0x53; wipe_target=&keyboard_event; }

#define ABORT_KEYTIMER_REQUEST repeats++
#define ForwardQueuedEvents(base) forwards++
#define ReplyMsg(msg) replies++
#define SEND_KBD_REQUEST(io,event) send_kbd(io,event)
''' + clear + reset + r'''
static int repeat_allowed(struct inputbase *InputDevice,int keyrepeat_state,unsigned wakeupsigs) {
    unsigned keytimersig=4;
''' + repeat_guard + r'''
    return 1;
    return 0;
}

static void mask_qualifiers(struct inputbase *InputDevice,struct InputEvent *ie_chain) {
''' + mask + r'''
}
static unsigned expire(struct inputbase *InputDevice) {
    struct IOStdReq *kbdio=&keyboard_request; struct InputEvent *kbdie=&keyboard_event;
    unsigned wakeupsigs=3,kbdsig=1;
''' + watchdog + r'''
    return wakeupsigs;
}

static unsigned command(struct inputbase *InputDevice, struct IOStdReq *ioreq, int keyrepeat_state) {
    struct IOStdReq *kbdio=&keyboard_request; struct InputEvent *kbdie=&keyboard_event; unsigned wakeupsigs=3,kbdsig=1;
    (void)kbdio; (void)kbdie; (void)wakeupsigs; (void)kbdsig;
    do {
''' + commands + r'''
    default: break;
    }
    } while (0);
    return wakeupsigs;
}
static void physical(struct inputbase *InputDevice,struct InputEvent *kbdie) {
    struct IOStdReq *kbdio=&keyboard_request; (void)kbdio;
    do {
''' + capture + r'''
    } while (0);
}
int main(void) {
    struct inputbase b={0}; struct IOStdReq q={0}; struct InputEvent event={0},out={0};
    b.SecretOwner=(void*)1; assert(!repeat_allowed(&b,1,4));
    b.SecretOwner=0; assert(!repeat_allowed(&b,0,4));
    assert(repeat_allowed(&b,1,4));
    setup_reset(); completed=0; ResetSecretKeyboard(&keyboard_request,&keyboard_event);
    assert(!strcmp(order,"CAWDSR") && !signals);
    setup_reset(); completed=1; ResetSecretKeyboard(&keyboard_request,&keyboard_event);
    assert(!strcmp(order,"CWDSR") && !signals);
    struct InputEvent chain2={0},chain1={0}; chain1.ie_NextEvent=&chain2;
    chain1.ie_Qualifier=chain2.ie_Qualifier=0xfff;
    mask_qualifiers(&b,&chain1); assert(chain1.ie_Qualifier==0xfff);
    b.SecretOwner=(void*)1; mask_qualifiers(&b,&chain1);
    assert(chain1.ie_Qualifier==0xf00 && chain2.ie_Qualifier==0xf00);
    b.SecretTicks=98; setup_reset(); assert(expire(&b)==3 && b.SecretTicks==99 && b.SecretOwner);
    assert(!orderlen); assert(expire(&b)==2 && !b.SecretOwner && !b.SecretTicks);
    assert(!strcmp(order,"CWDSR") && !signals);
    setup_reset();
    q.io_Message.mn_ReplyPort=(void*)1; q.io_Command=IND_SECRETBEGIN;
    assert(command(&b,&q,1)==2); assert(!q.io_Error && b.SecretOwner==(void*)1 && repeats==1 && forwards==1);
    command(&b,&q,0); assert(q.io_Error==IOERR_UNITBUSY);
    q.io_Command=IND_ADDEVENT; command(&b,&q,0); assert(q.io_Error==IOERR_UNITBUSY && replies==1);
    q.io_Command=IND_WRITEEVENT; command(&b,&q,0); assert(q.io_Error==IOERR_UNITBUSY && replies==2);
    assert(!b.SecretCount && forwards==1);
    event.ie_Code=42; physical(&b,&event); assert(b.SecretCount==1 && event.ie_Code==0);
    q.io_Command=IND_SECRETREAD; q.io_Data=&out; q.io_Length=sizeof(out);
    q.io_Message.mn_ReplyPort=(void*)2; command(&b,&q,0); assert(q.io_Error==IOERR_BADADDRESS && b.SecretCount==1);
    q.io_Message.mn_ReplyPort=(void*)1; q.io_Length=1; command(&b,&q,0); assert(q.io_Error==IOERR_BADLENGTH && b.SecretCount==1);
    q.io_Length=sizeof(out); command(&b,&q,0); assert(!q.io_Error && q.io_Actual==sizeof(out) && out.ie_Code==42 && !b.SecretCount);
    assert(b.SecretEvents[0].ie_Code==0); command(&b,&q,0); assert(!q.io_Error && q.io_Actual==0);
    for(int i=0;i<33;i++) { event.ie_Code=i+1; physical(&b,&event); }
    assert(b.SecretOverflow && b.SecretCount==32);
    command(&b,&q,0); assert(q.io_Error==IOERR_BADLENGTH);
    q.io_Command=IND_SECRETEND; setup_reset(); assert(command(&b,&q,0)==2); assert(!strcmp(order,"CWDSR") && !signals); assert(!q.io_Error && !b.SecretOwner && !b.SecretOverflow && !b.SecretCount);
    struct InputEvent zero={0}; for(int i=0;i<32;i++) assert(!memcmp(&b.SecretEvents[i],&zero,sizeof(zero)));
    q.io_Command=IND_SECRETREAD; command(&b,&q,0); assert(q.io_Error==IOERR_BADADDRESS);
    puts("secret-input ASan/UBSan PASS: physical capture, injection refusal, ownership, bounds, overflow, wipe, keyboard abort/completion/clear/signal/reissue ordering, stale wakeup removal, qualifier masking, watchdog expiry");
}
'''
with tempfile.TemporaryDirectory(prefix='input-secret-') as tmp:
    c = Path(tmp)/'test.c'; c.write_text(code)
    binary = Path(tmp)/'test'
    subprocess.run(['clang','-std=c99','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(c),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})

    # The same extracted fixture must reject missing signal clearing and
    # missing modifier masking at runtime. No production source is changed.
    controls = {
        'signal-clear': ('SetSignal(0, 1UL << request->io_Message.mn_ReplyPort->mp_SigBit);', '(void)SetSignal;'),
        'qualifier-mask': ('event->ie_Qualifier &= ~KEY_QUALIFIERS;', '(void)event;'),
    }
    for label, (original, replacement) in controls.items():
        assert original in code, label
        c.write_text(code.replace(original, replacement))
        subprocess.run(['clang','-std=c99','-Wall','-Wextra','-Werror','-g',
                        '-fsanitize=address,undefined','-fno-omit-frame-pointer',
                        str(c),'-o',str(binary)],check=True)
        negative = subprocess.run([str(binary)],capture_output=True,text=True,
                                  env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0',
                                       'UBSAN_OPTIONS':'halt_on_error=1'})
        assert negative.returncode != 0 and 'Assertion failed' in negative.stderr, label
        print('secret-input negative control rejected:', label)
