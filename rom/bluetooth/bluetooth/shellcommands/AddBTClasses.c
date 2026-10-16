/*
** AddBTClasses (ported from the Poseidon shell commands by Chris Hodges)
*/

#include <aros/isoascii.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <exec/exec.h>
#include <dos/dosextens.h>
#include <dos/datetime.h>
#include <dos/exall.h>
#include <libraries/bluetooth.h>
#include <proto/bluetooth.h>
#include <proto/exec.h>
#include <proto/dos.h>

#define ARGS_QUIET      0
#define ARGS_REMOVE     1
#define ARGS_FROM       2
#define ARGS_SIZEOF     3

#define CLASSPATH       "SYS:Classes/Bluetooth"
#define CLASSNAMEMAX    128

/* FROM: a class file, or a directory holding only classes, instead of
   SYS:Classes/Bluetooth - a development build is added from its depot. */
static const char *template = "QUIET/S,REMOVE/S,FROM/K";
const char *version = "$VER: AddBTClasses 1.0 (18.08.2026) " ISOASCII_COPYRIGHT " The AROS Development Team";
static IPTR ArgsArray[ARGS_SIZEOF];
static struct RDArgs *ArgsHook = NULL;

void fail(char *str)
{
    if(ArgsHook)
    {
        FreeArgs(ArgsHook);
        ArgsHook = NULL;
    }
    if(str)
    {
        PutStr(str);
        exit(20);
    }
    exit(0);
}

int main(int argc, char *argv[])
{
    UBYTE               buf[1024];
    struct Library      *BluetoothBase;
    struct ExAllControl *exall;
    struct ExAllData    *exdata;
    STRPTR              errmsg = NULL;
    struct List         *bclist;
    struct Node         *bc;
    UBYTE               sbuf[CLASSNAMEMAX];
    BPTR                lock;
    ULONG               ents;
    ULONG               namelen;
    BOOL                exready, isvalid;
    STRPTR              path = CLASSPATH;
    struct FileInfoBlock *fib;
    LONG                dir = 1;

    if(!(ArgsHook = ReadArgs(template, ArgsArray, NULL)))
    {
        fail("Wrong arguments!\n");
    }
    
    if((BluetoothBase = OpenLibrary("bluetooth.library", 1)))
    {
        if(ArgsArray[ARGS_REMOVE])
        {
            btLockWriteBase();
            btGetAttrs(BGA_STACK, NULL, BSA_ClassList, &bclist, TAG_END);
            while(bclist->lh_Head->ln_Succ)
            {
                if(!ArgsArray[ARGS_QUIET])
                {
                    Printf("Removing class %s...\n", bclist->lh_Head->ln_Name);
                }
                btUnlockBase();
                btRemClass(bclist->lh_Head);
                btLockWriteBase();
            }
            btUnlockBase();
        } else {
            if(ArgsArray[ARGS_FROM])
            {
                path = (STRPTR) ArgsArray[ARGS_FROM];
                if((lock = Lock(path, ACCESS_READ)))
                {
                    if((fib = AllocDosObject(DOS_FIB, NULL)))
                    {
                        if(Examine(lock, fib))
                        {
                            dir = fib->fib_DirEntryType;
                        }
                        FreeDosObject(DOS_FIB, fib);
                    }
                    UnLock(lock);
                }
            }
            if(dir < 0)
            {
                if(!ArgsArray[ARGS_QUIET])
                {
                    Printf("Adding class %s...", path);
                }
                if(btAddClass(path, 0))
                {
                    if(!ArgsArray[ARGS_QUIET])
                    {
                        PutStr("okay!\n");
                    }
                    btClassScan();
                } else {
                    if(!ArgsArray[ARGS_QUIET])
                    {
                        PutStr("failed!\n");
                    }
                    errmsg = "Class not added.\n";
                }
            }
            else if((exall = AllocDosObject(DOS_EXALLCONTROL, NULL)))
            {
                lock = Lock(path, ACCESS_READ);
                if(lock)
                {
                    exall->eac_LastKey = 0;
                    exall->eac_MatchString = NULL;
                    exall->eac_MatchFunc = NULL;
                    do
                    {
                        exready = ExAll(lock, (struct ExAllData *) buf, 1024, ED_NAME, exall);
                        exdata = (struct ExAllData *) buf;
                        ents = exall->eac_Entries;
                        while(ents--)
                        {
                            isvalid = TRUE;
                            namelen = strlen(path);
                            btSafeRawDoFmt(sbuf, CLASSNAMEMAX,
                                           (namelen && (path[namelen-1] == ':' || path[namelen-1] == '/')) ? "%s%s" : "%s/%s",
                                           path, exdata->ed_Name);
                            namelen = strlen(sbuf);
                            if (((namelen > 4) && (!strcmp(&sbuf[namelen-4], ".dbg"))) || ((namelen > 5) && (!strcmp(&sbuf[namelen-5], ".info"))))
                                isvalid = FALSE;
                            if (isvalid)
                            {
                                if(namelen > 4)
                                {
                                    if(!strcmp(&sbuf[namelen-4], ".elf"))
                                    {
                                        sbuf[namelen-4] = 0;
                                    }
                                }
                                btGetAttrs(BGA_STACK, NULL, BSA_ClassList, &bclist, TAG_END);
                                btLockReadBase();
                                bc = bclist->lh_Head;
                                while(bc->ln_Succ)
                                {
                                    if(!strncmp(bc->ln_Name, exdata->ed_Name, strlen(bc->ln_Name)))
                                    {
                                        break;
                                    }
                                    bc = bc->ln_Succ;
                                }
                                if(bc->ln_Succ)
                                {
                                    btUnlockBase();
                                    if(!ArgsArray[ARGS_QUIET])
                                    {
                                        Printf("Skipping class %s...\n", exdata->ed_Name);
                                    }
                                    exdata = exdata->ed_Next;
                                    continue;
                                }
                                btUnlockBase();

                                if(!ArgsArray[ARGS_QUIET])
                                {
                                    Printf("Adding class %s...", exdata->ed_Name);
                                }
                                if(btAddClass(sbuf, 0))
                                {
                                    if(!ArgsArray[ARGS_QUIET])
                                    {
                                        PutStr("okay!\n");
                                    }
                                } else {
                                    if(!ArgsArray[ARGS_QUIET])
                                    {
                                        PutStr("failed!\n");
                                    }
                                }
                            }
                            exdata = exdata->ed_Next;
                        }
                    } while(exready);
                    UnLock(lock);
                    btClassScan();
                } else {
                    errmsg = "Failed to lock the class directory.\n";
                }
                FreeDosObject(DOS_EXALLCONTROL, exall);
            }
        }
        CloseLibrary(BluetoothBase);
    } else {
        errmsg = "Unable to open bluetooth.library\n";
    }
    fail(errmsg);
    return(0); // never gets here, just to shut the compiler up
}
