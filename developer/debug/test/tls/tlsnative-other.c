/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: TLSNative's second translation unit
*/

_Thread_local int tls_extern = 1234;

int *tls_extern_addr(void)
{
    return &tls_extern;
}
