#!/usr/bin/env python3
"""Actual helper nesting/bounds with deterministic local timer injection.

Host model only. Architecture IRQ/SVC behavior and actual guest records
must additionally be reviewed and observed in the VM.
"""
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned char UBYTE;
static unsigned nesting,pending,disable_calls,enable_calls;
static char output[4096];static size_t used;
void Disable(void){nesting++;disable_calls++;}
void Enable(void){assert(nesting);nesting--;enable_calls++;if(!nesting&&pending){memcpy(output+used,"[KT]\n",5);used+=5;pending=0;}}
void RawPutChar(UBYTE c){if(!nesting){memcpy(output+used,"[KT]\n",5);used+=5;}else pending=1;output[used++]=(char)c;}
#include "afsprobe_report.h"
int main(void){
const char *record="[AFSCOMMANDS] result=PASS\n";
char bound[384];memset(bound,'x',sizeof(bound));bound[383]='\n';
afsprobe_debug_record(record,strlen(record));assert(!memcmp(output,record,strlen(record)));assert(!memcmp(output+strlen(record),"[KT]\n",5));assert(!nesting&&disable_calls==1&&enable_calls==1);
used=pending=0;nesting=2;afsprobe_debug_record(record,strlen(record));assert(nesting==2&&used==strlen(record));assert(!memcmp(output,record,used));assert(disable_calls==2&&enable_calls==2);
used=pending=0;nesting=0;afsprobe_debug_record(bound,sizeof(bound));assert(!memcmp(output,bound,sizeof(bound))&&used==389);
used=pending=0;afsprobe_debug_record(NULL,385);assert(strstr(output,"[AFSPROBE] result=FAIL reason=debug-record-bound\n")==output);assert(!nesting&&disable_calls==enable_calls);
puts("AFS probe production helper host PASS complete local records, nested exclusion,384byte bound, oversized explicitFAIL");return 0;}
'''
with tempfile.TemporaryDirectory(prefix="afsprobe-report-") as directory:
    directory = Path(directory)
    (directory / "proto").mkdir()
    (directory / "proto/exec.h").write_text("#pragma once\n")
    source = directory / "fixture.c"
    source.write_text(fixture)
    executable = directory / "fixture"
    subprocess.run(["clang", "-Wall", "-Wextra", "-Werror", "-g", "-fsanitize=address,undefined",
                    "-I" + str(directory), "-I" + str(here), str(source), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
