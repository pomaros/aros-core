#ifndef WIFIEDITOR_H
#define WIFIEDITOR_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#include <libraries/mui.h>

#define MUIC_WiFiEditor "WiFiEditor.mcc"
#define MUIB_WiFiEditor (TAG_USER | 0x10570000)
#define MUIM_WiFiEditor_Device     (MUIB_WiFiEditor | 1)
#define MUIM_WiFiEditor_Scan       (MUIB_WiFiEditor | 2)
#define MUIM_WiFiEditor_Connect    (MUIB_WiFiEditor | 3)
#define MUIM_WiFiEditor_KeyOk      (MUIB_WiFiEditor | 4)
#define MUIM_WiFiEditor_KeyCancel  (MUIB_WiFiEditor | 5)
#define MUIM_WiFiEditor_Details    (MUIB_WiFiEditor | 6)
#define MUIM_WiFiEditor_Disconnect (MUIB_WiFiEditor | 7)
#define MUIM_WiFiEditor_Reply      (MUIB_WiFiEditor | 8)

#endif
