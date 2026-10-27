/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#ifndef POSIXC_CWD_CONTEXT_H
#define POSIXC_CWD_CONTEXT_H
#include <dos/dos.h>
#include <exec/types.h>

struct __cwd_directory;
struct __cwd_context;
struct __cwd_snapshot {
    struct __cwd_directory *cwd;
    struct __cwd_directory *progdir;
    BOOL unix_paths;
};

/* Capture duplicates the caller-owned DOS lock. No Process or library-base
 * pointer is retained. Each directory object owns exactly one native lock. */
struct __cwd_directory *__cwd_directory_capture(BPTR lock, LONG *error);
BOOL __cwd_directory_retain(struct __cwd_directory *directory);
void __cwd_directory_release(struct __cwd_directory *directory);
BPTR __cwd_directory_lock(const struct __cwd_directory *directory);

/* Construction captures both locks. A missing progdir inherits cwd. */
struct __cwd_context *__cwd_context_create(BPTR cwd, BPTR progdir,
    BOOL unix_paths, LONG *error);
BOOL __cwd_context_retain(struct __cwd_context *context);
void __cwd_context_release(struct __cwd_context *context);
BOOL __cwd_context_snapshot(struct __cwd_context *context,
    struct __cwd_snapshot *snapshot);
void __cwd_snapshot_release(struct __cwd_snapshot *snapshot);

/* Set takes a reference of its own; the caller keeps its input reference.
 * Outstanding snapshots retain the old directory until their operations end. */
BOOL __cwd_context_set(struct __cwd_context *context,
    struct __cwd_directory *directory);
void __cwd_context_conversion(struct __cwd_context *context, BOOL unix_paths);
#endif
