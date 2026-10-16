/*
 *	wave.datatype by Fredrik Wikstrom
 *
 */

#ifndef WAVE_DECODERS_H
#define WAVE_DECODERS_H

#include <sys/types.h>

#define DEC_SETUPPROTO(NAME) \
LONG NAME (struct DecoderData *data)

#define DEC_CLEANUPPROTO(NAME) \
void NAME (struct DecoderData *data)

#define DECODERPROTO(NAME) \
LONG NAME (struct DecoderData *data, struct WaveFormatEx *fmt, UBYTE *Src, BYTE **Dst, LONG numBlocks, LONG numFrames)

#define DECODERPROTO_v2(NAME) \
LONG NAME (struct DecoderData *data, BPTR file, BYTE **Dst, LONG totalFrames, LONG *decFrames)

struct DecoderData {
	struct ClassBase	*libBase;
	struct RIFFChunk	chunk;
	struct WaveFormatEx	*fmt;
	LONG				blockFrames;
	LONG				sampleBytes;	// what Decode writes per sample: 1 (BYTE) or 2 (WORD); 0 means 1
	void *				state;	// decoder-specific data
	DECODERPROTO((*Decode));
	DECODERPROTO((*DecodeFrames));
};

struct Decoder {
	UWORD	formatTag;
	UWORD	pad;
	DEC_SETUPPROTO((*Setup));
	DEC_CLEANUPPROTO((*Cleanup));
	DECODERPROTO((*Decode));
	DECODERPROTO((*DecodeFrames));
};

struct Decoder * GetDecoder(UWORD fmtTag);

#endif
