/* Copyright (C) 2026 John Knipper
 * Licensed under the AROS Public License (APL) Version 1.1 */
#define MUIMASTER_YES_INLINE_STDARG
#include <libraries/mui.h>
#include <libraries/keyring.h>
#include <zune/prefseditor.h>
#include <proto/alib.h>
#include <proto/exec.h>
#include <proto/muimaster.h>
#include <proto/keyring.h>
#include "editor.h"
#include LC_LIBDEFS_FILE

IPTR KeyringEditor__OM_NEW(Class *CLASS,Object *self,struct opSet *message)
{
    Object *state,*result,*lock,*unlock,*refresh;
    self=(Object *)DoSuperNewTags(CLASS,self,NULL,
        MUIA_PrefsEditor_Name,(IPTR)"Keyring",
        MUIA_PrefsEditor_CanUse,FALSE,MUIA_PrefsEditor_CanTest,FALSE,
        Child,(IPTR)ColGroup(2),
            Child,(IPTR)Label2("State"),
            Child,(IPTR)(state=(Object *)TextObject,
                MUIA_ObjectID,MAKE_ID('K','R','S','T'),
                MUIA_Text_Contents,(IPTR)"Refresh to query the keyring",End),
            Child,(IPTR)Label2("Authority"),
            Child,(IPTR)TextObject,MUIA_Text_Contents,(IPTR)"containment",End,
        End,
        Child,(IPTR)HGroup,
            Child,(IPTR)(lock=(Object *)SimpleButton("Lock")),
            Child,(IPTR)(unlock=(Object *)SimpleButton("Unlock")),
            Child,(IPTR)(refresh=(Object *)SimpleButton("Refresh")),
        End,
        Child,(IPTR)(result=(Object *)TextObject,
            MUIA_ObjectID,MAKE_ID('K','R','R','S'),MUIA_Text_Contents,(IPTR)"",End),
        Child,(IPTR)TextObject,MUIA_Text_Contents,(IPTR)
            "Lock clears the session's decrypted entries.\n"
            "Unlock asks for your passphrase on the physical keyboard.\n"
            "Shared memory permits hostile programs to bypass ownership checks.",End,
        TAG_MORE,(IPTR)message->ops_AttrList);
    if(self) {
        struct KeyringEditor_DATA *data=INST_DATA(CLASS,self);
        data->state=state; data->result=result;
        SET(lock,MUIA_ObjectID,MAKE_ID('K','R','L','K'));
        SET(unlock,MUIA_ObjectID,MAKE_ID('K','R','U','L'));
        SET(refresh,MUIA_ObjectID,MAKE_ID('K','R','R','F'));
        DoMethod(lock,MUIM_Notify,MUIA_Pressed,FALSE,self,1,MUIM_KeyringEditor_Lock);
        DoMethod(unlock,MUIM_Notify,MUIA_Pressed,FALSE,self,1,MUIM_KeyringEditor_Unlock);
        DoMethod(refresh,MUIM_Notify,MUIA_Pressed,FALSE,self,1,MUIM_KeyringEditor_Refresh);
    }
    return (IPTR)self;
}
IPTR KeyringEditor__MUIM_KeyringEditor_Refresh(Class *CLASS,Object *self,Msg message)
{
    struct KeyringEditor_DATA *data=INST_DATA(CLASS,self);
    struct Library *KeyringBase=OpenLibrary(KEYRINGNAME,1);
    (void)message;
    SET(data->state,MUIA_Text_Contents,(IPTR)(KeyringBase?
        (KR_IsLocked()?"Locked":"Unlocked"):"Keyring unavailable"));
    if(KeyringBase) CloseLibrary(KeyringBase);
    return TRUE;
}
static IPTR action(Class *CLASS,Object *self,int unlock)
{
    struct KeyringEditor_DATA *data=INST_DATA(CLASS,self);
    struct Library *KeyringBase=OpenLibrary(KEYRINGNAME,1);
    LONG rc=KR_ERR_CONTEXT;
    if(KeyringBase) {
        if(unlock) rc=KR_Unlock(); else { KR_Lock(); rc=0; }
        CloseLibrary(KeyringBase);
    }
    SET(data->result,MUIA_Text_Contents,(IPTR)(rc?
        "Operation failed or cancelled; the keyring state is shown above.":
        (unlock?"Session unlocked.":"Session locked.")));
    DoMethod(self,MUIM_KeyringEditor_Refresh);
    return rc==0;
}
IPTR KeyringEditor__MUIM_KeyringEditor_Lock(Class *CLASS,Object *self,Msg message)
{ (void)message; return action(CLASS,self,FALSE); }
IPTR KeyringEditor__MUIM_KeyringEditor_Unlock(Class *CLASS,Object *self,Msg message)
{ (void)message; return action(CLASS,self,TRUE); }
IPTR KeyringEditor__OM_GET(Class *CLASS,Object *self,struct opGet *message)
{
    if(message->opg_AttrID==MUIA_PrefsEditor_CanSave) {
        *message->opg_Storage=FALSE; return TRUE;
    }
    return DoSuperMethodA(CLASS,self,(Msg)message);
}
IPTR KeyringEditor__MUIM_Setup(Class *CLASS,Object *self,Msg message)
{
    if(!DoSuperMethodA(CLASS,self,message)) return FALSE;
    DoMethod(self,MUIM_KeyringEditor_Refresh); return TRUE;
}
IPTR KeyringEditor__MUIM_PrefsEditor_ImportFromDirectory(Class *CLASS,Object *self,Msg message)
{ (void)CLASS; (void)self; (void)message; return TRUE; }
IPTR KeyringEditor__MUIM_PrefsEditor_ExportToDirectory(Class *CLASS,Object *self,Msg message)
{ (void)CLASS; (void)self; (void)message; return TRUE; }
