/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    This file is part of AROS, distributed under the AROS Public License 1.1.
*/
#ifndef HIDD_PREPAREDCURSOR_H
#define HIDD_PREPAREDCURSOR_H
#include <exec/types.h>
#include <oop/oop.h>
/* Explicit software cursor snapshot. Image/save-under are independently owned
 * allocations; target is borrowed. A mapped pixel lease pins the live state
 * and both old/new resources until publication or rollback has finished.
 * Preparing/rendering a proposed state never writes the live state. Commit
 * and rollback copy this structure only; release the losing resources after
 * ReleasePixels, never while their buffers remain in a pixel mapping.
 */
struct HIDD_CursorState
{
    OOP_Object *cursor_bm;
    UBYTE *cursor_backup, *cursor_argb;
    ULONG cursor_backup_bpp, cursor_backup_size;
    ULONG cursor_w, cursor_h;
    LONG cursor_mouseX, cursor_mouseY, cursor_xoffset, cursor_yoffset;
    LONG cursor_drawX, cursor_drawY, cursor_backupX, cursor_backupY;
    ULONG cursor_backupW, cursor_backupH;
    BOOL cursor_visible, cursor_drawn;
};
#endif
