#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Exercise production FAT EXAMINE_NEXT with modeled DOS helpers."""
import argparse, hashlib, json, subprocess
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[4]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',required=True,type=Path);a=p.parse_args();out=a.out.resolve()
 if out.exists() or ROOT==out or ROOT in out.parents:p.error('use a fresh output outside source')
 out.mkdir(parents=True);source=(ROOT/'rom/filesys/fat/packet.c').read_text();case=source.split('case ACTION_EXAMINE_NEXT:',1)[1].split('case ACTION_FH_FROM_LOCK:',1)[0].strip()
 mutations=[('force-child-lock','metadata_lock.gl = node;','metadata_lock.gl = NULL;'),('wrong-directory','node->dir_cluster == fl->ioh.first_cluster &&','1 &&'),('wrong-entry','node->dir_entry == dh.cur_index','1'),('free-borrowed','if (lock)\n                    FreeLock(lock, glob);','FreeLock(metadata_lock.gl ? &metadata_lock : lock, glob);'),('lost-cursor','fib->fib_DiskKey = dh.cur_index;','fib->fib_DiskKey = 9;'),('lost-eof','err = ERROR_NO_MORE_ENTRIES;','err = ERROR_OBJECT_NOT_FOUND;'),('permission-change','metadata_lock.gl = node;','metadata_lock.gl = node; node->access = SHARED_LOCK;')]
 variants=[('actual',case)]
 for name,old,new in mutations:
  assert old in case,name
  variants.append((name,case.replace(old,new)))
 rows=[]
 for name,body in variants:
  d=out/name;d.mkdir();(d/'case.inc').write_text(body+'\n');exe=d/'test';cmd=['clang','-g','-fsanitize=address,undefined','-I'+str(d),str(HERE/'test.c'),'-o',str(exe)]
  r=subprocess.run(cmd,capture_output=True,text=True,timeout=30);(d/'build.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
  r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10);(d/'output.txt').write_text(r.stdout+r.stderr);rows.append({'case':name,'exit':r.returncode});assert (r.returncode==0)==(name=='actual'),rows
 result={'result':'HOST_PASS','scope':'production EXAMINE_NEXT case with modeled DOS calls; Native execution is separate','packet_source_sha256':hashlib.sha256(source.encode()).hexdigest(),'cases':rows};(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('HOST_PASS actual + '+str(len(rows)-1)+' semantic negative controls')
if __name__=='__main__':main()
