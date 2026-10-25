#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Sanitize the actual MakeClass and MUI_FreeClass implementations."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[4]

def body(name):
    text = (root / name).read_text()
    text = text[text.index('{\n    AROS_LIBFUNC_INIT'):]
    return text[:text.index('} /*') + 1]

head = r'''
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uintptr_t IPTR;typedef void *APTR;typedef unsigned long ULONG;typedef unsigned short UWORD;typedef int BOOL;
typedef const char *ClassID;
struct Hook {void *h_Data;};
typedef struct IClass {struct IClass *cl_Super;ClassID cl_ID;
 ULONG cl_InstOffset,cl_InstSize,cl_Flags,cl_ObjectSize,cl_SubclassCount;
 void *cl_MemoryPool;struct Hook cl_Dispatcher;} Class;
struct _Object {void *cl;};
struct Sem {int depth;};
struct IntuitionBase {struct Sem ClassListLock;};
struct Library {struct Sem ZuneSemaphore;};
static struct Library library;static struct Library *MUIMasterBase=&library;
static unsigned closes,removed,added;static BOOL refuse;
#define AROS_LIBFUNC_INIT
#define AROS_LIBFUNC_EXIT
#define DEBUG_MAKECLASS(...)
#define D(...)
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 2
#define CLF_INLIST 1
#define CLF_SCALE_AWARE 2
#define CLF_SCALE_DECLARED 4
#define ROOTCLASS "rootclass"
#define GetPrivIBase(x) (x)
#define MUIMB(x) (x)
#define AROS_ATOMIC_INC(x) (++(x))
static void ObtainSemaphore(struct Sem *s){++s->depth;}
static void ObtainSemaphoreShared(struct Sem *s){++s->depth;}
static void ReleaseSemaphore(struct Sem *s){assert(s->depth);--s->depth;}
static void *AllocMem(size_t n,unsigned flags){(void)flags;return calloc(1,n);}
static Class *FindClass(ClassID name){(void)name;return NULL;}
static void CloseLibrary(struct Library *base){assert(base==MUIMasterBase);++closes;}
static void ZUNE_RemoveBuiltinClass(Class *cl,struct Library *base){assert(cl&&base);++removed;}
static void ZUNE_AddBuiltinClass(Class *cl,struct Library *base){assert(cl&&base);++added;}
static BOOL FreeClass(Class *cl){if(refuse)return 0;--cl->cl_Super->cl_SubclassCount;free(cl);return 1;}
static void MUI_FreeClass(Class *cl);
'''
tests = r'''
int main(void){
 Class parent={.cl_ID=ROOTCLASS,.cl_InstOffset=8,.cl_InstSize=24};
 Class *cl=MakeClass(NULL,NULL,&parent,12,CLF_SCALE_AWARE|CLF_SCALE_DECLARED);
 assert(cl&&cl->cl_Super==&parent&&cl->cl_InstOffset==32&&cl->cl_InstSize==12);
 assert(!(cl->cl_Flags&(CLF_SCALE_AWARE|CLF_SCALE_DECLARED))&&parent.cl_SubclassCount==1);
 assert(!ib_global.ClassListLock.depth);
 cl->cl_Flags=CLF_INLIST;cl->cl_Dispatcher.h_Data=(void *)1;
 MUI_FreeClass(cl);assert(closes==1&&removed==1&&!library.ZuneSemaphore.depth&&!parent.cl_SubclassCount);
 cl=MakeClass(NULL,NULL,&parent,12,0);assert(cl);
 cl->cl_Flags=CLF_INLIST;cl->cl_Dispatcher.h_Data=(void *)2;
 MUI_FreeClass(cl);assert(cl->cl_Dispatcher.h_Data==(void *)1&&closes==1);
 refuse=1;MUI_FreeClass(cl);assert(cl->cl_Dispatcher.h_Data==(void *)1&&added==1&&closes==1);
 refuse=0;MUI_FreeClass(cl);assert(closes==2&&!library.ZuneSemaphore.depth&&!parent.cl_SubclassCount);
 puts("scaleclass lifetime ASan/UBSan PASS: actual MakeClass clears copied capabilities; actual MUI_FreeClass success/refcount/refusal has no freed-class access");return 0;
}
'''
make = body('rom/intuition/makeclass.c').replace('GetPrivIBase(IntuitionBase)', 'GetPrivIBase(&ib_global)')
free = body('workbench/libs/muimaster/mui_freeclass.c')
# Same production body, with only the generated library ABI wrapper replaced.
text = head + '\nstatic struct IntuitionBase ib_global;\n'
text += 'static Class *MakeClass(ClassID classID,ClassID superClassID,Class *superClassPtr,UWORD instanceSize,ULONG flags)\n' + make
text += '\nstatic void MUI_FreeClass(Class *cl)\n' + free + tests
with tempfile.TemporaryDirectory(prefix='aros-scaleclass-') as directory:
    source = Path(directory) / 'test.c'
    binary = Path(directory) / 'test'
    source.write_text(text)
    subprocess.run(['clang', '-std=c11', '-g', '-O1', '-Wall', '-Wextra',
                    '-Werror', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
