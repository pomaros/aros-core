/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Distributed under the AROS Public License, version 1.1. */
#ifndef ZUNE_EDITOR_H
#define ZUNE_EDITOR_H
#include <libraries/mui.h>
#define ZUNE_PANELS 11
struct ZuneEditor_DATA
{
    Object *groups[ZUNE_PANELS];
    Object *configdata;     /* of the last import: the panels point into it */
};
#endif
