#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Exercise the prefs bytes consumed by the live indicator service."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
fixture = r'''#include <assert.h>
#include "workbench/system/Indicators/prefs.h"
int main(void) {
 unsigned char bytes[INDICATORS_PREFS_SIZE + 1]; unsigned mask, value;
 for (mask=0; mask<=INDICATORS_ALL_ENABLED; mask++) {
  IndicatorsPrefsEncode(bytes,mask); value=99;
  assert(IndicatorsPrefsDecode(bytes,INDICATORS_PREFS_SIZE,&value));
  assert(value==mask);
 }
 IndicatorsPrefsEncode(bytes,5);
 for (unsigned n=0;n<=sizeof bytes;n++) if(n!=INDICATORS_PREFS_SIZE) {
  value=99; assert(!IndicatorsPrefsDecode(bytes,n,&value)); assert(value==99);
 }
 for(unsigned i=0;i<4;i++) {
  unsigned char old=bytes[i]; bytes[i]='?'; value=99;
  assert(!IndicatorsPrefsDecode(bytes,INDICATORS_PREFS_SIZE,&value)); assert(value==99);
  bytes[i]=old;
 }
 bytes[4]=16; value=99;
 assert(!IndicatorsPrefsDecode(bytes,INDICATORS_PREFS_SIZE,&value)); assert(value==99);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
 source=Path(directory)/'prefs.c'; binary=Path(directory)/'prefs'
 source.write_text(fixture)
 subprocess.run(['clang','-fsanitize=address,undefined','-I',str(root),str(source),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
print('indicators-prefs result=PASS masks=16 invalid=length,magic,mask unchanged-on-reject=1')
