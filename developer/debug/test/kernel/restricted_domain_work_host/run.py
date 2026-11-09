#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Actual protected domain-work admission with synthetic Native caller identities; HOST only."""
import argparse,hashlib,json,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[5]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve()
if out.exists() or ROOT in out.parents:p.error('fresh output outside checkout required')
source=ROOT/'arch/aarch64-apple/kernel/restricted.c';text=source.read_text();start=text.index('    if (ec==0x15 && svc==0xa56e) {');end=text.index('\n    if (ec==0x15 && r_table_transfer',start);actual=text[start:end]
guard_start=text.index('    if (!AROSAppleTaskExecution(context)->root &&');guard_end=text.index('    r_coordinate();',guard_start);guard=text[guard_start:guard_end]
defines='\n'.join(line for line in text.splitlines() if any(line.startswith('#define '+name+' ') for name in ['R_LIFETIME','R_DOMAIN_CLAIM','R_RESUME','R_DOMAIN_WORK','R_COPY','R_REAP_REFRESH']))
header='''#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <aros/apple/restricted.h>
struct Task {int id;};
struct AROSRestrictedSpace {int id;};
struct RestrictedDomain {uint64_t handle;unsigned managed,state,closing;void *owner_context;struct AROSRestrictedSpace space;};
struct frame {uint64_t x[31];};
enum {R_LIVE=2};
struct execution {unsigned root;};static struct execution execution;
static struct execution *AROSAppleTaskExecution(void *context){(void)context;return &execution;}
static int native_admits(void *context,unsigned ec,unsigned svc);
static struct RestrictedDomain domains[2];static unsigned calls;static int accepted=1;
static struct Task worker;static int own,foreign;
static struct AROSRestrictedSpace *selected;
static struct RestrictedDomain *r_domain_lookup(uint64_t handle)
{for(unsigned i=0;i<2;++i)if(domains[i].handle==handle)return &domains[i];return NULL;}
static int AROSRestrictedServiceTakeSpace(struct Task *task,void *context,struct AROSRestrictedSpace *space,struct AROSRestrictedWork *work)
{(void)work;if(task!=&worker || context!=&own)return -1;++calls;selected=space;return accepted;}
static int dispatch(struct frame *frame,struct Task *task,void *context,unsigned svc)
{unsigned ec=0x15;
'''
tests='''\nreturn 0;}
#define CHECK(x,n) do{if(!(x)){puts("FAIL " n);return 1;}}while(0)
int main(void)
{
 CHECK(native_admits(&own,0x15,0xa56e) && native_admits(&own,0x15,0xa56d) && native_admits(&own,0x15,R_REAP_REFRESH) && !native_admits(&own,0x15,R_REAP_REFRESH+1),"outer-trap-range");
 struct AROSRestrictedWork work,prior;memset(&work,0x5a,sizeof(work));prior=work;
 domains[0]=(struct RestrictedDomain){11,1,R_LIVE,0,&own,{1}};
 domains[1]=(struct RestrictedDomain){22,1,R_LIVE,0,&foreign,{2}};
 for(unsigned i=0;i<7;++i){
  domains[0].managed=1;domains[0].state=R_LIVE;domains[0].closing=0;
  uint64_t handle=11;void *caller=&own;
  if(i==0)handle=0;else if(i==1)handle=UINT64_MAX;else if(i==2)handle=22;
  else if(i==3)caller=&foreign;else if(i==4)domains[0].managed=0;
  else if(i==5)domains[0].state=3;else domains[0].closing=1;
  struct frame frame={{handle,(uintptr_t)&work}};calls=0;selected=NULL;
  CHECK(dispatch(&frame,&worker,caller,0xa56e)==1 && !frame.x[0] && !calls && !selected && !memcmp(&work,&prior,sizeof(work)),"admission-refused");
 }
 domains[0].managed=1;domains[0].state=R_LIVE;domains[0].closing=0;
 struct frame frame={{11,(uintptr_t)&work}};calls=0;
 CHECK(dispatch(&frame,&worker,&own,0xa56e)==1 && frame.x[0]==1 && calls==1 && selected==&domains[0].space,"owned-selected-space");
 accepted=0;frame.x[0]=11;calls=0;
 CHECK(dispatch(&frame,&worker,&own,0xa56e)==1 && !frame.x[0] && calls==1 && !memcmp(&work,&prior,sizeof(work)),"empty-owned-space");
 puts("PASS protected domain work admission");return 0;
}
'''
variants=[('actual',actual),('foreign-owner',actual.replace('d->owner_context==context','1')),('unmanaged',actual.replace('d->managed','1')),('nonlive',actual.replace('d->state==R_LIVE','1')),('closing',actual.replace('!d->closing','1')),('wrong-selected-space',actual.replace('&d->space','NULL')),('old-trap-range',actual)]
out.mkdir(parents=True);sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest();r={'result':'FAIL','scope':__doc__,'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'inputs':{str(f):sha(f) for f in [source,Path(__file__).resolve(),ROOT/'arch/aarch64-apple/include/aros/apple/restricted.h']},'cases':[]}
try:
 for name,body in variants:
  if name not in ('actual','old-trap-range') and body==actual:raise RuntimeError('mutation did not change source')
  c=out/(name+'.c');selected_guard=guard.replace('svc>R_REAP_REFRESH','svc>R_RESUME') if name=='old-trap-range' else guard
  c.write_text(defines+'\n'+header+body+tests+'\nstatic int native_admits(void *context,unsigned ec,unsigned svc)\n{\n'+selected_guard+'return 1;\n}\n');binary=out/name
  cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined','-I'+str(ROOT/'arch/aarch64-apple/include'),str(c),'-o',str(binary)]
  build=subprocess.run(cmd,capture_output=True,text=True);(out/(name+'-build.log')).write_text(build.stdout+build.stderr)
  if build.returncode:raise RuntimeError(name+' build failed')
  run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10);(out/(name+'.log')).write_text(run.stdout+run.stderr)
  r['cases'].append({'name':name,'argv':cmd,'exit':run.returncode,'stdout':run.stdout,'stderr':run.stderr})
  if name=='actual':assert run.returncode==0 and run.stdout=='PASS protected domain work admission\n' and not run.stderr
  else:assert run.returncode==1 and run.stdout.startswith('FAIL ') and not run.stderr
 r['result']='HOST_PASS_NOT_AROS_EXECUTION'
except Exception as error:r['error']=str(error)
(out/'result.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({'result':r['result'],'error':r.get('error')}));raise SystemExit(r['result']=='FAIL')
