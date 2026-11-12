/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.
*/
#ifndef FAT_FILE_POSITION_H
#define FAT_FILE_POSITION_H

/* FAT stores file sizes/positions in unsigned 32 bits. Validate before adding
 * the signed packet offset, including INT64_MIN/MAX, and before narrowing. */
static LONG FatFilePosition(ULONG position, ULONG size, QUAD offset,
    LONG mode, ULONG *result)
{
    QUAD base;
    switch (mode)
    {
        case OFFSET_BEGINNING: base = 0; break;
        case OFFSET_CURRENT: base = position; break;
        case OFFSET_END: base = size; break;
        default: return ERROR_SEEK_ERROR;
    }
    if (offset < -base)
        return ERROR_SEEK_ERROR;
    if (offset > 0xffffffffLL - base)
        return ERROR_OBJECT_TOO_LARGE;
    *result = (ULONG)(base + offset);
    return 0;
}
#endif
