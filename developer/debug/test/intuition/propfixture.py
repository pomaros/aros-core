# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License 1.1.
"""Shared CURRENT native PropGClass/capture/geometry source fixture.

The C declarations model only fields used by extracted methods. PropGData and
capture structures come from this checkout; no older class/math mixtures.
Rendering, superclass/object dispatch and locks are explicit adapters.
"""
from pathlib import Path
import hashlib,re
CORE=Path(__file__).resolve().parents[4]
def structure(text,name):
    return re.search(r'struct '+name+r'\s*\{.*?\n\};',text,re.S).group(0)
def fn(src,name):
 m=re.search(r'(?:static\s+(?:inline\s+)?)?(?:VOID|void|BOOL|LONG|WORD|UWORD|IPTR|QUAD|int|const struct PropInputPosition|struct Gadget)\s+\*?'+name+r'\s*\([^;]*?\)\s*\{',src);assert m,name
 i=m.end();depth=1
 while depth:depth+=(src[i]=='{')-(src[i]=='}');i+=1
 return src[m.start():i]

def native_fixture(mutant=""):
    core=CORE
    sources={str(Path(__file__).resolve()):hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    def read(name):
     p=core/name;sources[str(p)]=hashlib.sha256(p.read_bytes()).hexdigest();return p.read_text(encoding='latin1')
    prop=read('rom/intuition/propgadgets.c');cl=read('rom/intuition/propgclass.c');internal=read('rom/intuition/intuition_intern.h');gadgets=read('rom/intuition/gadgets.c');composition=read('rom/intuition/windowcomposition.c');inputsrc=read('rom/intuition/inputhandler_support.c');raw=read('rom/intuition/inputhandler.c');scale=read('rom/graphics/scalecompose.h');pub=read('compiler/include/intuition/intuition.h')
    if mutant=='local-filter':cl=cl.replace('        case IECODE_NOBUTTON:','        case IECODE_NOBUTTON: if (msg->gpi_Mouse.X == data->last_x && msg->gpi_Mouse.Y == data->last_y) break;')
    if mutant=='no-track-rebase':prop=prop.replace('axis->track != track || ','')
    if mutant=='no-owner-rebase':prop=prop.replace(' || axis->pot != pot || axis->body != body','')
    if mutant=='discard-phase':prop=prop.replace('first + axis->pointer - axis->cell','first')
    if mutant=='no-frame-rebase':prop=prop.replace('drag->factor != factor || drag->originX != ox + box.Left * factor','FALSE').replace('drag->factor != factor || drag->originY != oy + box.Top * factor','FALSE')
    if mutant=='no-inactive-clear':cl=cl.replace('    if (GetPrivIBase(IntuitionBase)->prop_drag.gadget == g)\n        GetPrivIBase(IntuitionBase)->prop_drag.gadget = NULL;','')
    if mutant=='ignore-domain':prop=prop.replace('(dx + box.Left)','dx').replace('(dy + box.Top)','dy')
    if mutant=='endpoint-deadzone':prop=prop.replace('        axis->anchor = pointer;\n        axis->anchorPot = value;','')
    if mutant=='floor-pot':cl=cl.replace('(((ULONG)top * MAXPOT) + hidden / 2)','((ULONG)top * MAXPOT)')
    if mutant=='ignore-native-position':prop=prop.replace('x = position->x; y = position->y;','/* ignore native event point */')
    if mutant=='borrow-context':prop=prop.replace('input->message == message && ','')
    if mutant=='lose-context-restore':inputsrc=inputsrc.replace('    *input = saved;','')
    # Constants come from the actual public header.
    names=['FREEHORIZ','FREEVERT','PROPBORDERLESS','PROPNEWLOOK','KNOBHIT','AUTOKNOB','KNOBHMIN','KNOBVMIN','MAXPOT','MAXBODY','GFLG_SELECTED','GFLG_RELRIGHT','GFLG_RELBOTTOM','GFLG_RELWIDTH','GFLG_RELHEIGHT','GTYP_GTYPEMASK','GTYP_GADGETTYPE','GTYP_SYSGADGET','GTYP_SCRGADGET','GTYP_GZZGADGET','GTYP_REQGADGET','GTYP_PROPGADGET','GTYP_CUSTOMGADGET','WFLG_GIMMEZEROZERO']
    const='\n'.join(re.search(r'^#define\s+'+n+r'\b[^\n]*',pub,re.M).group(0) for n in names)
    c=r'''
    #include <stdint.h>
    #include <assert.h>
    #include <stdio.h>
    typedef int64_t QUAD;typedef uint16_t UWORD;typedef int16_t WORD;typedef uint32_t ULONG;typedef int32_t LONG;typedef uintptr_t IPTR;typedef int BOOL;
    #define VOID void
    #define TRUE 1
    #define FALSE 0
    #define D(x)
    #define IECLASS_RAWMOUSE 2
    #define IECODE_NOBUTTON 255
    #define SELECTUP 232
    #define GMR_MEACTIVE 1
    #define GMR_NOREUSE 2
    #define GMR_VERIFY 4
    #define SPFLAG_WANTBUFFER 1
    struct IBox{WORD Left,Top,Width,Height;};
    #define BBox IBox
    struct Screen{WORD MouseX,MouseY,Width,Height,LeftEdge,TopEdge;UWORD sx,sy;};
    struct ScaleComposeTransform{LONG x,y;UWORD factor;};
    struct WindowComposition{struct ScaleComposeTransform transform;int damageLock;};
    struct Window{struct Screen*WScreen;WORD LeftEdge,TopEdge,Width,Height,MouseX,MouseY;UWORD BorderLeft,BorderTop,BorderRight,BorderBottom;ULONG Flags;};
    struct IntWindow{struct Window W;struct WindowComposition*Composition;ULONG specialflags;};
    struct Requester{WORD LeftEdge,TopEdge,Width,Height;};
    struct Image{WORD Width,Height;};
    struct Gadget{UWORD Flags,GadgetType;WORD LeftEdge,TopEdge,Width,Height;void*SpecialInfo;struct Image*GadgetRender;};
    struct PropInfo{UWORD Flags,HorizPot,VertPot,HorizBody,VertBody,CWidth,CHeight,HPotRes,VPotRes,LeftBorder,TopBorder;};
    struct GadgetInfo{struct Window*gi_Window;struct Requester*gi_Requester;struct Screen*gi_Screen;struct IBox gi_Domain;};
    struct InputEvent{ULONG ie_Class,ie_Code;};
    struct gpInput{struct GadgetInfo*gpi_GInfo;struct InputEvent*gpi_IEvent;struct{WORD X,Y;}gpi_Mouse;LONG*gpi_Termination;};
    struct gpGoInactive{struct GadgetInfo*gpgi_GInfo;ULONG gpgi_Abort;};
    struct PropGData{struct PropInfo propinfo;UWORD last_x,last_y,top,visible,total;};
    typedef struct Class{IPTR cl_UserData;}Class;
    #define INST_DATA(cl,g) ((struct PropGData*)(g)->SpecialInfo)
    #define GetPrivIBase(b) (b)
    #define IW(w) ((struct IntWindow*)(w))
    #define IS_SCREEN_GADGET(g) (((g)->GadgetType&GTYP_GADGETTYPE)==GTYP_SCRGADGET)
    #define ADDREL(g,flag,domain,field) (((g)->Flags&(flag))?((domain)->field):0)
    static void ObtainSemaphore(int*l){}
    static void ReleaseSemaphore(int*l){}
    static void SetGadgetType(struct Gadget*g,UWORD type){g->GadgetType=(g->GadgetType&~GTYP_GTYPEMASK)|type;}
    static unsigned notifications;
    static void NotifyTop(Class*cl,struct Gadget*g,struct GadgetInfo*gi,BOOL final){notifications++;}
    static void RefreshPropGadget(struct Gadget*g,struct Window*w,struct Requester*r,struct IntuitionBase*b){}
    static void NewModifyProp(struct Gadget*g,struct Window*w,struct Requester*r,UWORD flags,LONG hp,LONG vp,UWORD hb,UWORD vb,int n){struct PropInfo*pi=g->SpecialInfo;pi->Flags=flags;pi->HorizPot=hp;pi->VertPot=vp;pi->HorizBody=hb;pi->VertBody=vb;}
    '''
    a=internal.index('struct PropInputPosition');b=internal.index('/* IntuitionBase */',a)
    c=c.replace('static void RefreshPropGadget',internal[a:b]+'\nstruct IntuitionBase{struct PropInputDispatch prop_input;struct PropDragState prop_drag;void*UtilityBase;};\nstatic void RefreshPropGadget')
    c=c.replace('struct PropInfo{UWORD Flags,HorizPot,VertPot,HorizBody,VertBody,CWidth,CHeight,HPotRes,VPotRes,LeftBorder,TopBorder;};',structure(pub,'PropInfo'))
    c=c.replace('struct PropGData{struct PropInfo propinfo;UWORD last_x,last_y,top,visible,total;};',structure(internal,'PropGData'))
    c=const+'\n'+c
    for text,names in [(gadgets,['GetGadgetDomain','GetGadgetLeft','GetGadgetTop','GetWinGadgetIBox','GetDomGadgetIBox','CalcBBox']),(scale,['ScaleComposeLong','ScaleComposeFloor','ScaleComposeInput']),(composition,['WindowCompositionInputFrame','WindowCompositionMouse']),(inputsrc,['SetGPIMouseCoords']),(prop,['PropInputForMessage','CalcKnobSize','PropPointer','PropDragPot','PropCapture','HandlePropSelectDown','HandlePropSelectUp','HandlePropMouseMove']),(cl,['FindScrollerValues','FindScrollerTop','UpdateTop','PropGClass__GM_GOACTIVE','PropGClass__GM_HANDLEINPUT','PropGClass__GM_GOINACTIVE'])]:
     c+='\n'+'\n'.join(fn(text,n) for n in names)
    # The native dispatcher is real; only Locked_DoMethodA and its return handler
    # are adapters. Process_RawMouse normalization is compiled as an OS object and
    # source-bound below, rather than simulated as the full input event loop.
    c=c.replace('typedef int64_t QUAD;', 'typedef void*Msg;typedef ULONG_PLACEHOLDER STACKULONG;typedef int64_t QUAD;').replace('typedef ULONG_PLACEHOLDER STACKULONG;','typedef uint32_t STACKULONG;')
    c=c.replace('struct InputEvent{ULONG ie_Class,ie_Code;};','struct InputEvent{ULONG ie_Class,ie_Code,ie_Qualifier;LONG ie_X,ie_Y;};')
    c=c.replace('struct gpInput{struct GadgetInfo*','struct gpInput{ULONG MethodID;struct GadgetInfo*').replace('LONG*gpi_Termination;};','ULONG*gpi_Termination;void*gpi_TabletData;};')
    c=c.replace('struct IntuitionBase{','struct IIHData{ULONG ActQualifier;};struct Interrupt{void*is_Data;};struct IntuitionBase{LONG MouseX,MouseY;struct Interrupt*InputHandler;')
    c='#define GM_GOACTIVE 1\n#define GM_HANDLEINPUT 2\n#define DEBUG_DOGPINPUT(x)\n'+c
    c+=r'''
    static unsigned dispatches,nested_checks,nested_dispatches;
static IPTR last_result;
    static struct Gadget *DoGPInput(struct GadgetInfo *, struct Gadget *, struct InputEvent *, STACKULONG, BOOL *, struct IntuitionBase *);
    static IPTR Locked_DoMethodA(struct Window*w,struct Gadget*g,Msg msg,struct IntuitionBase*base){
     struct gpInput*in=msg;Class cl={(IPTR)base};
     if(in->MethodID==99){assert(!base->prop_input.message);assert(!PropInputForMessage(g,in,base));nested_dispatches++;return 0;}
     if(base->prop_input.message){
      assert(PropInputForMessage(g,in,base));
      struct gpInput nested=*in;assert(!PropInputForMessage(g,&nested,base));
      WORD old=in->gpi_Mouse.X;in->gpi_Mouse.X++;assert(!PropInputForMessage(g,in,base));in->gpi_Mouse.X=old;
      const struct gpInput *saved=base->prop_input.message;
      struct InputEvent fabricated={IECLASS_RAWMOUSE,IECODE_NOBUTTON,0};BOOL reuse=FALSE;
      DoGPInput(in->gpi_GInfo,g,&fabricated,99,&reuse,base);
      assert(base->prop_input.message==saved);assert(PropInputForMessage(g,in,base));
      struct InputEvent *old_event=in->gpi_IEvent;in->gpi_IEvent=&fabricated;assert(!PropInputForMessage(g,in,base));in->gpi_IEvent=old_event;
      struct GadgetInfo nested_gi=*in->gpi_GInfo;struct GadgetInfo *old_gi=in->gpi_GInfo;in->gpi_GInfo=&nested_gi;assert(!PropInputForMessage(g,in,base));in->gpi_GInfo=old_gi;
      nested_checks++;
     }
     dispatches++;
     return in->MethodID==GM_GOACTIVE?PropGClass__GM_GOACTIVE(&cl,g,in):PropGClass__GM_HANDLEINPUT(&cl,g,in);
    }
    static struct Gadget*HandleCustomGadgetRetVal(IPTR ret,struct GadgetInfo*gi,struct Gadget*g,ULONG term,BOOL*reuse,struct IntuitionBase*base){last_result=ret;return g;}
    '''
    c+=fn(inputsrc,'DoGPInputAt')+'\n'+fn(inputsrc,'DoGPInput')
    # Bind the exact native event point snapshots, separately from graphics/events
    # which the host does not emulate. Full Process_RawMouse is cross-compiled.
    button=re.search(r'    position.screen = w \? w->WScreen : screen;.*?position.y = .*?;',raw,re.S).group(0)
    movement=re.search(r'        if \(position.screen\)\n        \{\n            position.x = ie->ie_X.*?\n        \}',raw,re.S).group(0)
    assert '&newie,\n                   GM_HANDLEINPUT,\n                   &reuse_event,\n                   &position' not in raw
    c+='\n#define int_MouseScaleX(s) ((s)->sx)\n#define int_MouseScaleY(s) ((s)->sy)\n'
    c+='static struct PropInputPosition button_point(struct Window*w,struct Screen*screen,struct IntuitionBase*IntuitionBase){struct PropInputPosition position;'+button+'return position;}\n'
    c+='static struct PropInputPosition movement_point(struct PropInputPosition position,struct InputEvent*ie){'+movement+'return position;}\n'


    # Tags and object/raster operations are explicit OS adapters; model functions
    # and all capture/geometry/input methods above come from this checkout.
    tags=read('compiler/include/intuition/gadgetclass.h')
    constants='\n'.join(l for l in (pub+'\n'+tags).splitlines() if re.match(r'#define\s+(PGA_\w+|GA_(Dummy|Image|Border|Highlight|ID)|GFLG_(DISABLED|GADGHBOX|GADGHCOMP))\b',l))
    c=constants+'\n'+c
    c=c.replace('typedef void*Msg;', 'typedef void Object;typedef void*Msg;')
    c=c.replace('UWORD Flags,GadgetType;', 'UWORD Flags,GadgetType,GadgetID;')
    adapters=r'''
    #define TAG_USER 0x80000000UL
    #define TAG_END 0
    #define TAG_DONE TAG_END
    #define GM_RENDER 3
    #define OM_SET 10
    #define OM_NOTIFY 11
    #define PRIVFLAG_NICERENDER 1
    #define PRIVFLAG_NICENOTIFY 2
    #define GREDRAW_REDRAW 1
    #define GREDRAW_UPDATE 2
    #define OPUF_INTERIM 1
    #define OCLASS(g) cl
    #define SETFLAG(value,condition,flag) do{if(condition)(value)|=(flag);else (value)&=~(flag);}while(0)
    struct TagItem{ULONG ti_Tag;IPTR ti_Data;};
    struct opSet{ULONG MethodID;struct TagItem*ops_AttrList;struct GadgetInfo*ops_GInfo;};
    struct opGet{ULONG MethodID,opg_AttrID;IPTR*opg_Storage;};
    struct opUpdate{ULONG MethodID;struct TagItem*opu_AttrList;struct GadgetInfo*opu_GInfo;ULONG opu_Flags;};
    struct gpRender{ULONG MethodID;struct GadgetInfo*gpr_GInfo;struct RastPort*gpr_RPort;ULONG gpr_Redraw;};
    static struct TagItem*NextTagItem(struct TagItem**scan){struct TagItem*t=(*scan)++;return t->ti_Tag?t:NULL;}
    static struct RastPort*ObtainGIRPort(struct GadgetInfo*g){assert(0);return NULL;}
    static void ReleaseGIRPort(struct RastPort*r){assert(0);}
    static void DoMethodA(Object*g,Msg m){assert(0);}
    static IPTR DoSuperMethodA(Class*c,Object*g,Msg m){if(*(ULONG*)m==OM_NOTIFY)notifications++;return 0;}
    '''
    c=c.replace('static void NotifyTop(Class*cl,struct Gadget*g,struct GadgetInfo*gi,BOOL final){notifications++;}',adapters+'\n'+fn(cl,'NotifyTop'))
    c+='\n'+fn(cl,'PropGClass__OM_SET')+'\n'+fn(cl,'PropGClass__OM_GET')
    return c,sources

# Original event positions are supplied to the real DoGPInputAt dispatcher.
# Window-relative message coordinates and domains use the actual OS functions;
# monitor acceleration/queueing and the full event loop are outside this host.
EVENT_HELPER=r'''
static IPTR native_event(struct Gadget*g,struct GadgetInfo*gi,struct IntuitionBase*base,LONG x,LONG y,ULONG method,ULONG code){
 LONG lx,ly;assert(WindowCompositionMouse(gi->gi_Window,x,y,&lx,&ly));gi->gi_Window->MouseX=lx;gi->gi_Window->MouseY=ly;
 GetGadgetDomain(g,gi->gi_Screen,gi->gi_Window,gi->gi_Requester,&gi->gi_Domain);
 struct InputEvent ie={.ie_Class=IECLASS_RAWMOUSE,.ie_Code=code};BOOL reuse=FALSE;
 struct PropInputPosition point={gi->gi_Screen,x,y};DoGPInputAt(gi,g,&ie,method,&reuse,&point,base);assert(!base->prop_input.message);return last_result;
}
'''
