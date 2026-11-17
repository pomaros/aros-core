#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER.
# AROS Public License 1.1
"""Run the production executeLine body against controlled DOS outcomes.

HOST evidence only. --reject REV must fail the same assertions, demonstrating
that a previous successful command cannot hide a loader/preparation failure.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]
SOURCE = 'workbench/c/Shell/Shell.c'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reject')
args = parser.parse_args()

PRELUDE = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef intptr_t LONG;
typedef uintptr_t ULONG, BPTR;
typedef int BOOL;
typedef signed char BYTE;
typedef char TEXT, *STRPTR;
#define TRUE 1
#define FALSE 0
#define BNULL 0
#define BADDR(x) ((void *)(uintptr_t)(x))
#define MEMF_ANY 0
#define FILE_MAX 256
#define CLI_DEFAULTSTACK_UNIT 4
#define RETURN_OK 0
#define RETURN_FAIL 20
#define ERROR_NO_FREE_STORE 103
#define ERROR_OBJECT_WRONG_TYPE 212
#define ERROR_OBJECT_NOT_FOUND 205
#define ERROR_INVALID_COMPONENT_NAME 210
#define SHARED_LOCK 0
#define DOS_FIB 0
#define LV_VAR 0
#define SIGBREAKF_CTRL_C (1u << 12)
#define SIGBREAKF_CTRL_D (1u << 13)
#define D(x)
typedef struct {char command[258];} ShellState;
struct CommandLineInterface {LONG cli_ReturnCode; ULONG cli_DefaultStack; BPTR cli_Module;};
struct Segment {BPTR seg_Seg;};
struct Process {struct {struct {STRPTR ln_Name;} tc_Node; ULONG tc_SigAlloc;} pr_Task;};
struct FileInfoBlock {LONG fib_DirEntryType;};
static struct CommandLineInterface cli;
static struct Process process;
static int oom, loaded, directory, unloads, runs, changed_dir;
static LONG dos_error, command_rc, command_error;
static struct CommandLineInterface *Cli(void) {return &cli;}
static void *AllocVec(size_t n, int flags) {(void)flags; return oom ? NULL : calloc(1,n);}
static void FreeVec(void *p) {free(p);}
static void PrintFault(LONG e, const char *s) {(void)e; (void)s;}
static BPTR loadCommand(ShellState *s, STRPTR cmd, BPTR *script, BOOL *home, BOOL *resident) {
 (void)s; (void)cmd; (void)script; (void)home; (void)resident;
 dos_error=loaded ? 0 : ERROR_OBJECT_NOT_FOUND;
 return loaded ? 1 : 0;
}
static void SetProgramName(STRPTR s) {(void)s;}
static void *FindTask(void *p) {(void)p; return &process;}
static LONG IoErr(void) {return dos_error;}
static void SetIoErr(LONG e) {dos_error=e;}
static int NameFromLock(BPTR p, char *b, int n) {(void)p;(void)n;strcpy(b,"script");return 1;}
static void UnLock(BPTR p) {(void)p;}
static void SetSignal(ULONG v, ULONG m) {(void)v;(void)m;}
static void *FindVar(const char *s, int t) {(void)s;(void)t;return NULL;}
static ULONG AvailMem(int f) {(void)f;return 0;}
static LONG RunCommand(BPTR b, ULONG stack, char *cmd, LONG len) {
 (void)b;(void)stack;(void)cmd;(void)len;runs++;dos_error=command_error;return command_rc;
}
static void setInteractive(struct CommandLineInterface *c, ShellState *s) {(void)c;(void)s;}
static void Printf(const char *f, ...) {(void)f;}
static void AllocSignal(BYTE b) {(void)b;}
static void FreeSignal(BYTE b) {(void)b;}
static void unloadCommand(ShellState *s, BPTR p, BOOL h, BOOL r) {(void)s;(void)p;(void)h;(void)r;unloads++;}
static BPTR Lock(STRPTR s, int mode) {(void)s;(void)mode;return directory ? 2 : 0;}
static void *AllocDosObject(int type, void *tags) {(void)type;(void)tags;return calloc(1,sizeof(struct FileInfoBlock));}
static int Examine(BPTR p, struct FileInfoBlock *f) {(void)p;f->fib_DirEntryType=1;dos_error=0;return 1;}
static void FreeDosObject(int t, void *p) {(void)t;free(p);}
static void setPath(ShellState *s, BPTR p) {(void)s;(void)p;}
static BPTR CurrentDir(BPTR p) {(void)p;changed_dir++;return 3;}
'''
TESTS = r'''
static void reset(LONG previous) {
 memset(&cli,0,sizeof cli);cli.cli_ReturnCode=previous;
 oom=loaded=directory=unloads=runs=changed_dir=0;
 dos_error=command_rc=command_error=0;
}
int main(void) {
 ShellState s={"C:missing"}; LONG error;
 reset(RETURN_OK);error=executeLine(&s,"");
 assert(error==ERROR_OBJECT_NOT_FOUND && cli.cli_ReturnCode==RETURN_FAIL && runs==0);
 reset(RETURN_OK);oom=1;error=executeLine(&s,"");
 assert(error==ERROR_NO_FREE_STORE && cli.cli_ReturnCode==RETURN_FAIL && runs==0);
 reset(RETURN_FAIL);loaded=1;error=executeLine(&s,"args");
 assert(error==0 && cli.cli_ReturnCode==RETURN_OK && runs==1 && unloads==1);
 reset(RETURN_OK);loaded=1;command_rc=5;error=executeLine(&s,"");
 assert(error==0 && cli.cli_ReturnCode==5 && runs==1 && unloads==1);
 reset(RETURN_OK);loaded=1;command_rc=10;command_error=212;error=executeLine(&s,"");
 assert(error==212 && cli.cli_ReturnCode==10 && runs==1 && unloads==1);
 reset(RETURN_FAIL);directory=1;error=executeLine(&s,"");
 assert(error==0 && cli.cli_ReturnCode==RETURN_OK && runs==0 && changed_dir==1);
 puts("PASS loader refusal, allocation failure, success, warning, command error, directory shortcut");
 return 0;
}
'''

def body(text):
    start = text.index('static LONG executeLine(ShellState *ss, STRPTR commandArgs)\n{')
    return text[start:text.index('\nvoid setPath(', start)]

with tempfile.TemporaryDirectory(prefix='shell-status-') as temporary:
    directory = Path(temporary)
    versions = [('current', (ROOT / SOURCE).read_text())]
    if args.reject:
        old = subprocess.check_output(['git', '-C', str(ROOT), 'show', f'{args.reject}:{SOURCE}'], text=True)
        versions.append(('negative', old))
    for name, source in versions:
        cfile, binary = directory / f'{name}.c', directory / name
        cfile.write_text(PRELUDE + body(source) + TESTS)
        subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O1', '-g',
                        '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                        str(cfile), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        if name == 'current':
            print(result.stdout, end='')
            if result.returncode:
                raise SystemExit(result.stderr)
        else:
            assert result.returncode != 0, 'negative control unexpectedly passed'
            assert 'cli.cli_ReturnCode==RETURN_FAIL' in result.stderr, result.stderr
            print(f'PASS negative control {args.reject}: rejected stale successful return code')
