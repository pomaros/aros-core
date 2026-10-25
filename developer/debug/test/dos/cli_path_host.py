#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# AROS Public License 1.1
"""Exercise the production CLI path-copy/replace/free blocks with counted locks.

The negative control removes the release in CliInitRun, matching the historical
CreateNewProc(NP_Cli)->CliInitRun asynchronous launch. Host evidence only.
"""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[4]
dos = root / 'rom/dos'
source = (dos / 'createnewproc.c').read_text()
functions = source[source.index('void internal_FreePath('):source.index('void internal_ChildWait(', source.index('void internal_FreePath('))]
newcli = (dos / 'newcliproc.c').read_text()
start = newcli.index('        /* CreateNewProc(NP_Cli)')
end = newcli.index('\n    } else {', start)
replace = newcli[start:end]
create = source[source.index('        if (defaults[22].ti_Data != TAGDATA_NOT_SPECIFIED)'):source.index('\n    /* NP_Input */')]
create = create[:create.rfind('\n    }')]
freedos = (dos / 'freedosobject.c').read_text()
assert 'internal_FreePath(path, DOSBase);' in freedos
harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
typedef uintptr_t BPTR;
#define BNULL 0
#define BADDR(x) ((void *)(uintptr_t)(x))
#define MKBADDR(x) ((BPTR)(uintptr_t)(x))
#define MEMF_CLEAR 1
#define TAGDATA_NOT_SPECIFIED ((uintptr_t)-1)
struct DosLibrary { int unused; };
struct CommandLineInterface { BPTR cli_CommandDir; };
static unsigned locks, allocations, dup_calls, alloc_calls;
static unsigned fail_dup, fail_alloc;
static void *AllocVec(size_t n, int flags) {
 (void)flags;
 if (++alloc_calls == fail_alloc) return NULL;
 void *p = calloc(1,n); assert(p); allocations++; return p;
}
static void FreeVec(void *p) { if(p) { assert(allocations); allocations--; free(p); } }
static BPTR DupLock(BPTR p) {
 if (++dup_calls == fail_dup || locks == 1024) return BNULL;
 assert(p); locks++; return p;
}
static void UnLock(BPTR p) { if(p) { assert(locks); locks--; } }
'''
harness += functions
harness += '\nstatic void launch(struct CommandLineInterface *cli, struct CommandLineInterface *oldcli, struct DosLibrary *DOSBase) {\n'
harness += 'struct {uintptr_t ti_Data;} defaults[23] = {{0}}; BPTR oldpath = oldcli->cli_CommandDir; defaults[22].ti_Data = TAGDATA_NOT_SPECIFIED;\n'
harness += create + '\n' + replace + '\n}\n'
harness += r'''
int main(int argc,char **argv) {
 (void)argv;
 struct DosLibrary *DOSBase = NULL;
 BPTR nodes[11][2];
 for(unsigned i=0;i<11;i++) { nodes[i][0] = i==10 ? 0 : MKBADDR(nodes[i+1]); nodes[i][1] = i+1; }
 struct CommandLineInterface parent = {MKBADDR(nodes)};
 unsigned good=0;
 for(unsigned i=0;i<256;i++) {
  struct CommandLineInterface child = {0};
  launch(&child,&parent,DOSBase);
  internal_FreePath(child.cli_CommandDir,DOSBase);
  if(locks == 0 && allocations == 0) good++;
  if(argc>1 && locks>=1024) break;
 }
 if(argc>1) { printf("negative-control retained_locks=%u retained_nodes=%u good_launches=%u\n",locks,allocations,good); assert(locks==1024); return 0; }
 assert(good==256 && locks==0 && allocations==0);
 /* Reused boot CLI owns its path; never free then copy from itself. */
 struct CommandLineInterface self = {internal_CopyPath(parent.cli_CommandDir,DOSBase)};
 struct CommandLineInterface *cli=&self, *oldcli=&self;
'''
harness += replace
harness += r'''
 assert(locks==11); internal_FreePath(self.cli_CommandDir,DOSBase);
 /* Allocation/DupLock failure at each list position retains no orphan node. */
 for(unsigned i=1;i<=11;i++) {
  fail_dup=dup_calls+i;
  BPTR p=internal_CopyPath(parent.cli_CommandDir,DOSBase);
  internal_FreePath(p,DOSBase); assert(locks==0 && allocations==0); fail_dup=0;
  fail_alloc=alloc_calls+i;
  p=internal_CopyPath(parent.cli_CommandDir,DOSBase);
  internal_FreePath(p,DOSBase); assert(locks==0 && allocations==0); fail_alloc=0;
 }
 puts("PASS 256 async CLI launches; self CLI; 22 path-allocation/lock failures: no retained locks/nodes");
}
'''
with tempfile.TemporaryDirectory(prefix='cli-path-host-') as td:
    p = Path(td)
    for name, text in [('fixed', harness), ('negative', harness.replace('            internal_FreePath(cli->cli_CommandDir, DOSBase);', ''))]:
        c=p/(name+'.c'); c.write_text(text)
        binary=p/name
        subprocess.run([os.environ.get('CC','clang'),'-std=c11','-g','-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-o',str(binary)],check=True)
        subprocess.run([str(binary)]+(['negative'] if name=='negative' else []),check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
