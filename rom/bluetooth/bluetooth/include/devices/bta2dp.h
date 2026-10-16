/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1
    Desc: bta2dp.device - the PCM side of bta2dp.class. One unit (0).
          OpenDevice("bta2dp.device", 0, ...) with a struct IOStdReq.
          CMD_WRITE takes interleaved signed 16-bit stereo frames at the
          stream's rate; a request completes once all of it is queued for
          encoding, so its completion paces the writer. A write to a stream
          the headset suspended waits while the class asks it to START again
          (at most 3 s). Without a configured stream a write fails with
          BTA2DPERR_NOSTREAM.
*/
#ifndef DEVICES_BTA2DP_H
#define DEVICES_BTA2DP_H

#include <exec/io.h>

/* io_Actual: the stream's sampling rate in Hz (0 without a configured stream);
   io_Offset: the number of channels; io_Length: PCM frames queued. */
#define BTA2DPCMD_QUERY (CMD_NONSTD + 0)
/* io_Offset: the output volume in percent (0..100). Kept in
   ENV:Sys/AudioVolume; applied as the headset's AVRCP absolute volume when
   it offers one, else as PCM gain. */
#define BTA2DPCMD_SETVOLUME (CMD_NONSTD + 1)
/* io_Offset: the volume in percent, as set here or on the headset;
   io_Actual: 1 while the headset controls it (AVRCP absolute volume). */
#define BTA2DPCMD_GETVOLUME (CMD_NONSTD + 2)

#define BTA2DPERR_NOSTREAM 1

#endif
