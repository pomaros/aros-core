#!/usr/bin/env python3
"""Boundaries for IDs parsed from actual CLI capture/list output."""
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
fixture = r'''
#include <assert.h>
#include <stdio.h>
#include "afscommands_parse.h"
static int token(const char *s,uint64_t *n){return afscommands_id(s,strlen(s),n);}
static int capture(const char *s,uint64_t *n){return afscommands_capture(s,strlen(s),n);}
static int list(const char *s,uint64_t n,int backup){return afscommands_list_has(s,strlen(s),n,backup);}
int main(void){
uint64_t id=0;
const char *bad[]={"","0","0x0","0x","-1","+1"," 1","1 ","1junk","1\n", "0xG", "18446744073709551616", "0x10000000000000000"};
size_t i;
assert(token("18446744073709551615",&id)&&id==UINT64_MAX);
assert(token("0xffffffffffffffff",&id)&&id==UINT64_MAX);
assert(token("0X8000000000000000",&id)&&id==UINT64_C(0x8000000000000000));
assert(token("010",&id)&&id==10);
for(i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(!token(bad[i],&id));
assert(capture("AFSPlusBackup: captured snapshot 18446744073709551615\n",&id)&&id==UINT64_MAX);
assert(capture("AFSPlusBackup: captured snapshot 0x8000000000000000\r\n",&id)&&id==UINT64_C(0x8000000000000000));
assert(!capture("prefix AFSPlusBackup: captured snapshot 5\n",&id));
assert(!capture("AFSPlusBackup: captured snapshot 5\n6\n",&id));
assert(!capture("AFSPlusBackup: captured snapshot 18446744073709551616\n",&id));
assert(list("snapshot 10\nsnapshot 110\nsnapshot 18446744073709551615\n",10,1)==1);
assert(list("snapshot 110\n",10,1)==0);
assert(list("snapshot 10\nsnapshot 10\n",10,1)==-1);
assert(list("10 generation 0\n18446744073709551615 generation 18446744073709551615\n",UINT64_MAX,0)==1);
assert(list("10 generation 0\n",1,0)==0);
assert(list("10 generation 0 garbage\n",10,0)==-1);
assert(list("10 generation 18446744073709551616\n",10,0)==-1);
assert(list("snapshot 10\nAFSPlusBackup: error 103\n",10,1)==-1);
assert(list("",10,1)==0);
puts("AFSCommands capture/list parser PASS full64-bit decimal+hex, overflow/sign/trailing/duplicate refusal");
return 0;}
'''
with tempfile.TemporaryDirectory(prefix="afscommands-parse-") as directory:
    directory = Path(directory)
    source = directory / "fixture.c"
    source.write_text(fixture)
    executable = directory / "fixture"
    subprocess.run(["clang", "-Wall", "-Wextra", "-Werror", "-g", "-fsanitize=address,undefined",
                    "-I" + str(here), str(source), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
