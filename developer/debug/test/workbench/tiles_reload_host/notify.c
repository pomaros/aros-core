/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define SNOTIFY_BEFORE_CLOSEWB 1
#define SNOTIFY_AFTER_OPENWB 2
struct Message {int placeholder;};
struct ScreenNotifyMessage {struct Message message;unsigned snm_Class;};
struct TileService {void *screenPort,*screenNotify,*screen;int stopping,reloadPending;};
static struct ScreenNotifyMessage message;
static unsigned queued,replied,attempts,closes,locks;
static int subscription,portAlive;
static struct Message *GetMsg(void *port){assert(port&&(portAlive));if(!queued)return NULL;queued=0;return &message.message;}
static void ReplyMsg(struct Message *m){assert(m==&message.message&&portAlive);++replied;}
static void release_screen(struct TileService *s){assert(portAlive);s->screen=NULL;++closes;}
static void *LockPubScreen(void *name){(void)name;++locks;return (void*)3;}
static BOOL EndScreenNotify(void *handle){assert(handle==(void*)2&&subscription&&portAlive);++attempts;if(attempts==1){/* Concurrent sender arrives after first drain and holds lock for WAIT_REPLY. */queued=1;message.snm_Class=SNOTIFY_BEFORE_CLOSEWB;return FALSE;}assert(replied==1&&!queued&&!locks);subscription=0;return TRUE;}
/* EXTRACT */
int main(void){struct TileService s={0};/* Startup before port/subscription creation is safe. */assert(unsubscribe_screen(&s));assert(!attempts&&!replied);s.screenPort=(void*)1;s.screenNotify=(void*)2;s.screen=(void*)3;s.stopping=1;subscription=portAlive=1;assert(!unsubscribe_screen(&s));assert(s.screenNotify==(void*)2&&s.screenPort==(void*)1&&subscription&&portAlive&&queued);assert(unsubscribe_screen(&s));assert(!s.screenNotify&&s.screenPort==(void*)1&&!subscription&&portAlive&&attempts==2&&replied==1&&closes==1&&!s.screen);/* A queued AFTER_OPEN must be replied, but shutdown must not reacquire screen. */queued=1;message.snm_Class=SNOTIFY_AFTER_OPENWB;assert(unsubscribe_screen(&s));assert(!locks&&!s.reloadPending&&replied==2);portAlive=0;puts("tiles-notify-host PASS");}
