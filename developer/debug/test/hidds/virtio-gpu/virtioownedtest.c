/* Native owner-boundary regressions. Test resources never touch scanout1. */
#include <stdint.h>
#include <exec/memory.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <hidd/gfx.h>
#include <hidd/virtiogpu.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/oop.h>
#include "../../../../../workbench/hidds/virgl/virgl_gpu_protocol.h"
#include "../../crt/test.h"
static struct Screen *screen;
static OOP_Object *service[2];
static OOP_AttrBase bitmap_ab;
static OOP_MethodID methods;
static IPTR call(unsigned client,void *message,ULONG index)
{ *(OOP_MethodID*)message=methods+index;return OOP_DoMethod(service[client],(OOP_Msg)message); }
static BOOL command(unsigned client,const void *request,ULONG size)
{
 struct gpu_header response;
 struct pHidd_VirtioGPU_Command msg={0,request,size,&response,sizeof(response),GPU_OK_NODATA};
 return call(client,&msg,moHidd_VirtioGPU_Command);
}
static ULONG identity(unsigned client,BOOL context)
{ struct pHidd_VirtioGPU_AllocateID m={0,context};return call(client,&m,moHidd_VirtioGPU_AllocateID); }
static BOOL resource(unsigned client,ULONG id)
{
 struct render_create m={.header.type=GPU_RESOURCE_CREATE_3D,.resource=id,.target=2,.format=1,.bind=2,.width=16,.height=16,.depth=1,.array_size=1};
 return command(client,&m,sizeof(m));
}
static void release_resource(unsigned client,ULONG id)
{ struct render_resource m={.header.type=GPU_RESOURCE_UNREF,.resource=id};command(client,&m,sizeof(m)); }
void cleanup(void)
{
 for(unsigned i=0;i<2;i++)if(service[i]){struct pHidd_VirtioGPU_Release m;call(i,&m,moHidd_VirtioGPU_Release);service[i]=NULL;}
 if(bitmap_ab){OOP_ReleaseAttrBase(IID_Hidd_BitMap);bitmap_ab=0;}
 if(screen){UnlockPubScreen(NULL,screen);screen=NULL;}
}
int main(void)
{
 OOP_Object *display=NULL;struct TagItem tags[]={{TAG_DONE,0}};
 TEST((screen=LockPubScreen(NULL))!=NULL);
 TEST((bitmap_ab=OOP_ObtainAttrBase(IID_Hidd_BitMap))!=0);
 OOP_GetAttr(HIDD_BM_OBJ(screen->RastPort.BitMap),bitmap_ab+aoHidd_BitMap_Display,(IPTR*)&display);TEST(display!=NULL);
 OOP_Class *cl=OOP_FindClass(CLID_Hidd_VirtioGPU);TEST(cl!=NULL);methods=OOP_GetMethodID(IID_Hidd_VirtioGPU,0);
 for(unsigned i=0;i<2;i++){service[i]=HIDD_Display_CreateObject(display,cl,tags);TEST(service[i]!=NULL);}
 ULONG id=identity(0,FALSE);TEST(id>1);TEST(resource(0,id));
 struct pHidd_VirtioGPU_Allocate alloc={0,1024};APTR memory=(APTR)call(0,&alloc,moHidd_VirtioGPU_Allocate);TEST(memory!=NULL);
 UQUAD address=0;struct pHidd_VirtioGPU_Address addr={0,memory,&address};TEST(call(0,&addr,moHidd_VirtioGPU_Address));
 struct render_backing attach={.header.type=GPU_ATTACH_BACKING,.resource=id,.count=1,.address=address,.length=1024};
 BOOL attached=command(0,&attach,sizeof(attach));
 struct pHidd_VirtioGPU_Free free_msg={0,memory};BOOL freed=FALSE;
 if(attached)freed=call(0,&free_msg,moHidd_VirtioGPU_Free);
 /* No rendering references this empty test resource. Detach/unref before
  * asserting, including on the old implementation that incorrectly frees. */
 struct render_resource detach={.header.type=GPU_DETACH_BACKING,.resource=id};
 if(attached){TEST(command(0,&detach,sizeof(detach)));}
 release_resource(0,id);if(!freed){TEST(call(0,&free_msg,moHidd_VirtioGPU_Free));}
 TEST(attached);TEST(!freed);
 /* A peer must not be able to destroy an allocated, live context. */
 ULONG context=identity(0,TRUE);TEST(context!=0);
 struct render_context create={.header.type=GPU_CTX_CREATE,.header.context=context,.length=5,.name="owner"};TEST(command(0,&create,sizeof(create)));
 struct gpu_header destroy={.type=GPU_CTX_DESTROY,.context=context};BOOL peer_destroyed=command(1,&destroy,sizeof(destroy));
 if(!peer_destroyed){TEST(command(0,&destroy,sizeof(destroy)));}TEST(!peer_destroyed);
 /* Valid DMA belonging to a different service is still unauthorized. */
 id=identity(0,FALSE);TEST(resource(0,id));memory=(APTR)call(1,&alloc,moHidd_VirtioGPU_Allocate);TEST(memory!=NULL);
 addr.pointer=memory;TEST(call(1,&addr,moHidd_VirtioGPU_Address));attach.resource=id;attach.address=address;
 BOOL peer_backing=command(0,&attach,sizeof(attach));detach.resource=id;
 if(peer_backing){TEST(command(0,&detach,sizeof(detach)));}release_resource(0,id);
 free_msg.pointer=memory;TEST(call(1,&free_msg,moHidd_VirtioGPU_Free));TEST(!peer_backing);
 /* An arbitrary unused number is not an owner-allocated resource ID. */
 BOOL unallocated=resource(0,0x7fffffff);if(unallocated)release_resource(0,0x7fffffff);TEST(!unallocated);
 cleanup();puts("owned DMA and protocol: PASS");return OK;
}
