/* Copyright (C) 2026 John KNIPPER. AROS Public License 1.1. */

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL;
typedef char *STRPTR;
#define NRF_SEND_MESSAGE 1
struct NotifyRequest {char *nr_Name;int nr_Flags;union {struct {void *nr_Port;} nr_Msg;} nr_stuff;};
struct MUI_ApplicationData {void *app_CfgNotifyPort; char *app_CfgNotifyName,*app_Base; struct NotifyRequest app_CfgNotifyRequest,app_GlobalCfgNotifyRequest; BOOL app_CfgNotifyStarted,app_GlobalCfgNotifyStarted;};
struct IClass{};typedef struct MUI_ApplicationData Object;
#define INST_DATA(cl,obj) (obj)
static int fail_port,fail_name,fail_global,fail_local,active,started,ended,deleted;
static void *CreateMsgPort(void){return fail_port?NULL:malloc(1);}
static void DeleteMsgPort(void *p){assert(active==0);deleted++;free(p);}
static char *StrDup(const char *s){return fail_name?NULL:strdup(s);}
static void FreeVec(void *p){free(p);}
static int StartNotify(struct NotifyRequest *n){assert(n->nr_stuff.nr_Msg.nr_Port);if(!strcmp(n->nr_Name,"ENV:zune/global.prefs")?fail_global:fail_local)return 0;active++;started++;return 1;}
static void EndNotify(struct NotifyRequest *n){assert(n->nr_Name);assert(active>0);active--;ended++;}
#include "production_notify.inc"

int main(void){int scenario;for(scenario=0;scenario<7;scenario++){Object d={0};fail_port=scenario==1;fail_name=scenario==2;fail_global=scenario==3||scenario==5;fail_local=scenario==4||scenario==5;active=started=ended=deleted=0;d.app_Base=scenario==6?NULL:"APP";Application_SetupConfigNotify(NULL,&d);assert(d.app_GlobalCfgNotifyStarted==(!fail_port&&!fail_global));assert(d.app_CfgNotifyStarted==(!fail_port&&!fail_name&&!fail_local&&d.app_Base!=NULL));cleanup(&d);assert(active==0&&started==ended);assert(deleted==!fail_port);}puts("Zune production notify lifecycle: seven scenarios PASS");}
