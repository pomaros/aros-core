/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * This file is distributed under the AROS Public License 1.1.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <aros/debug.h>
#include "__at.h"

/* Share directory/flag/error handling while filling the actual wide layout. */
#define fstatat fstatat64
#define stat stat64
#define lstat lstat64
#include "fstatat.c"
