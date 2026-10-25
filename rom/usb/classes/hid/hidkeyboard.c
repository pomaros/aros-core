/*
 * Copyright (C) 2026 John KNIPPER
 * Licensed under the AROS Public License (APL) Version 1.1.
 * VM hardware keyboard backend. Only interrupt-IN reports enter this path;
 * configured actions, key strings and macros retain public IND_* injection.
 */
#define __OOP_NOATTRBASES__
#define __OOP_NOMETHODBASES__
#include "hid.class.h"
#include "hidkeyboard.h"
#if defined(AROS_VM_GUEST)
#include <hidd/hidd.h>
#include <hidd/input.h>
#include <hidd/keyboard.h>
#include <proto/oop.h>
#include <proto/utility.h>
#include <string.h>
#undef UtilityBase
#define UtilityBase (nh->nh_UtilityBase)
#undef HiddAttrBase
#undef HWAttrBase
#undef HWBase
#undef HWInputBase
#define HiddAttrBase (nh->nh_KeyboardAB[0])
#define HWAttrBase (nh->nh_KeyboardAB[1])
#define HWBase (nh->nh_KeyboardMB[0])
#define HWInputBase (nh->nh_KeyboardMB[1])

/* Descriptor state only: never inspect a user action or configured keymap. */
BOOL nKeyboardItem(struct NepHidItem *item)
{
    ULONG i;
    if(!item || item->nhi_Type!=REPORT_MAIN_INPUT) return FALSE;
    if(item->nhi_Flags & RPF_MAIN_VARIABLE)
        return (item->nhi_Usage>>16)==7;
    if(!item->nhi_UsageMap) return FALSE;
    if(item->nhi_MapSize>65536) return TRUE; /* unbounded map: claim, then reject */
    for(i=0;i<item->nhi_MapSize;i++)
        if((item->nhi_UsageMap[i]>>16)==7) return TRUE;
    return FALSE;
}

/* Decode a single field into a separate bounded page07 bitmap. A malformed
 * keyboard field or ErrorRollOver rejects the entire report atomically. */
static int keyboard_bits(const UBYTE *bytes,ULONG length,ULONG offset,ULONG width,ULONG *value)
{
    ULONG i,v=0;
    if(length>65536 || !width || width>32 || offset/8>=length ||
       width>(length-offset/8)*8-(offset&7)) return 0;
    for(i=0;i<width;i++)
        v|=(ULONG)((bytes[(offset+i)/8]>>((offset+i)&7))&1)<<i;
    *value=v;return 1;
}
static int keyboard_decode(struct NepHidItem *item,const UBYTE *bytes,ULONG length,UBYTE keys[32])
{
    ULONG i,count,offset=item->nhi_Offset,value,usage;
    if(!nKeyboardItem(item)) return 1;
    if(item->nhi_MapSize>65536 || (item->nhi_Flags & (RPF_MAIN_CONST|RPF_MAIN_RELATIVE)) || item->nhi_IsSigned)
        return 0;
    count=(item->nhi_Flags&RPF_MAIN_VARIABLE)?1:item->nhi_Count;
    if(!count || count>length*8 || !item->nhi_Size || item->nhi_Size>32) return 0;
    for(i=0;i<count;i++) {
        if(!keyboard_bits(bytes,length,offset,item->nhi_Size,&value)) return 0;
        offset+=item->nhi_Size;
        if(item->nhi_Flags&RPF_MAIN_VARIABLE) {
            if(item->nhi_LogicalMin!=0 || item->nhi_LogicalMax!=1 || value>1) return 0;
            if(!value) continue;
            usage=item->nhi_Usage;
        } else {
            if(item->nhi_LogicalMin<0 || value<(ULONG)item->nhi_LogicalMin ||
               value>(ULONG)item->nhi_LogicalMax ||
               value-(ULONG)item->nhi_LogicalMin>=item->nhi_MapSize) return 0;
            usage=item->nhi_UsageMap[value-(ULONG)item->nhi_LogicalMin];
        }
        if((usage>>16)!=7) continue;
        usage&=0xffff;
        if(usage>=1 && usage<=3) return 0; /* ErrorRollOver/POSTFail/Undefined */
        if(usage>=4 && usage<=0xe7) keys[usage/8]|=1U<<(usage&7);
    }
    return 1;
}

