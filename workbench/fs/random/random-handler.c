/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL) Version 1.1

    random-handler: the byte streams RANDOM: and ZERO:.

    RANDOM: reads return bytes from entropy.resource's CSPRNG, as many as
    asked, never blocking; what is written to it is mixed into the pool
    without being credited as entropy. ZERO: reads return zero bytes and
    writes are discarded. posixc maps /dev/urandom and /dev/random to
    RANDOM:, /dev/zero to ZERO:.

    One handler serves both: a mount whose device name is ZERO is the zero
    stream, any other the random one.
*/

#include <aros/debug.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/entropy.h>

#include <string.h>

static void reply_packet(struct DosPacket *dp, struct ExecBase *SysBase)
{
    struct MsgPort *mp = dp->dp_Port;
    struct Message *mn = dp->dp_Link;

    mn->mn_Node.ln_Name = (char *)dp;
    dp->dp_Port = &((struct Process *)FindTask(NULL))->pr_MsgPort;
    PutMsg(mp, mn);
}

static struct DosPacket *next_packet(struct MsgPort *mp, struct ExecBase *SysBase)
{
    WaitPort(mp);
    return (struct DosPacket *)(GetMsg(mp)->mn_Node.ln_Name);
}

/* Whether the mount's device name is ZERO */
static BOOL is_zero_device(struct DeviceNode *dn)
{
    CONST_STRPTR name;
    ULONG len;

    if (!dn || !dn->dn_Name)
        return FALSE;
    name = AROS_BSTR_ADDR(dn->dn_Name);
    len = AROS_BSTR_strlen(dn->dn_Name);
    return len == 4 && (name[0] | 0x20) == 'z' && (name[1] | 0x20) == 'e' &&
           (name[2] | 0x20) == 'r' && (name[3] | 0x20) == 'o';
}

LONG random_handler(struct ExecBase *SysBase)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    struct MsgPort *mp = &me->pr_MsgPort;
    struct Library *EntropyBase;
    struct DosPacket *dp;
    struct DeviceNode *dn;
    ULONG openers = 0;
    BOOL zero, running = TRUE;

    dp = next_packet(mp, SysBase);
    dn = (struct DeviceNode *)BADDR(dp->dp_Arg3);
    zero = is_zero_device(dn);
    EntropyBase = zero ? NULL : OpenResource("entropy.resource");
    if (!zero && !EntropyBase)
    {
        dp->dp_Res1 = DOSFALSE;
        dp->dp_Res2 = ERROR_OBJECT_NOT_FOUND;
        reply_packet(dp, SysBase);
        return RETURN_FAIL;
    }
    dn->dn_Task = mp;
    dp->dp_Res1 = DOSTRUE;
    dp->dp_Res2 = 0;

    do
    {
        reply_packet(dp, SysBase);
        dp = next_packet(mp, SysBase);
        dp->dp_Res2 = 0;

        switch (dp->dp_Type)
        {
        case ACTION_FINDINPUT:
        case ACTION_FINDOUTPUT:
        case ACTION_FINDUPDATE:
        {
            struct FileHandle *fh = BADDR(dp->dp_Arg1);

            /* The stream has no files: any name opens it */
            fh->fh_Arg1 = (SIPTR)mp;
            fh->fh_Type = mp;
            openers++;
            dp->dp_Res1 = DOSTRUE;
            break;
        }

        case ACTION_READ:
        {
            APTR buffer = (APTR)dp->dp_Arg2;
            LONG length = (LONG)dp->dp_Arg3;

            if (length < 0)
            {
                dp->dp_Res1 = -1;
                dp->dp_Res2 = ERROR_BAD_NUMBER;
            }
            else
            {
                if (zero)
                    memset(buffer, 0, length);
                else if (length > 0)
                    GetEntropy(buffer, length);
                dp->dp_Res1 = length;
            }
            break;
        }

        case ACTION_WRITE:
        {
            LONG length = (LONG)dp->dp_Arg3;

            if (length < 0)
            {
                dp->dp_Res1 = -1;
                dp->dp_Res2 = ERROR_BAD_NUMBER;
                break;
            }
            if (!zero && length > 0)
                AddEntropy((CONST_APTR)dp->dp_Arg2, length, 0);
            dp->dp_Res1 = length;
            break;
        }

        case ACTION_SEEK:
            /* A stream stays at its start */
            dp->dp_Res1 = 0;
            break;

        case ACTION_END:
            if (openers)
                openers--;
            dp->dp_Res1 = DOSTRUE;
            break;

        case ACTION_IS_FILESYSTEM:
            dp->dp_Res1 = DOSFALSE;
            break;

        case ACTION_DIE:
            if (openers)
            {
                dp->dp_Res1 = DOSFALSE;
                dp->dp_Res2 = ERROR_OBJECT_IN_USE;
            }
            else
            {
                dp->dp_Res1 = DOSTRUE;
                running = FALSE;
            }
            break;

        default:
            dp->dp_Res1 = DOSFALSE;
            dp->dp_Res2 = ERROR_ACTION_NOT_KNOWN;
            break;
        }
    } while (running);

    dn->dn_Task = NULL;
    reply_packet(dp, SysBase);
    return RETURN_OK;
}
