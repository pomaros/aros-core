#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Exercise production pthread timer ownership with host sanitizers."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[4]
source = (root / 'compiler/pthread/pthread.c').read_text()


def function(signature):
    start = source.index(signature)
    return source[start:source.index('\n}\n', start) + 3]


prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef signed char BYTE;
typedef int BOOL;
typedef char *STRPTR;
#define TRUE 1
#define FALSE 0
#define NT_MESSAGE 1
#define NT_REPLYMSG 2
#define NT_MSGPORT 3
#define PA_SIGNAL 0
#define SIGB_TIMER_FALLBACK 31
#define SIGF_TIMER_FALLBACK (1UL << SIGB_TIMER_FALLBACK)
#define TIMERNAME "timer.device"
#define UNIT_MICROHZ 0
#define DB2(x)
struct Task { int unused; };
struct Node { void *ln_Succ, *ln_Pred; unsigned char ln_Type; BYTE ln_Pri; STRPTR ln_Name; };
struct List { void *head, *tail, *tailpred; };
struct MsgPort {
    struct Node mp_Node;
    unsigned char mp_Flags, mp_SigBit;
    struct Task *mp_SigTask;
    struct List mp_MsgList;
    uint64_t mp_SpinLock;
};
struct Message { struct Node mn_Node; struct MsgPort *mn_ReplyPort; unsigned short mn_Length; };
struct IORequest {
    struct Message io_Message;
    void *io_Device, *io_Unit;
    unsigned short io_Command;
    unsigned char io_Flags;
    signed char io_Error;
};
struct timerequest { struct IORequest tr_node; unsigned long secs, usecs; };
#define NEWLIST(list) memset((list), 0, sizeof(*(list)))
static unsigned opens, closes, aborts, waits, freed;
static int fail_open, fallback_signal;
static BYTE AllocSignal(int requested) { (void)requested; return fallback_signal ? -1 : 3; }
static void FreeSignal(int sig) { assert(sig == 3); ++freed; }
static void SetSignal(unsigned long a, unsigned long b) { (void)a; (void)b; }
static int OpenDevice(STRPTR name, unsigned unit, struct IORequest *io, unsigned flags)
{
    (void)name; (void)unit; (void)flags; ++opens;
    io->io_Device = fail_open ? NULL : (void *)(uintptr_t)0x1000;
    io->io_Unit = (void *)(uintptr_t)0x2000;
    io->io_Message.mn_Node.ln_Type = NT_MESSAGE;
    return fail_open;
}
static void CloseDevice(struct IORequest *io) { assert(io->io_Device); ++closes; }
static int CheckIO(struct IORequest *io) { return io->io_Message.mn_Node.ln_Type == NT_REPLYMSG; }
static void AbortIO(struct IORequest *io)
{
    assert(io->io_Message.mn_Node.ln_Pred && io->io_Message.mn_Node.ln_Succ);
    ++aborts;
}
static void WaitIO(struct IORequest *io) { ++waits; io->io_Message.mn_Node.ln_Type = NT_REPLYMSG; }
static struct timerequest timer_shared_io;
static BOOL timer_shared_ok;
'''
suffix = r'''
int main(void)
{
    struct timerequest request;
    struct MsgPort port;
    struct Task task;
    memset(&port, 0xa5, sizeof(port));
    assert(OpenTimerDevice(&request.tr_node, &port, &task));
    assert(!port.mp_SpinLock && port.mp_SigTask == &task);
    CloseTimerDevice(&request.tr_node);
    assert(opens == 1 && closes == 1 && !aborts && !waits && freed == 1);
    fail_open = 1;
    assert(!OpenTimerDevice(&request.tr_node, &port, &task));
    CloseTimerDevice(&request.tr_node);
    assert(opens == 2 && closes == 1 && !aborts && freed == 2);
    fail_open = 0;
    timer_shared_ok = TRUE;
    timer_shared_io.tr_node.io_Device = (void *)(uintptr_t)0x3000;
    timer_shared_io.tr_node.io_Unit = (void *)(uintptr_t)0x4000;
    for (unsigned i = 0; i < 1000; ++i)
    {
        memset(&port, 0xa5, sizeof(port));
        assert(OpenTimerDevice(&request.tr_node, &port, &task));
        assert(!port.mp_SpinLock);
        assert(request.tr_node.io_Device == timer_shared_io.tr_node.io_Device);
        if (i & 1)
        {
            request.tr_node.io_Message.mn_Node.ln_Type = 0;
            request.tr_node.io_Message.mn_Node.ln_Pred = &port;
            request.tr_node.io_Message.mn_Node.ln_Succ = &task;
        }
        CloseTimerDevice(&request.tr_node);
    }
    assert(opens == 2 && closes == 1 && aborts == 500 && waits == 500 && freed == 1002);
    fallback_signal = 1;
    assert(OpenTimerDevice(&request.tr_node, &port, &task));
    CloseTimerDevice(&request.tr_node);
    assert(freed == 1002 && opens == 2 && closes == 1);
    puts("pthread-timer-lifecycle PASS idle,failed-open,pending,shared-ownership,1000-reopens,SMP-port-zeroing");
    return 0;
}
'''
production = function('BOOL OpenTimerDevice(') + function('void CloseTimerDevice(')
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    for label, code in [('production', production),
                        ('unsent-negative', production.replace('= NT_REPLYMSG;', '= NT_MESSAGE;'))]:
        path = directory / (label + '.c')
        executable = directory / label
        path.write_text(prefix + code + suffix)
        subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        str(path), '-o', str(executable)], check=True)
        result = subprocess.run([str(executable)], capture_output=True, text=True)
        if label == 'production':
            if result.returncode:
                raise SystemExit(result.stdout + result.stderr)
            print(result.stdout.strip())
        else:
            assert result.returncode != 0, 'unsent-request mutation was accepted'
            print('pthread-timer-lifecycle negative-control PASS')
