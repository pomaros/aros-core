/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL), version 1.1.
 * HOST fixture: exact production bodies are included, never rewritten here.
 */
#define _DARWIN_C_SOURCE 1
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
typedef uint8_t UBYTE; typedef uint16_t UWORD; typedef int16_t WORD;
typedef uint32_t ULONG; typedef void *APTR; typedef void VOID;
typedef void OOP_Class;
typedef struct { ULONG fg,bg; int colexp,draw; } OOP_Object;
#define GC_FG(g) ((g)->fg)
#define GC_BG(g) ((g)->bg)
#define GC_COLEXP(g) ((g)->colexp)
#define GC_DRMD(g) ((g)->draw)
#define vHidd_GC_ColExp_Transparent 1
#define vHidd_GC_DrawMode_Invert 10
#define AROS_BIG_ENDIAN 0
#define AROS_BE2WORD(v) __builtin_bswap16(v)
#define MSG(bits) struct pHidd_BitMap_PutMemTemplate##bits { OOP_Object *gc; UBYTE *masktemplate; ULONG modulo; WORD srcx; APTR dst; ULONG dstMod; WORD x,y,width,height; int inverttemplate; };
MSG(8) MSG(16) MSG(24) MSG(32)
#include "production.inc"
static void *guard;
static void fault(int sig,siginfo_t *info,void *context) {
 (void)sig;(void)context;
 if(info->si_addr==guard){static const char s[]="GUARD_PAGE_READ_FAULT\n";write(2,s,sizeof(s)-1);_exit(90);}
 _exit(91);
}
static int one(int bits,int type,int width,int srcx,int height,int pattern,int guarded) {
 int bpp=bits/8,modulo=((srcx+width+15)/16)*2;
 size_t bytes=(size_t)modulo*height,page=(size_t)sysconf(_SC_PAGESIZE);
 if(bytes>page)return 10;
 UBYTE *mapping=NULL,*source;
 if(guarded){
  mapping=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
  if(mapping==MAP_FAILED||mprotect(mapping+page,page,PROT_NONE))return 11;
  guard=mapping+page;source=mapping+page-bytes;
 }else{source=malloc(bytes);if(!source)return 12;}
 for(size_t i=0;i<bytes;i++)source[i]=pattern==0?0:pattern==1?255:(UBYTE)(i*37+0xa6);
 int stride=(width+5)*bpp;size_t size=(size_t)(height+2)*stride;
 UBYTE *dst=malloc(size),*want=malloc(size);if(!dst||!want)return 13;
 for(size_t i=0;i<size;i++)dst[i]=want[i]=(UBYTE)(i*13+0x49);
 OOP_Object gc={0x98a7b6c5U,0x12e34a5bU,type<2?1:0,(type==2||type==3)?10:0};
 for(int y=0;y<height;y++)for(int x=0;x<width;x++){
  int bit=(source[(size_t)y*modulo+(srcx+x)/8]>>(7-((srcx+x)&7)))&1;
  int active=bit^(type&1);
  for(int k=0;k<bpp;k++){
   size_t at=(size_t)(y+1)*stride+(x+2)*bpp+k;
   if(type<2){if(active)want[at]=(UBYTE)(gc.fg>>(8*k));}
   else if(type<4){if(active)want[at]=(UBYTE)~want[at];}
   else want[at]=(UBYTE)((active?gc.fg:gc.bg)>>(8*k));
  }
 }
 #define CALL(b) case b:{struct pHidd_BitMap_PutMemTemplate##b msg={&gc,source,(ULONG)modulo,(WORD)srcx,dst,(ULONG)stride,2,1,(WORD)width,(WORD)height,type&1}; BM__Hidd_BitMap__PutMemTemplate##b(NULL,NULL,&msg);break;}
 switch(bits){CALL(8) CALL(16) CALL(24) CALL(32) default:return 14;}
 int result=memcmp(dst,want,size)?15:0;
 if(result)fprintf(stderr,"pixel mismatch bits=%d type=%d width=%d srcx=%d height=%d pattern=%d\n",bits,type,width,srcx,height,pattern);
 free(dst);free(want);if(guarded){if(munmap(mapping,page*2))return 16;}else free(source);
 return result;
}
int main(int argc,char **argv) {
 if(argc==3&&!strcmp(argv[1],"fault")){
  struct sigaction a={0};a.sa_sigaction=fault;a.sa_flags=SA_SIGINFO;sigemptyset(&a.sa_mask);sigaction(SIGSEGV,&a,NULL);sigaction(SIGBUS,&a,NULL);
  return one(atoi(argv[2]),0,64,0,8,2,1);
 }
 int guarded=argc==2&&!strcmp(argv[1],"guard");
 int formats[]={8,16,24,32},widths[]={1,15,16,17,31,64},offsets[]={0,1,7,15,16,17,31},heights[]={1,2,8};unsigned cases=0;
 for(unsigned f=0;f<4;f++)for(int mode=0;mode<6;mode++)for(unsigned w=0;w<6;w++)for(unsigned o=0;o<7;o++)for(unsigned h=0;h<3;h++)for(int pattern=0;pattern<3;pattern++){
  int r=one(formats[f],mode,widths[w],offsets[o],heights[h],pattern,guarded);if(r)return r;cases++;
 }
 printf("PASS actual production bodies cases=%u boundary=%s\n",cases,guarded?"guard-page":"exact-heap-ASan");return 0;
}
