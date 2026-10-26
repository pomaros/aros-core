/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */

#include "shim.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#define main command_main
#include "COMMAND_SOURCE"
#undef main
struct Property { const char *name; unsigned long size; void *data; };
static unsigned char cells[]={99,0,0,0,42};
static unsigned char large[257];
static struct Property props[]={
 {"model",8,"Test VM"},{"compatible",8,"one\0two"},
 {"reg",4,cells+1},{"odd",3,"abc"},{"status",3,"bad"},
 {"flag",0,NULL},{"large",257,large},{"bootargs",7,"SECRET"},
 {"unsafe\033",4,"a\033b"}
};
static int mode,rootnode,childnode;

struct RDArgs *ReadArgs(const char *t, IPTR *a, void *p) {
 static struct RDArgs r; static LONG depth;
 (void)t;(void)p;
 if(mode==1) a[1]=1;
 if(mode==2) {a[2]=(IPTR)"compatible";a[3]=1;}
 if(mode==3) a[0]=(IPTR)"/missing";
 if(mode==4) a[2]=(IPTR)"missing";
 if(mode==5) {depth=0;a[4]=(IPTR)&depth;}
 if(mode==6) {depth=65;a[4]=(IPTR)&depth;}
 if(mode==7) a[2]=(IPTR)"bootargs";
 if(mode==11) {a[0]=(IPTR)"/pci@0/";a[1]=1;}
 if(mode==13) a[2]=(IPTR)"reg";
 return &r;
}
void FreeArgs(struct RDArgs *r){(void)r;}
LONG IoErr(void){return 0;}
void PrintFault(LONG e,const char *s){(void)e;(void)s;}
int Printf(const char *s,...){va_list a;va_start(a,s);int n=vprintf(s,a);va_end(a);return n;}
int PutStr(const char *s){return fputs(s,stdout);}
APTR OpenResource(const char *s){(void)s;return mode==8?NULL:&rootnode;}
struct Resident *FindResident(const char *s){(void)s;assert(!"runtime resource needs no ROM tag"); return NULL;}
ULONG SetSignal(ULONG a,ULONG b){(void)a;(void)b;return mode==10?SIGBREAKF_CTRL_C:0;}
LONG Flush(APTR f){(void)f;return mode==12?0:1;}
APTR Output(void){return stdout;}
APTR OF_OpenKey(char *s){return !strcmp(s,"/")?&rootnode:!strcmp(s,"/pci@0")?&childnode:NULL;}
void OF_CloseKey(APTR n){(void)n;}
APTR OF_GetProperty(APTR n,APTR p){if(n!=&rootnode)return NULL;struct Property *v=p?(struct Property *)p+1:props;return v<props+9?v:NULL;}
APTR OF_GetChild(APTR n,APTR p){return n==&rootnode&&!p?&childnode:NULL;}
APTR OF_FindProperty(APTR n,char *s){APTR p=NULL;while((p=OF_GetProperty(n,p)))if(!strcmp(OF_GetPropName(p),s))return p;return NULL;}
const char *OF_GetNodeName(APTR n){return n==&rootnode?"":"pci@0";}
const char *OF_GetPropName(APTR p){return ((struct Property *)p)->name;}
ULONG OF_GetPropLen(APTR p){return ((struct Property *)p)->size;}
APTR OF_GetPropValue(APTR p){return ((struct Property *)p)->data;}
int main(int argc,char **argv){(void)argc;mode=atoi(argv[1]);memset(large,0xaa,sizeof large);return command_main();}
