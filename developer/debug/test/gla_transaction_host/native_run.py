#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Actual-source drawable provider retention and native binding admission."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import shutil
import sys
import time
from run import braced, routine, limits


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--ref",required=True)
    parser.add_argument("--allow-dirty-diagnostic",action="store_true")
    parser.add_argument("--cc",default="clang")
    args=parser.parse_args()
    root=args.source.resolve(); out=args.output.resolve()
    if out==root or root in out.parents:
        raise ValueError("output must be outside production source")
    out.mkdir(parents=True,exist_ok=False)
    here=Path(__file__).resolve().parent
    result={"schema":"GLA_NATIVE_DRAWABLE_HOST_V1","result":"INCOMPLETE",
            "scope":"actual-source HOST facades; no native ABI/GPU proof",
            "sources":{},"cases":[]}
    result["started_at"]=datetime.now(timezone.utc).isoformat()
    compiler=Path(shutil.which(args.cc) or args.cc).resolve()
    result["compiler"]={"path":str(compiler),"sha256":hashlib.sha256(compiler.read_bytes()).hexdigest()}
    start=time.monotonic()
    cache={}

    def source(path):
        if path not in cache:
            data=path.read_bytes(); cache[path]=data.decode()
            result["sources"][str(path)]=hashlib.sha256(data).hexdigest()
            snapshot=out/"source"; snapshot.mkdir(exist_ok=True)
            (snapshot/path.name).write_bytes(data)
        return cache[path]

    def run(name,argv):
        done=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                            timeout=25,preexec_fn=limits,env={**os.environ,
                            "ASAN_OPTIONS":"detect_leaks="+("0" if sys.platform=="darwin" else "1")+":abort_on_error=1",
                            "UBSAN_OPTIONS":"halt_on_error=1"})
        (out/(name+".log")).write_bytes(done.stdout)
        record={"name":name,"argv":argv,"exit":done.returncode,"pass":done.returncode==0}
        result["cases"].append(record)
        return record["pass"]
    try:
        head=subprocess.run(["git","rev-parse","HEAD"],cwd=root,check=True,capture_output=True,text=True,timeout=10).stdout.strip()
        expected=subprocess.run(["git","rev-parse",args.ref],cwd=root,check=True,capture_output=True,text=True,timeout=10).stdout.strip()
        assert head==expected
        status=subprocess.run(["git","status","--porcelain","--","workbench/libs/mesa"],cwd=root,check=True,capture_output=True,text=True,timeout=10).stdout
        result.update(source_commit=head,dirty_source=bool(status))
        if status and not args.allow_dirty_diagnostic:
            raise ValueError("uncommitted production source requires diagnostic mode")
        mesa=root/"workbench/libs/mesa"
        support=source(mesa/"mesa3dgl_support.c")
        types=source(mesa/"mesa3dgl_types.h")
        gallium=source(mesa/"mesa3dgl_gallium.c")
        setter=source(mesa/"mesa3dgl_glasetrast.c")
        drawables=source(mesa/"mesa3dgl_gladrawable.c")
        destroyer=source(mesa/"mesa3dgl_gladestroycontext.c")
        creator=source(mesa/"mesa3dgl_glacreatecontext.c")
        for p in (Path(__file__).resolve(),here/"run.py"):
            source(p)
        structs=[]
        for name in ("mesa3dgl_screen","mesa3dgl_drawable","mesa3dgl_context"):
            at=types.index("struct "+name+"\n")
            structs.append("struct "+name+"\n"+braced(types,at)+";")
        functions=[routine(support,name) for name in (
            "MESA3DGLSelectRastPort","MESA3DGLStandardInit","MESA3DGLRecalculateBufferWidthHeight",
            "MESA3DGLFreeTarget","MESA3DGLPrepareTarget","MESA3DGLCommitTarget","MESA3DGLVisualAccepts")]
        functions.append(routine(gallium,"MESA3DGLCheckAndUpdateBufferSize"))
        functions.append(routine(setter,"glASetRast","BOOL glASetRast(GLAContext ctx, struct TagItem *tagList)"))
        generated=source(here/"target.c").replace("/* STRUCTS */","\n".join(structs)).replace("/* SOURCE */","\n\n".join(functions))
        assert generated.count("int main(")==1
        generated=generated.replace("int main(","int target_main(")
        signatures=(
            ("glACreateDrawable","GLADrawable glACreateDrawable(GLAContext ctx, struct TagItem *tagList)"),
            ("glADestroyDrawable","void glADestroyDrawable(GLADrawable drawable)"),
            ("glAMakeCurrentDrawables","BOOL glAMakeCurrentDrawables(GLAContext ctx, GLADrawable draw, GLADrawable read)"),
        )
        bodies=[routine(drawables,name,sig) for name,sig in signatures]
        bodies.append(routine(destroyer,"glADestroyContext","void glADestroyContext(GLAContext ctx)"))
        # Exercise the exact error cleanup tail with the reachable state:
        # tracker created, framebuffer allocation just returned NULL.
        tail=creator[creator.index("\nerror_out:"):].strip()
        assert tail.endswith("}")
        bodies.append("static GLAContext creation_error(struct mesa3dgl_context *ctx)\n{\n    goto error_out;\n"+tail)
        generated+="\n"+source(here/"native.c").replace("/* NATIVE_SOURCE */","\n\n".join(bodies))
        c=out/"native.c"; c.write_text(generated)
        binary=out/"native"
        compiled=run("compile",[args.cc,"-std=c11","-Wall","-Wextra","-Werror","-pedantic",
             "-Wno-missing-field-initializers","-fsanitize=address,undefined","-fno-omit-frame-pointer",
             "-g",str(c),"-o",str(binary)])
        if not compiled:
            raise RuntimeError("compile failed; retained log")
        checks=[run(mode,[str(binary),mode]) for mode in ("success","lifetime","screen","visual","depth","accum","samples","buffers","cleanup")]
        result["result"]=("DIAGNOSTIC_PASS" if status else "PASS") if all(checks) else "FAIL"
        if any(path.read_text()!=text for path,text in cache.items()):
            raise RuntimeError("source changed during run; saved snapshots retain exact tested bytes")
    except (OSError,ValueError,AssertionError,subprocess.SubprocessError,RuntimeError) as error:
        result.update(result="FAIL",error=str(error))
    finally:
        result["elapsed_seconds"]=time.monotonic()-start
        (out/"result.json").write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps({"result":result["result"],"output":str(out)}))
    return 0 if result["result"] in ("PASS","DIAGNOSTIC_PASS") else 1


if __name__=="__main__":
    raise SystemExit(main())