static OOP_Object *KeyboardNew(OOP_Class *cl,OOP_Object *o,struct pRoot_New *msg)
{
    return (OOP_Object *)OOP_DoSuperMethod(cl,o,(OOP_Msg)msg);
}
static BOOL keyboard_init(struct NepHidBase *nh)
{
    BOOL ok=FALSE;
    ObtainSemaphore(&nh->nh_CtrlLock);
    if(nh->nh_KeyboardClass) { ReleaseSemaphore(&nh->nh_CtrlLock);return TRUE; }
    nh->nh_KeyboardHiddBase=OpenLibrary("keyboard.hidd",0);
    if(nh->nh_KeyboardHiddBase) {
        struct OOP_ABDescr abs[]={
            {IID_Hidd,(OOP_AttrBase *)&nh->nh_KeyboardAB[0]},
            {IID_HW,(OOP_AttrBase *)&nh->nh_KeyboardAB[1]},
            {IID_Hidd_Input,(OOP_AttrBase *)&nh->nh_KeyboardAB[2]}, {NULL,NULL}};
        if(OOP_ObtainAttrBases(abs)) {
            OOP_AttrBase MetaAttrBase=OOP_ObtainAttrBase(IID_Meta);
            nh->nh_KeyboardMB[0]=OOP_GetMethodID(IID_HW,0);
            nh->nh_KeyboardMB[1]=OOP_GetMethodID(IID_HW_Input,0);
            nh->nh_KeyboardHW=OOP_NewObject(NULL,CLID_HW_Kbd,NULL);
            if(MetaAttrBase && nh->nh_KeyboardHW) {
                struct OOP_MethodDescr root[]={{(OOP_MethodFunc)KeyboardNew,moRoot_New},{NULL,0}};
                struct OOP_InterfaceDescr interfaces[]={{root,IID_Root,1},{NULL,NULL,0}};
                struct TagItem tags[]={
                    {aMeta_SuperID,(IPTR)CLID_Hidd_Kbd},
                    {aMeta_ID,(IPTR)"hidd.input.usbrawkbd"},
                    {aMeta_InterfaceDescr,(IPTR)interfaces},
                    {aMeta_InstSize,0},{TAG_DONE,0}};
                nh->nh_KeyboardClass=OOP_NewObject(NULL,CLID_HiddMeta,tags);
                ok=nh->nh_KeyboardClass!=NULL;
            }
            if(MetaAttrBase) OOP_ReleaseAttrBase(IID_Meta);
            if(!ok) OOP_ReleaseAttrBases(abs);
        }
        if(!ok) { CloseLibrary(nh->nh_KeyboardHiddBase);nh->nh_KeyboardHiddBase=NULL; }
    }
    ReleaseSemaphore(&nh->nh_CtrlLock);return ok;
}

