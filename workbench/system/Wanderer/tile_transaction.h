/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#ifndef TILE_TRANSACTION_H
#define TILE_TRANSACTION_H
#define TILE_TXN_PATH 512
#define TILE_TXN_STEPS 16
#define TILE_TXN_CREATE 1
#define TILE_TXN_EXTRACT 2
#define TILE_TXN_POSITION 3
struct TileDropRequest {
    unsigned operation;
    char source[TILE_TXN_PATH],target[TILE_TXN_PATH];
    long x,y;
};
struct TileRenamePlan {
    unsigned count,completed,rollbackFailed;
    struct {char from[TILE_TXN_PATH],to[TILE_TXN_PATH];} steps[TILE_TXN_STEPS];
};
/* Rename callback MUST refuse existing destinations, including races. Failed
 * undo retains the journal and files for recovery; never delete user data. */
int TileRenameAdd(struct TileRenamePlan *,const char *,const char *);
int TileRenameExecute(struct TileRenamePlan *,int (*rename)(void *,const char *,const char *),void *);
/* Takes a copy; no caller memory, MUI object, owner port or bitmap is borrowed.
 * One filesystem worker at a time; up to40 pending positions are copied and
 * repeated queued paths coalesce. Structural operations reject while busy.
 * Full/invalid returns0, accepted returns1; GUI polling never waits. */
/* Append one volume-relative backdrop line, preserving bytes and avoiding duplicates.
 * Returns new length, or -1 without changing the buffer on invalid/overflow input. */
long TileBackdropAppend(char *,unsigned long,unsigned long,const char *);
/* Parent GUI poll is nonblocking; explicit quit joins the exact DOS child. */
void TileDropPoll(void);
void TileDropFinish(void);
int TileDropStart(const struct TileDropRequest *);
#endif
