/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Licensed under the AROS Public License (APL) Version 1.1. */
/*
    Copyright (C) 2002, The AROS Development Team. 
    All rights reserved.
    
*/

#ifndef _MUI_MENU_H
#define _MUI_MENU_H

struct NewMenu;
struct Window;
struct ZMenu;

struct MUI_RenderInfo;
struct ZMenu *zune_open_menu(struct MUI_RenderInfo *mri, struct NewMenu *newmenu);
void zune_mouse_update(struct ZMenu *menu, int left_down);
/* returns the user data of the selected entry */
struct MenuItem *zune_leave_menu(struct ZMenu *zmenu);
void zune_close_menu(struct ZMenu *menu);
struct Menu *zune_get_menu_pointer(struct ZMenu *menu);

#endif
