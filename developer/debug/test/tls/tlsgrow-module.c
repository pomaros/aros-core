/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: TLSGrow's dlopen module: one TLS variable of its own
*/

_Thread_local int module_value = 5;

int *module_tls(void)
{
    return &module_value;
}
