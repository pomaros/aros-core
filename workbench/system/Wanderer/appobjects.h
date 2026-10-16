#ifndef _APPOBJECTS_H_
#define _APPOBJECTS_H_

/*
    Copyright © 2011, The AROS Development Team. All rights reserved.
    $Id$
*/

#include <exec/types.h>
#include <workbench/workbench.h>

/*** Prototypes *************************************************************/
BOOL SendAppIconMessage(struct AppIcon * appicon, LONG numargs, STRPTR args);
BOOL SendAppIconMenuMessage(struct AppIcon * appicon, UWORD class);
APTR AppObjectsLock();
VOID AppObjectsUnlock(APTR lock);
struct AppIcon * GetNextAppIconLocked(struct AppIcon * lastappicon, APTR lock);
struct DiskObject * AppIcon_GetDiskObject(struct AppIcon * appicon);
CONST_STRPTR AppIcon_GetLabel(struct AppIcon * appicon);
BOOL AppIcon_Supports(struct AppIcon * appicon, ULONG tag); /* Takes one of the WBAPPICONA_SupportsXXX tags as parameter */
BOOL AppIcon_CallRenderHook(struct AppIcon * appicon, struct AppIconRenderMsg * msg);
VOID CleanAppIconReplyMessages();
BOOL AppIcon_TileSize(struct AppIcon *, ULONG *, ULONG *);
BOOL AppIcon_DrawTile(struct AppIcon *, struct RastPort *, LONG, LONG);
BOOL AppIcon_TileClick(struct AppIcon *, LONG, LONG);
BOOL AppIcon_TileHit(struct AppIcon *, LONG, LONG);
BOOL AppIcon_TilesChanged(void);
BOOL AppIcon_TileVisibility(struct AppIcon *, BOOL);

BOOL AppIcon_SendTileMessage(struct AppIcon *, UWORD, LONG, LONG, const char *);

BOOL AppIcon_TilePlacement(struct AppIcon *icon, struct Screen *screen, BOOL valid, LONG left, LONG top, ULONG width, ULONG height);
BOOL AppIcon_TileGeometry(struct AppIcon *icon, BOOL valid, LONG left, LONG top, ULONG width, ULONG height);
BOOL AppIcon_TilePointer(struct AppIcon *icon, BOOL inside, LONG x, LONG y);

BOOL AppIcon_CopyTileDefinition(struct AppIcon *, char *, ULONG);
BOOL AppIcon_TileDefinitionRegistered(const char *);
BOOL AppIcon_TileAnchor(struct AppIcon *, ULONG *);

#endif /* _APPOBJECTS_H_ */
