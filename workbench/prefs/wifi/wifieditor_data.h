#ifndef WIFIEDITOR_DATA_H
#define WIFIEDITOR_DATA_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#include <exec/types.h>
#include <libraries/mui.h>
#include <utility/hooks.h>

/*** Instance data **********************************************************/
struct WiFiEditor_DATA
{
    Object *list, *scanbtn, *connectbtn, *statustext, *detailsbtn, *disconnectbtn, *devcycle;
    Object *keywindow, *keystring, *keytext, *keyok, *keycancel;
    Object *detailswindow, *detailsok;
    Object *dv_network, *dv_security, *dv_ap, *dv_iface, *dv_mac;
    Object *dv_address, *dv_netmask, *dv_broadcast, *dv_gateway, *dv_dns;
    struct Hook display_hook;
    struct MUI_InputHandlerNode handler;
    BOOL handler_added, windows_added;
};

#endif