static const UBYTE physical_keymap[256] =
{
    0xff, 0xff, 0xff, 0xff, 0x20, 0x35, 0x33, 0x22,  /* 0x00 */
    0x12, 0x23, 0x24, 0x25, 0x17, 0x26, 0x27, 0x28,  /* 0x08 */
    0x37, 0x36, 0x18, 0x19, 0x10, 0x13, 0x21, 0x14,  /* 0x10 */
    0x16, 0x34, 0x11, 0x32, 0x15, 0x31, 0x01, 0x02,  /* 0x18 */
    0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,  /* 0x20 */
    0x44, 0x45, 0x41, 0x42, 0x40, 0x0b, 0x0c, 0x1a,  /* 0x28 */
    0x1b, 0x0d, 0x2b, 0x29, 0x2a, 0x00, 0x38, 0x39,  /* 0x30 */
    0x3a, 0x62, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55,  /* 0x38 */
    0x56, 0x57, 0x58, 0x59, 0x5f, 0x67, 0xff, 0xff,  /* 0x40 */
    0x6e, 0x47, 0x70, 0x48, 0x46, 0x71, 0x49, 0x4e,  /* 0x48 */
    0x4f, 0x4d, 0x4c, 0xff, 0x5c, 0x5d, 0x4a, 0x5e,  /* 0x50 */
    0x43, 0x1d, 0x1e, 0x1f, 0x2d, 0x2e, 0x2f, 0x3d,  /* 0x58 */
    0x3e, 0x3f, 0x0f, 0x3c, 0x30, 0x67, 0xff, 0xff,  /* 0x60 */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  /* 0x68 F13-F24 */
    0xff, 0xff, 0xff, 0xff, 0xff, 0x5f, 0xff, 0xff,  /* 0x70 */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  /* 0x78 */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  /* 0x80 */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  /* 0x88 */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  /* 0x90 */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x44, 0xff,  /* 0x98 */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  /* 0xa0 */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  /* 0xa8 */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  /* 0xb0 */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  /* 0xb8 */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  /* 0xc0 */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  /* 0xc8 */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  /* 0xd0 */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  /* 0xd8 */
    0x63, 0x60, 0x64, 0x66, 0x63, 0x61, 0x65, 0x67,  /* 0xe0 */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  /* 0xe8 */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  /* 0xf0 */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff   /* 0xf8 */
};

