#ifndef REGISTERTAB_PRIVATE_H
#define REGISTERTAB_PRIVATE_H
/*
    Copyright (C) 2003-2026, The AROS Development Team. All rights reserved.
*/
#include <exec/types.h>
#include <intuition/classusr.h>

struct LocaleRegister_DATA
{
    int i;

    Object *child;
    Object *language;
    Object *region;
    Object *timezone;

    const char *LocaleRegisterLabels[3];
    char *tab_label;

    BOOL save;
};

#endif /* REGISTERTAB_PRIVATE_H */
