/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * SPDX-License-Identifier: APL-1.1
 */
#ifndef DOS_PIPE_H
#define DOS_PIPE_H

/* PIPE: handler extensions. Send to fh_Type with fh_Arg1 in Arg1.
 * SET_NONBLOCK: Arg2 != 0 enables non-blocking reads on this handle.
 * Empty reads fail with ERROR_PIPE_WOULD_BLOCK while a writer is open;
 * EOF and zero-length reads still return zero. Writes remain blocking.
 */
#define ACTION_PIPE_SET_NONBLOCK 0x50534e42L /* 'PSNB' */
#define ERROR_PIPE_WOULD_BLOCK   0x50574f42L /* 'PWOB' */

/* READ_NOTIFY: Arg2 = Exec signal mask; Arg3 = struct Task * recipient.
 * Register only on a read-capable handle. One registration per pipe.
 * Signals report buffered data or EOF; registration also signals immediately
 * if already readable. Signals coalesce and are hints: use a non-blocking read
 * after a wakeup, and register again to recheck the level without a lost wakeup.
 * Arg2 == 0 unregisters (Arg3 is ignored). Unregister synchronously before
 * freeing the signal or exiting the recipient task. Closing the read handle
 * also unregisters. The recipient is borrowed, never retained by Exec.
 */
#define ACTION_PIPE_READ_NOTIFY  0x50524e31L /* 'PRN1' */

#endif /* DOS_PIPE_H */