static void keyboard_raw_state(struct NepClassHid *nch,UBYTE raw[16])
{
    struct NepHidReport *r;ULONG usage;
    memset(raw,0,16);
    for(r=(struct NepHidReport *)nch->nch_HidReports.lh_Head;r->nhr_Node.ln_Succ;r=(struct NepHidReport *)r->nhr_Node.ln_Succ)
        for(usage=4;usage<=0xe7;usage++)
            if(r->nhr_PhysicalKeys[usage/8]&(1U<<(usage&7))) {
                UBYTE code=physical_keymap[usage];
                if(code<128) raw[code/8]|=1U<<(code&7);
            }
}
static void keyboard_push(struct NepClassHid *nch,UWORD code)
{
    struct NepHidBase *nh=nch->nch_ClsBase;
    struct pHidd_Kbd_Event event;
    event.flags=0;event.code=code;
    /* Same interrupt discipline as other keyboard HIDD producers. No trace
     * includes a key, report byte, configured action or credential. */
    Disable();
    HW_Input_PushEvent((OOP_Object *)nh->nh_KeyboardHW,(OOP_Object *)nch->nch_KeyboardDevice,&event);
    Enable();
}
static void keyboard_diff(struct NepClassHid *nch,const UBYTE old[16],const UBYTE now[16])
{
    ULONG phase,code;
    if(!nch->nch_KeyboardDevice) return;
    /* Qualifiers before printable presses; releases before presses. */
    for(phase=0;phase<4;phase++) for(code=0;code<128;code++) {
        BOOL was=(old[code/8]&(1U<<(code&7)))!=0;
        BOOL is=(now[code/8]&(1U<<(code&7)))!=0;
        BOOL qualifier=code>=0x60 && code<=0x67;
        if(was==is || qualifier!=(phase%2==0) || is!=(phase>=2)) continue;
        keyboard_push(nch,(UWORD)(code|(is?0:IECODE_UP_PREFIX)));
    }
}
BOOL nKeyboardReport(struct NepClassHid *nch,struct NepHidReport *report,const UBYTE *bytes,ULONG length,BOOL *valid)
{
    struct NepHidBase *nh=nch->nch_ClsBase;
    UBYTE keys[32]={0},old[16],now[16];ULONG i;BOOL claimed=FALSE;
    *valid=FALSE;
    if(!report || !report->nhr_InItemMap || report->nhr_ReportID==0xffff) return FALSE;
    for(i=0;i<report->nhr_InItemCount;i++)
        if(nKeyboardItem(report->nhr_InItemMap[i])) claimed=TRUE;
    if(!claimed) return FALSE;
    /* Claim even invalid packets or unavailable backend: never replay a
     * physical keyboard report through user mappings, debug or GUI paths. */
    if(!length || length>nch->nch_MaxInSize || length>65536 || length<(report->nhr_ReportInSize/8+((report->nhr_ReportInSize&7)!=0))) return TRUE;
    for(i=0;i<report->nhr_InItemCount;i++)
        if(report->nhr_InItemMap[i] && !keyboard_decode(report->nhr_InItemMap[i],bytes,length,keys)) return TRUE;
    /* The legacy non-keyboard decoder must also remain in actual bounds. */
    for(i=0;i<report->nhr_InItemCount;i++) {
        struct NepHidItem *item=report->nhr_InItemMap[i];
        ULONG value,count;
        if(!item) continue;
        count=(item->nhi_Flags&RPF_MAIN_VARIABLE)?1:item->nhi_Count;
        if(!count || !item->nhi_Size || item->nhi_Size>32 ||
           count>length*8 || item->nhi_Offset>=length*8 || count>(length*8-item->nhi_Offset)/item->nhi_Size ||
           !keyboard_bits(bytes,length,item->nhi_Offset,item->nhi_Size,&value)) return TRUE;
    }
    *valid=TRUE;
    if(!nch->nch_KeyboardDevice) {
        if(!keyboard_init(nh)) return TRUE;
        { struct TagItem tags[]={{aHidd_Name,(IPTR)"hid.class"},
            {aHidd_HardwareName,(IPTR)"USB physical keyboard"},{TAG_DONE,0}};
        nch->nch_KeyboardDevice=HW_AddDriver((OOP_Object *)nh->nh_KeyboardHW,(OOP_Class *)nh->nh_KeyboardClass,tags);
        if(!nch->nch_KeyboardDevice) return TRUE;
        }
    }
    keyboard_raw_state(nch,old);
    memcpy(report->nhr_PhysicalKeys,keys,sizeof(keys));
    keyboard_raw_state(nch,now);keyboard_diff(nch,old,now);
    return TRUE;
}
void nKeyboardDetach(struct NepClassHid *nch)
{
    struct NepHidBase *nh=nch->nch_ClsBase;struct NepHidReport *r;
    UBYTE old[16],now[16]={0};
    keyboard_raw_state(nch,old);
    for(r=(struct NepHidReport *)nch->nch_HidReports.lh_Head;r->nhr_Node.ln_Succ;r=(struct NepHidReport *)r->nhr_Node.ln_Succ)
        memset(r->nhr_PhysicalKeys,0,sizeof(r->nhr_PhysicalKeys));
    keyboard_diff(nch,old,now);
    if(nch->nch_KeyboardDevice) {
        HW_RemoveDriver((OOP_Object *)nh->nh_KeyboardHW,(OOP_Object *)nch->nch_KeyboardDevice);
        nch->nch_KeyboardDevice=NULL;
    }
}
void nKeyboardExit(struct NepHidBase *nh)
{
    if(nh->nh_KeyboardClass) {
        struct OOP_ABDescr abs[]={
            {IID_Hidd,(OOP_AttrBase *)&nh->nh_KeyboardAB[0]},
            {IID_HW,(OOP_AttrBase *)&nh->nh_KeyboardAB[1]},
            {IID_Hidd_Input,(OOP_AttrBase *)&nh->nh_KeyboardAB[2]},{NULL,NULL}};
        OOP_DisposeObject(nh->nh_KeyboardClass);nh->nh_KeyboardClass=NULL;
        OOP_ReleaseAttrBases(abs);
    }
    if(nh->nh_KeyboardHiddBase) { CloseLibrary(nh->nh_KeyboardHiddBase);nh->nh_KeyboardHiddBase=NULL; }
}
#endif
