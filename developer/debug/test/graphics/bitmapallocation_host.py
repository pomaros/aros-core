#!/usr/bin/env python3
# Copyright (C) 2026, John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License, version 1.1.
"""Check actual bitmap storage declarations and constructor publication order."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]
gfx = (ROOT / "compiler/include/graphics/gfx.h").read_text(encoding="latin-1")
hidd = (ROOT / "rom/hidds/gfx/include/gfx.h").read_text(encoding="latin-1")
allocator = (ROOT / "rom/graphics/allocbitmap.c").read_text()
bitmap = re.search(r"struct BitMap\s*\{.*?\};", gfx, re.S).group()
plane = re.search(r"struct HiddBitMapPlaneData\s*\{.*?\};", hidd, re.S).group()
macros = "\n".join(line for line in hidd.splitlines() if line.startswith("#define HIDD_BM_"))
size = re.search(r"AllocVec\((sizeof\(struct BitMap\).*?), MEMF_ANY \| MEMF_CLEAR\)", allocator).group(1)
start = allocator.index("struct BitMap *pbm = NULL;")
end = allocator.index('ReturnPtr("AllocBitMap", struct BitMap *, nbm);', start)
finish = allocator[start:end] + "return nbm;"
prefix = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
typedef uint8_t UBYTE; typedef uint16_t UWORD; typedef uint32_t ULONG;
typedef int32_t LONG; typedef uintptr_t IPTR; typedef UBYTE *PLANEPTR;
typedef void OOP_Object; typedef IPTR HIDDT_ColorModel;
typedef IPTR HIDDT_ModeID; typedef ULONG HIDDT_Pixel;
#define STACKED
struct Hook { int unused; };
struct monitor_driverdata;
'''
declarations = bitmap + "\n" + plane + "\n" + macros + "\n"
support = r'''
#define BMF_SPECIALFMT 128
#define BMF_STANDARD 8
#define BMF_DISPLAYABLE 2
#define BMF_INTERLEAVED 4
#define HIDD_BM_PAD_MAGIC 0x6148
#define aHidd_PlanarBM_BitMap 1
static int clear_calls;
static struct BitMap *planar;
static void BltBitMap(struct BitMap *a,int ax,int ay,struct BitMap *b,
 int bx,int by,int w,int h,int m,int mask,void *tmp) {
 (void)ax;(void)ay;(void)bx;(void)by;(void)w;(void)h;(void)m;(void)mask;(void)tmp;
 assert(a==b && a->pad==HIDD_BM_PAD_MAGIC);
 assert(HIDD_BM_OBJ(a)==(void *)(uintptr_t)123);
 assert(HIDD_BM_UPDATEHOOK(a)==NULL); ++clear_calls;
}
static void OOP_GetAttr(void *o,IPTR a,IPTR *out) {
 assert(o==(void *)(uintptr_t)123 && a==aHidd_PlanarBM_BitMap);
 *out=(IPTR)planar;
}
'''
main = r'''
int main(void) {
 struct Hook hook;
 for (int with_planar=0; with_planar<2; ++with_planar)
 for (int clear=0; clear<2; ++clear) {
   struct BitMap backing={0}; backing.BytesPerRow=47;
   for(int i=0;i<8;++i)backing.Planes[i]=(void *)(uintptr_t)(i+1);
   planar=with_planar?&backing:NULL;
   struct BitMap *bm=calloc(1,ALLOCATION_SIZE);assert(bm);
   /* Last field must fit the real allocation, on every tested ABI. */
   assert((char *)&HIDD_BM_UPDATEHOOK(bm)+sizeof(void *)<=(char *)bm+ALLOCATION_SIZE);
   HIDD_BM_OBJ(bm)=(void *)(uintptr_t)123;
   int before=clear_calls;
   assert(finish(bm,&hook,clear)==bm && clear_calls==before+clear);
   assert(HIDD_BM_UPDATEHOOK(bm)==&hook && bm->pad==HIDD_BM_PAD_MAGIC);
   if(with_planar) {
     assert(bm->BytesPerRow==47 && (bm->Flags&BMF_STANDARD));
     for(int i=0;i<8;++i)assert(bm->Planes[i]==backing.Planes[i]);
   }
   free(bm);
 }
 puts("PASS bitmap allocation: actual storage size, clear classification, hook publication");
}
'''
wrapper = "static struct BitMap *finish(struct BitMap *nbm,struct Hook *updatehook,int clear) {\nvoid *bm_obj=(void *)(uintptr_t)123;int sizex=8,sizey=8,flags=0;\n"
with tempfile.TemporaryDirectory(prefix="aros-bitmapallocation-") as directory:
    tmp = Path(directory)
    for name, code in [
        ("production", finish),
        ("premature-hook", "HIDD_BM_UPDATEHOOK(nbm)=updatehook;\n" + finish),
        ("missing-hidd-magic", finish.replace("nbm->pad = HIDD_BM_PAD_MAGIC;", "")),
    ]:
        text = prefix + declarations + "#define ALLOCATION_SIZE (" + size + ")\n" + support + wrapper + code + "}\n" + main
        (tmp / "test.c").write_text(text)
        subprocess.run(["clang", "-std=c99", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", str(tmp / "test.c"),
                        "-o", str(tmp / "test")], check=True)
        result = subprocess.run([str(tmp / "test")], capture_output=True, text=True)
        if name == "production":
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            print(result.stdout, end="")
        elif result.returncode == 0:
            raise RuntimeError("negative control escaped: " + name)
        else:
            print("PASS negative control:", name)
    # Compile the real declarations for both pointer widths; no host libc.
    freestanding = "typedef __UINT8_TYPE__ UBYTE;typedef __UINT16_TYPE__ UWORD;typedef __UINT32_TYPE__ ULONG;typedef __INT32_TYPE__ LONG;typedef __UINTPTR_TYPE__ IPTR;typedef UBYTE *PLANEPTR;typedef void OOP_Object;typedef IPTR HIDDT_ColorModel,HIDDT_ModeID;typedef ULONG HIDDT_Pixel;\n#define STACKED\n"
    checks = r'''
_Static_assert(__builtin_offsetof(struct BitMap,Planes)+8*sizeof(PLANEPTR)==sizeof(struct BitMap),"extension origin");
_Static_assert(sizeof(struct HiddBitMapPlaneData)==(sizeof(void *)==8?64:36),"actual extension ABI");
'''
    (tmp / "layout.c").write_text(freestanding + declarations + checks)
    for target in ["aarch64-none-elf", "i386-none-elf"]:
        subprocess.run(["clang", "--target=" + target, "-ffreestanding", "-std=c11",
                        "-Wall", "-Werror", "-c", str(tmp / "layout.c"),
                        "-o", str(tmp / "layout.o")], check=True)
        print("PASS actual storage declarations:", target)
