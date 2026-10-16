/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#ifndef INPUT_MOCK_H
#define INPUT_MOCK_H
#include <stdint.h>
#include <stddef.h>
typedef int BOOL;typedef int32_t LONG;typedef uint32_t ULONG;typedef int8_t BYTE;
#define TRUE 1
#define FALSE 0
typedef LONG CxObj;typedef LONG CxMsg;
struct Task;struct Screen{LONG MouseX,MouseY;};
struct InputEvent{unsigned ie_Class,ie_Code,ie_Qualifier;};
typedef struct {unsigned ix_Class,ix_Code;} IX;
struct NewBroker{BYTE nb_Version;const char *nb_Name,*nb_Title,*nb_Descr;int nb_Unique,nb_Flags;BYTE nb_Pri;void *nb_Port;int nb_ReservedChannel;};
#define NB_VERSION 5
#define NBU_UNIQUE 1
#define CXM_IEVENT 32
#define IECLASS_RAWKEY 1
#define IECLASS_RAWMOUSE 2
#define IECODE_UP_PREFIX 128
#define IEQUALIFIER_REPEAT 512
#define IECODE_LBUTTON 104
#define IECODE_RBUTTON 105
#define IECODE_MBUTTON 106
LONG CxMsgType(CxMsg*);void *CxMsgData(CxMsg*);void DisposeCxMsg(CxMsg*);
LONG ParseIX(const char*,IX*);LONG MatchIX(struct InputEvent*,IX*);
BYTE AllocSignal(LONG);void FreeSignal(LONG);struct Task *FindTask(void*);void Signal(struct Task*,ULONG);ULONG SetSignal(ULONG,ULONG);
CxObj *CxBroker(struct NewBroker*,LONG*);CxObj *mock_custom(void (*)(CxMsg*,CxObj*));
#define CxCustom(fn,id) mock_custom(fn)
void AttachCxObj(CxObj*,CxObj*);LONG CxObjError(CxObj*);LONG ActivateCxObj(CxObj*,LONG);void DeleteCxObjAll(CxObj*);
void kprintf(const char*,...);
#endif
