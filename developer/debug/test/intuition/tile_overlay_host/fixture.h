/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include <assert.h>
#include <stdio.h>
#include <stddef.h>
typedef int LONG;
#define FALSE 0
#define TRUE 1
#define PROTECTEDPRIORITY 30
#define BACKDROPPRIORITY 10
#define UPFRONTPRIORITY 20
#define LAYERPROTECTED (1<<11)
#define LAYERBACKDROP (1<<6)
#define WFLG_BORDERLESS 1
#define WFLG_TOOLBOX 2
#define WFLG_ACTIVATE 4
#define WFLG_GIMMEZEROZERO 8
#define WFLG_BACKDROP 16
struct Layer{struct Layer *front,*back,*parent;int priority,nesting;void *LayerInfo;};
static void *LayersBase;static int locks,moves;static struct Layer *target;
static void LockLayers(void *p){(void)p;locks++;}static void UnlockLayers(void *p){(void)p;locks--;}
static struct Layer *GetFirstFamilyMember(struct Layer *p){return p;}
static int _MoveLayerToFront(struct Layer *p,struct Layer *t,void *b){(void)p;(void)b;moves++;target=t;return 1;}
static int _MoveLayerBehind(struct Layer *p,struct Layer *t,void *b){(void)p;(void)b;moves++;target=t;return 1;}
