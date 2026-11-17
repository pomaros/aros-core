/* Copyright (C) 2026, The AROS Development Team. All rights reserved. */
#include <stdint.h>
#include <string.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <hidd/gfx.h>
#include <hidd/virtiogpu.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/oop.h>
#include "../../crt/test.h"
static struct Screen *screen;
static OOP_Object *services[2];
static OOP_AttrBase bitmap_ab;
static OOP_MethodID methods;
static IPTR call(OOP_Object *object,void *message,ULONG index)
{
    *(OOP_MethodID*)message=methods+index;
    return OOP_DoMethod(object,(OOP_Msg)message);
}
void cleanup(void)
{
    for(unsigned i=0;i<2;i++)if(services[i]){
        struct pHidd_VirtioGPU_Release msg;
        call(services[i],&msg,moHidd_VirtioGPU_Release);services[i]=NULL;
    }
    if(bitmap_ab){OOP_ReleaseAttrBase(IID_Hidd_BitMap);bitmap_ab=0;}
    if(screen){UnlockPubScreen(NULL,screen);screen=NULL;}
}
int main(void)
{
    OOP_Object *display=NULL;
    struct TagItem tags[]={{TAG_DONE,0}};
    struct Header {uint32_t type,flags;uint64_t fence;uint32_t context;uint8_t ring,pad[3];} request={0};
    uint8_t response[408];
    _Static_assert(sizeof(request)==24,"wire header");
    TEST((screen=LockPubScreen(NULL))!=NULL);
    TEST(IS_HIDD_BM(screen->RastPort.BitMap));
    TEST((bitmap_ab=OOP_ObtainAttrBase(IID_Hidd_BitMap))!=0);
    OOP_GetAttr(HIDD_BM_OBJ(screen->RastPort.BitMap),bitmap_ab+aoHidd_BitMap_Display,(IPTR*)&display);
    TEST(display!=NULL);
    OOP_Class *cl=OOP_FindClass(CLID_Hidd_VirtioGPU);TEST(cl!=NULL);
    methods=OOP_GetMethodID(IID_Hidd_VirtioGPU,0);
    for(unsigned i=0;i<2;i++){
        services[i]=HIDD_Display_CreateObject(display,cl,tags);TEST(services[i]!=NULL);
    }
    struct pHidd_VirtioGPU_Acquire acquire={0,VIRTIOGPU_TRANSPORT_VERSION+1};
    TEST(!call(services[0],&acquire,moHidd_VirtioGPU_Acquire));
    struct pHidd_VirtioGPU_AllocateID id={0,FALSE};
    ULONG first=call(services[0],&id,moHidd_VirtioGPU_AllocateID);
    ULONG second=call(services[1],&id,moHidd_VirtioGPU_AllocateID);
    TEST(first>1 && second>1 && first!=second);
    struct pHidd_VirtioGPU_Allocate alloc={0,VIRTIOGPU_MAX_BACKING_BYTES+1};
    TEST(!call(services[0],&alloc,moHidd_VirtioGPU_Allocate));
    alloc.bytes=4096;APTR memory=(APTR)call(services[0],&alloc,moHidd_VirtioGPU_Allocate);TEST(memory!=NULL);
    struct pHidd_VirtioGPU_Free free_msg={0,memory};
    TEST(!call(services[1],&free_msg,moHidd_VirtioGPU_Free));
    TEST(call(services[0],&free_msg,moHidd_VirtioGPU_Free));
    memset(response,0xa5,sizeof(response));
    struct pHidd_VirtioGPU_Command cmd={0,&request,0,response,sizeof(response),0x1101};
    TEST(!call(services[0],&cmd,moHidd_VirtioGPU_Command));
    cmd.bytes=VIRTIOGPU_MAX_REQUEST_BYTES+1;TEST(!call(services[0],&cmd,moHidd_VirtioGPU_Command));
    TEST(response[0]==0xa5);
    /* A truncated context-create command must be rejected without losing
     * the shared queue. A subsequent display query verifies recovery. */
    request.type=0x200;id.context=TRUE;
    request.context=call(services[0],&id,moHidd_VirtioGPU_AllocateID);TEST(request.context!=0);
    cmd.bytes=sizeof(request);cmd.response_bytes=24;cmd.response_type=0x1100;
    IPTR created=call(services[0],&cmd,moHidd_VirtioGPU_Command);
    if(created){request.type=0x201;call(services[0],&cmd,moHidd_VirtioGPU_Command);}
    TEST(!created);
    request.type=0x100;request.context=0;cmd.response_bytes=sizeof(response);cmd.response_type=0x1101;
    TEST(call(services[1],&cmd,moHidd_VirtioGPU_Command));
    struct pHidd_VirtioGPU_Healthy healthy;
    TEST(call(services[0],&healthy,moHidd_VirtioGPU_Healthy));
    cleanup();puts("shared transport: PASS");return OK;
}
