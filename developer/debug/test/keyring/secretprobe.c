/*
 * Copyright (C) 2026 John Knipper
 * Licensed under the AROS Public License (APL) Version 1.1
 */
#include <aros/asmcall.h>
#include <exec/errors.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <string.h>
#include "secretprobe.h"

AROS_UFH2(struct InputEvent *, observe,
    AROS_UFHA(struct InputEvent *, events, A0),
    AROS_UFHA(struct secret_probe *, probe, A1))
{
    AROS_USERFUNC_INIT
    struct InputEvent *e;
    for(e=events;e;e=e->ie_NextEvent)
        if(e->ie_Class==IECLASS_RAWKEY && (e->ie_Code & ~IECODE_UP_PREFIX)<=0x41)
            ++probe->printable;
    return events;
    AROS_USERFUNC_EXIT
}
static int command(struct secret_probe *probe, UWORD cmd, void *data, ULONG size)
{
    probe->request->io_Command=cmd;
    probe->request->io_Data=data;
    probe->request->io_Length=size;
    probe->request->io_Flags=0;
    probe->request->io_Error=0;
    return DoIO((struct IORequest *)probe->request);
}
int secret_probe_begin(struct secret_probe *probe)
{
    struct InputEvent event;
    int add,write;
    memset(probe,0,sizeof(*probe));
    probe->port=CreateMsgPort();
    if(!probe->port) return 0;
    probe->request=(struct IOStdReq *)CreateIORequest(probe->port,sizeof(struct IOStdReq));
    if(!probe->request || OpenDevice("input.device",0,(struct IORequest *)probe->request,0)) goto fail;
    probe->opened=1;
    probe->handler.is_Node.ln_Type=NT_INTERRUPT;
    /* Observe before consumers can remove an event. Return the list unchanged. */
    probe->handler.is_Node.ln_Pri=127;
    probe->handler.is_Node.ln_Name="keyring-probe-recorder";
    probe->handler.is_Code=(void (*)())AROS_ASMSYMNAME(observe);
    probe->handler.is_Data=probe;
    if(command(probe,IND_ADDHANDLER,&probe->handler,0)) goto fail;
    probe->added=1;
    memset(&event,0,sizeof(event)); event.ie_Class=IECLASS_RAWKEY; event.ie_Code=0x20;
    if(command(probe,IND_ADDEVENT,&event,sizeof(event))) goto fail;
    probe->positive_control=probe->printable>0;
    if(command(probe,IND_SECRETBEGIN,NULL,0)) goto fail;
    add=command(probe,IND_ADDEVENT,&event,sizeof(event));
    write=command(probe,IND_WRITEEVENT,&event,sizeof(event));
    probe->injection_refused=add==IOERR_UNITBUSY && write==IOERR_UNITBUSY;
    if(command(probe,IND_SECRETEND,NULL,0)) goto fail;
    probe->printable=0;
    if(probe->positive_control && probe->injection_refused) return 1;
fail:
    secret_probe_end(probe); return 0;
}
ULONG secret_probe_end(struct secret_probe *probe)
{
    ULONG count;
    if(probe->added) { command(probe,IND_REMHANDLER,&probe->handler,0); probe->added=0; }
    count=probe->printable;
    if(probe->opened) { CloseDevice((struct IORequest *)probe->request); probe->opened=0; }
    if(probe->request) { DeleteIORequest((struct IORequest *)probe->request); probe->request=NULL; }
    if(probe->port) { DeleteMsgPort(probe->port); probe->port=NULL; }
    return count;
}
