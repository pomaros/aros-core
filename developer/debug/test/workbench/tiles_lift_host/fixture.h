/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t ULONG;typedef int32_t LONG;typedef uint8_t UBYTE;typedef uintptr_t IPTR;typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define MEMF_CLEAR 1
#define MEMF_ANY 0
#define RECTFMT_ARGB 1
#define BMA_WIDTH 1
#define BMA_HEIGHT 2
#define IDCMP_REFRESHWINDOW 1
#define TAG_DONE 0
#define WA_CustomScreen 1
#define WA_Left 2
#define WA_Top 3
#define WA_Width 4
#define WA_Height 5
#define WA_Borderless 6
#define WA_ToolBox 7
#define WA_Activate 8
#define WA_SimpleRefresh 9
#define WA_BackFill 10
#define WA_TileOverlay 11
#define LAYERS_NOBACKFILL 1
#define TILE_LIFT_CLICK 1
#define TILE_LIFT_DISMISS 2
struct BitMap{ULONG w,h;};struct RastPort{int unused;};struct Screen{LONG Width,Height;struct RastPort RastPort;};
struct Message{int unused;};struct IntuiMessage{struct Message ExecMessage;ULONG Class;};
struct MsgPort{unsigned mp_SigBit;};struct Window{struct RastPort *RPort;struct MsgPort *UserPort;};
struct TileLift;struct TileLiftItem{ULONG id;struct BitMap *bitmap;LONG left,top;ULONG width,height;};struct TileLiftEvent{ULONG type,id;LONG x,y;};
static struct BitMap *blit_sources[32];
static int clears;
static void SetRast(struct RastPort *r,int pen){(void)r;assert(pen==0);clears++;}
static int allocations,ports,windows,calls,fail_at,fail_read,blits,writes;static struct RastPort rp;
static void *AllocVec(size_t n,int flags){(void)flags;if(++calls==fail_at)return NULL;void *p=calloc(1,n);if(p)allocations++;return p;}
static void FreeVec(void *p){assert(p);allocations--;free(p);}
static struct MsgPort *CreateMsgPort(void){if(++calls==fail_at)return NULL;ports++;return calloc(1,sizeof(struct MsgPort));}
static void DeleteMsgPort(struct MsgPort *p){assert(p);ports--;free(p);}
static ULONG GetBitMapAttr(struct BitMap *p,int a){return a==BMA_WIDTH?p->w:p->h;}
static ULONG ReadPixelArray(UBYTE *p,int x,int y,ULONG stride,struct RastPort *r,int sx,int sy,ULONG w,ULONG h,int f){(void)x;(void)y;(void)stride;(void)r;(void)sx;(void)sy;(void)f;if(fail_read)return 0;for(ULONG i=0;i<w*h;i++){p[4*i]=255;p[4*i+1]=200;p[4*i+2]=100;p[4*i+3]=50;}return w*h;}
static void WritePixelArray(UBYTE *p,int x,int y,ULONG stride,struct RastPort *r,int sx,int sy,ULONG w,ULONG h,int f){(void)x;(void)y;(void)stride;(void)r;(void)sx;(void)sy;(void)f;for(ULONG i=0;i<w*h;i++)assert(p[i*4]==255&&p[i*4+1]==60&&p[i*4+2]==30&&p[i*4+3]==15);writes++;}
static struct Window *OpenWindowTags(void *p,...){(void)p;if(++calls==fail_at)return NULL;struct Window *w=calloc(1,sizeof(*w));w->RPort=&rp;windows++;return w;}
static BOOL ModifyIDCMP(struct Window *w,ULONG flags){if(!flags&&w->UserPort){DeleteMsgPort(w->UserPort);w->UserPort=NULL;}return TRUE;}
static void CloseWindow(struct Window *w){assert(!w->UserPort);windows--;free(w);}
static void BltBitMapRastPort(struct BitMap *b,LONG sx,LONG sy,struct RastPort *r,LONG x,LONG y,LONG w,LONG h,int minterm){(void)r;(void)minterm;assert(sx>=0&&sy>=0&&w>0&&h>0&&sx+w<=(LONG)b->w&&sy+h<=(LONG)b->h);assert(x>=0&&y>=0&&x+w<=100&&y+h<=80);blit_sources[blits++]=b;}
static void WaitBlit(void){}static void Forbid(void){}static void Permit(void){}
static struct Message *GetMsg(struct MsgPort *p){(void)p;return NULL;}
static void ReplyMsg(struct Message *p){(void)p;}
static void BeginRefresh(struct Window *w){(void)w;}static void EndRefresh(struct Window *w,BOOL b){(void)w;(void)b;}static void WindowToFront(struct Window *w){assert(w);}
