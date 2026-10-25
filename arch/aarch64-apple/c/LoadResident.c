/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: LoadResident FILE - initialise a ROM-style module from disk.

    The desktop's coldstart proof pins the exact resident list, so modules
    that are not in the boot bundle (entropy.resource, a radio resource) are
    loaded from disk the way bwfm.device already loads its resource: LoadSeg,
    find the RomTag in the segment list, InitResident. A module that is
    already present is left alone. Return code 0 on success, 10 otherwise.
*/

#include <exec/resident.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

static struct Resident *find_resident(BPTR seglist)
{
    while (seglist)
    {
        ULONG *segment = (ULONG *)BADDR(seglist);
        UBYTE *start = (UBYTE *)(segment + 1);
        ULONG size = segment[-1] - sizeof(ULONG) * 2;
        UBYTE *addr;

        for (addr = start; addr + sizeof(struct Resident) <= start + size; addr += 2)
        {
            struct Resident *res = (struct Resident *)addr;

            if (res->rt_MatchWord == RTC_MATCHWORD && res->rt_MatchTag == res)
                return res;
        }
        seglist = *(BPTR *)BADDR(seglist);
    }
    return NULL;
}

int main(void)
{
    IPTR args[1] = { 0 };
    struct RDArgs *rda = ReadArgs("FILE/A", args, NULL);
    BPTR seglist;
    struct Resident *res;
    int rc = RETURN_ERROR;

    if (rda == NULL)
    {
        PrintFault(IoErr(), "LoadResident");
        return RETURN_ERROR;
    }
    seglist = LoadSeg((CONST_STRPTR)args[0]);
    if (seglist == 0)
        PrintFault(IoErr(), (CONST_STRPTR)args[0]);
    else if ((res = find_resident(seglist)) == NULL)
    {
        Printf("%s: no RomTag\n", args[0]);
        UnLoadSeg(seglist);
    }
    else
    {
        Forbid();
        if (FindName(&SysBase->ResourceList, res->rt_Name) ||
            FindName(&SysBase->LibList, res->rt_Name) ||
            FindName(&SysBase->DeviceList, res->rt_Name))
        {
            Permit();
            Printf("%s: %s already present\n", args[0], res->rt_Name);
            UnLoadSeg(seglist);
            rc = RETURN_OK;
        }
        else
        {
            Permit();
            if (InitResident(res, seglist) != NULL ||
                FindName(&SysBase->ResourceList, res->rt_Name))
            {
                Printf("%s: %s initialised\n", args[0], res->rt_Name);
                rc = RETURN_OK;
            }
            else
            {
                Printf("%s: InitResident failed\n", args[0]);
                UnLoadSeg(seglist);
            }
        }
    }
    FreeArgs(rda);
    return rc;
}
