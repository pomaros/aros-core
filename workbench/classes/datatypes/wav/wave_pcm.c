/*
 *	wave.datatype
 *	(c) Fredrik Wikstrom (fredrik@a500.org)
 */

#include "wave_pcm.h"

DEC_SETUPPROTO(SetupPCM) {
	struct WaveFormatEx * fmt;
	fmt = data->fmt;

	/* check bitsPerSample */
	if (fmt->bitsPerSample < 1) {
		return NOTOK;
	}
	/* owervrite, in case these are corrupt */
	fmt->blockAlign = ((fmt->bitsPerSample+7)>>3) * fmt->numChannels;
	data->blockFrames = 1;
	/* deeper than 8 bits: keep 16 of them */
	data->sampleBytes = fmt->bitsPerSample > 8 ? 2 : 1;
	return OK;
}

DECODERPROTO(DecodePCM) {
	LONG mod = (fmt->bitsPerSample+7) >> 3;
	LONG chan, frame;

	if (mod > 1 && data->sampleBytes == 2) {
		/* little-endian source: the top two bytes of each sample */
		for (frame=0;frame<numFrames;frame++) {
			for (chan=0;chan<fmt->numChannels;chan++) {
				WORD *d = (WORD *)Dst[chan];
				*d++ = (WORD)(((UWORD)Src[mod-1] << 8) | Src[mod-2]);
				Dst[chan] = (BYTE *)d;
				Src += mod;
			}
		}
		return numFrames;
	}
	Src += (mod-1); /* set to MSB */
	if (mod == 1) {
		for (frame=0;frame<numFrames;frame++) {
			for (chan=0;chan<fmt->numChannels;chan++) {
				*Dst[chan]++ = (*Src++)+128; /* convert unsigned -> signed */
			}
		}
	} else {
		for (frame=0;frame<numFrames;frame++) {
			for (chan=0;chan<fmt->numChannels;chan++) {
				*Dst[chan]++ = *Src; Src += mod;
			}
		}
	}

	return numFrames;
}
