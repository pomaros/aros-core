/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t ULONG;
typedef uintptr_t IPTR;
typedef int BOOL;
typedef void *APTR;
typedef const char *CONST_STRPTR;
typedef char *STRPTR;
#define TRUE 1
#define FALSE 0
#define RXCOMM 0x1000000
#define RXFF_RESULT 0x20000
struct Message { struct Message *next; };
struct MsgPort { struct Message *head,*tail; };
struct RexxMsg { struct Message message; IPTR rm_Args[1]; ULONG rm_Action; long rm_Result1; intptr_t rm_Result2; };
static struct MsgPort destination;
static unsigned liveMessages,liveStrings,forbidDepth;
static BOOL portPresent=TRUE;
static struct MsgPort *CreateMsgPort(void) { return calloc(1,sizeof(struct MsgPort)); }
static void DeleteMsgPort(struct MsgPort *p) { assert(!p->head); free(p); }
static struct RexxMsg *CreateRexxMsg(struct MsgPort *reply,void *extension,const char *host)
{ (void)reply;(void)extension;(void)host; ++liveMessages; return calloc(1,sizeof(struct RexxMsg)); }
static void DeleteRexxMsg(struct RexxMsg *p) { assert(liveMessages);--liveMessages;free(p); }
static char *CreateArgstring(const char *text,size_t length)
{ char *p=malloc(length+1);memcpy(p,text,length);p[length]=0;++liveStrings;return p; }
static void DeleteArgstring(char *p) { assert(liveStrings);--liveStrings;free(p); }
static void Forbid(void) { ++forbidDepth; }
static void Permit(void) { assert(forbidDepth);--forbidDepth; }
static struct MsgPort *FindPort(const char *name) { assert(forbidDepth);(void)name;return portPresent?&destination:NULL; }
static void PutMsg(struct MsgPort *p,struct Message *m)
{ if(p==&destination)assert(forbidDepth);m->next=NULL;if(p->tail)p->tail->next=m;else p->head=m;p->tail=m; }
static struct Message *GetMsg(struct MsgPort *p)
{ struct Message *m=p->head;if(m){p->head=m->next;if(!p->head)p->tail=NULL;}return m; }
