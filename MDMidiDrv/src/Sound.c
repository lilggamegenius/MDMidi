// MegaDrive MIDI Player
// ---------------------
// Sound Rendering source file

#include <assert.h>
#include <stdbool.h>
#include <windows.h>

#include "SoundEngine/Sound.h"
#include "SoundEngine/Structs.h"
#include "SoundEngine/chips/mamedef.h"

#include "SoundEngine/chips/2612intf.h"
#include "SoundEngine/chips/sn764intf.h"

typedef struct chip_audio_attributes {
	UINT32 SmpRate;
	UINT16 Volume;
	UINT8 ChipType;
	UINT8 Resampler; // Resampler Type: 00 - Old, 01 - Upsampling, 02 - Copy, 03 - Downsampling
	UINT32 SmpP;     // Current Sample (Playback Rate)
	UINT32 SmpLast;  // Sample Number Last
	UINT32 SmpNext;  // Sample Number Next
	WAVE_32BS LSmpl; // Last Sample
	WAVE_32BS NSmpl; // Next Sample
} CAUD_ATTR;

typedef struct {
	const DAC_SAMPLE *Sample;
	UINT32 Delta;
	UINT32 SmplPos;
	UINT32 SmplFric; // .16 Friction
	UINT8 RateOverrd;
	UINT8 Volume;
	UINT8 VolIdx;
	UINT8 VolShift;
} DAC_STATE;

//INLINE INT16 Limit2Short(INT32 Value);
static void GetChipStream(UINT8 ChipID, UINT8 ChipNum, INT32 **Buffer, UINT32 BufSize);

static void ResampleChipStream(UINT8 ChipID, WAVE_32BS *RetSample, UINT32 Length);

static void UpdateDAC(UINT8 ChipID, UINT32 Samples);

#define YM2612_CLOCK	7670454
#define SN76496_CLOCK	3579545
#define MAX_CHIPS		0x10

UINT32 SampleRate; // Note: also used by some sound cores to determinate the chip sample rate

UINT8 ResampleMode; // 00 - HQ both, 01 - LQ downsampling, 02 - LQ both
UINT8 CHIP_SAMPLING_MODE;
INT32 CHIP_SAMPLE_RATE;

CAUD_ATTR ChipAudio[MAX_CHIPS];

#define SMPL_BUFSIZE	0x80
//UINT32 SMPL_BUFSIZE;
INT32 *StreamBufs[0x02];

UINT8 OPN_CHIPS; // also indicates, if DLL is running
UINT8 PSG_CHIPS;
DAC_STATE *DACState;

UINT32 NullSamples;

UINT8 DACSmplCount;
DAC_SAMPLE DACSmpls[0x80]; // 0x80 different samples are maximum
DAC_TABLE DACMasterPlaylist[0x80];
UINT32 DAC_BaseRate;
float DAC_RateDiv;

static UINT8 DAC_RateOverride;
static const UINT16 DAC_VOL_ARRAY[0x04] = {0x100, 0xD7, 0xB5, 0x98};

UINT8 InitChips(UINT8 Chips, UINT32 Sample_Rate/*, UINT32 MaxSmpPFrame*/) {
	UINT8 CurChip;
	CAUD_ATTR *CAA;

	if(OPN_CHIPS)
		return 0x80; // already running
	if(Chips > 0x10)
		return 0xFF; // too many chips

	//SMPL_BUFSIZE = MaxSmpPFrame * 0x10;
	StreamBufs[0x00] = (INT32*) malloc(SMPL_BUFSIZE * sizeof(INT32));
	StreamBufs[0x01] = (INT32*) malloc(SMPL_BUFSIZE * sizeof(INT32));

	SampleRate         = Sample_Rate;
	ResampleMode       = 0x02;
	CHIP_SAMPLING_MODE = 0x02;
	CHIP_SAMPLE_RATE   = SampleRate;
	for(CurChip = 0x00; CurChip < MAX_CHIPS; CurChip++) {
		CAA           = &ChipAudio[CurChip];
		CAA->ChipType = 0xFF;
		CAA->SmpRate  = 0x00;
		CAA->Volume   = 0x00;
	}

	OPN_CHIPS = Chips;
	DACState  = (DAC_STATE*) malloc(OPN_CHIPS * sizeof(DAC_STATE));
	for(CurChip = 0x00; CurChip < OPN_CHIPS; CurChip++) {
		CAA           = &ChipAudio[CurChip];
		CAA->ChipType = 0x01;
		CAA->SmpRate  = device_start_ym2612(CurChip, YM2612_CLOCK);
		CAA->Volume   = 0xC0;
		device_reset_ym2612(CurChip);

		memset(&DACState[CurChip], 0x00, sizeof(DAC_STATE));
		// Necessary are at least: Sample and SmplFric
	}
	PSG_CHIPS = Chips;
	for(CurChip = 0x00; CurChip < PSG_CHIPS; CurChip++) {
		CAA           = &ChipAudio[OPN_CHIPS + CurChip];
		CAA->ChipType = 0x00;
		CAA->SmpRate  = device_start_sn764xx(CurChip, SN76496_CLOCK, 0x10, 0x09, 0x01, 0x01, 0x00, 0x00);
		CAA->Volume   = 0x60;
		device_reset_sn764xx(CurChip);
	}

	for(CurChip = 0x00; CurChip < OPN_CHIPS + PSG_CHIPS; CurChip++) {
		CAA = &ChipAudio[CurChip];
		if(!CAA->SmpRate)
			CAA->Resampler = 0xFF;
		else if(CAA->SmpRate < SampleRate)
			CAA->Resampler = 0x01;
		else if(CAA->SmpRate == SampleRate)
			CAA->Resampler = 0x02;
		else if(CAA->SmpRate > SampleRate)
			CAA->Resampler = 0x03;
		if((ResampleMode == 0x01 && CAA->Resampler == 0x03) || ResampleMode == 0x02)
			CAA->Resampler = 0x00;

		CAA->SmpP        = 0x00;
		CAA->SmpLast     = 0x00;
		CAA->SmpNext     = 0x00;
		CAA->LSmpl.Left  = 0x00;
		CAA->LSmpl.Right = 0x00;
		if(CAA->Resampler == 0x01) {
			// Pregenerate first Sample (the upsampler is always one too late)
			GetChipStream(0x00, CurChip, StreamBufs, 1);
			CAA->NSmpl.Left  = StreamBufs[0x00][0x00];
			CAA->NSmpl.Right = StreamBufs[0x01][0x00];
		} else {
			CAA->NSmpl.Left  = 0x00;
			CAA->NSmpl.Right = 0x00;
		}
	}

	NullSamples      = 0xFFFFFFFF;
	DAC_RateOverride = 0x00;

	return 0x00;
}

void DeinitChips(void) {
	UINT8 CurChip;

	free(StreamBufs[0x00]);
	StreamBufs[0x00] = NULL;
	free(StreamBufs[0x01]);
	StreamBufs[0x01] = NULL;

	for(CurChip = 0x00; CurChip < OPN_CHIPS; CurChip++)
		device_stop_ym2612(CurChip);
	for(CurChip = 0x00; CurChip < PSG_CHIPS; CurChip++)
		device_stop_sn764xx(CurChip);
	OPN_CHIPS = PSG_CHIPS = 0x00;
}

void ResetChips(void) {
	UINT8 CurChip;

	for(CurChip = 0x00; CurChip < OPN_CHIPS; CurChip++)
		device_reset_ym2612(CurChip);
	for(CurChip = 0x00; CurChip < PSG_CHIPS; CurChip++)
		device_reset_sn764xx(CurChip);
}

/*INLINE INT16 Limit2Short(INT32 Value)
{
	INT32 NewValue;

	NewValue = Value;
	if (NewValue < -0x8000)
		NewValue = -0x8000;
	if (NewValue > 0x7FFF)
		NewValue = 0x7FFF;

	return (INT16)NewValue;
}*/

INLINE void GetChipStream(UINT8 ChipID, UINT8 ChipNum, INT32 **Buffer, UINT32 BufSize) {
	if(BufSize > SMPL_BUFSIZE) {
		assert(false);
		BufSize = SMPL_BUFSIZE;
	}

	switch(ChipID) {
		case 0x00: sn764xx_stream_update(ChipNum, Buffer, BufSize);
			break;
		case 0x01: ym2612_stream_update(ChipNum, Buffer, BufSize);
			break;
		default: memset(Buffer[0], 0x00, BufSize * sizeof(INT32));
			memset(Buffer[1], 0x00, BufSize * sizeof(INT32));
			break;
	}
}

// I recommend 11 bits as it's fast and accurate
#define FIXPNT_BITS		11
#define FIXPNT_FACT		(1 << FIXPNT_BITS)
#if (FIXPNT_BITS <= 11)
typedef UINT32 SLINT; // 32-bit is a lot faster
#else
	typedef UINT64	SLINT;
#endif
#define FIXPNT_MASK		(FIXPNT_FACT - 1)

#define getfriction(x)	((x) & FIXPNT_MASK)
#define getnfriction(x)	((FIXPNT_FACT - (x)) & FIXPNT_MASK)
#define fpi_floor(x)	((x) & ~FIXPNT_MASK)
#define fpi_ceil(x)		((x + FIXPNT_MASK) & ~FIXPNT_MASK)
#define fp2i_floor(x)	((x) / FIXPNT_FACT)
#define fp2i_ceil(x)	((x + FIXPNT_MASK) / FIXPNT_FACT)

static void ResampleChipStream(UINT8 ChipID, WAVE_32BS *RetSample, UINT32 Length) {
	INT32 *CurBufL;
	INT32 *CurBufR;
	INT32 *StreamPnt[0x02];
	UINT32 InBase;
	UINT32 InPos;
	UINT32 InPosNext;
	UINT32 OutPos;
	UINT32 SmpFrc; // Sample Friction
	UINT32 InPre;
	UINT32 InNow;
	SLINT InPosL;
	INT64 TempSmpL;
	INT64 TempSmpR;
	INT32 SmpCnt; // must be signed, else I'm getting calculation errors
	UINT64 ChipSmpRate;

	CAUD_ATTR *CAA      = &ChipAudio[ChipID];
	const UINT8 ChipIDP = CAA->ChipType; // ChipID with Paired flag
	const UINT8 ChipNum = ChipID % OPN_CHIPS;
	CurBufL             = StreamBufs[0x00];
	CurBufR             = StreamBufs[0x01];

	// This Do-While-Loop gets and resamples the chip output of one or more chips.
	// It's a loop to support the AY8910 paired with the YM2203/YM2608/YM2610.
	//do
	//{
	switch(CAA->Resampler) {
		case 0x00: // old, but very fast resampler
			CAA->SmpLast = CAA->SmpNext;
			CAA->SmpP += Length;
			CAA->SmpNext = (UINT32) ((UINT64) CAA->SmpP * CAA->SmpRate / SampleRate);
			if(CAA->SmpLast >= CAA->SmpNext) {
				RetSample->Left += CAA->LSmpl.Left * CAA->Volume;
				RetSample->Right += CAA->LSmpl.Right * CAA->Volume;
			} else {
				SmpCnt = CAA->SmpNext - CAA->SmpLast;

				GetChipStream(ChipIDP, ChipNum, StreamBufs, SmpCnt);

				if(SmpCnt == 1) {
					RetSample->Left += CurBufL[0x00] * CAA->Volume;
					RetSample->Right += CurBufR[0x00] * CAA->Volume;
					CAA->LSmpl.Left  = CurBufL[0x00];
					CAA->LSmpl.Right = CurBufR[0x00];
				} else if(SmpCnt == 2) {
					RetSample->Left += (CurBufL[0x00] + CurBufL[0x01]) * CAA->Volume >> 1;
					RetSample->Right += (CurBufR[0x00] + CurBufR[0x01]) * CAA->Volume >> 1;
					CAA->LSmpl.Left  = CurBufL[0x01];
					CAA->LSmpl.Right = CurBufR[0x01];
				} else {
					// I'm using InPos
					INT32 TempS32L = CurBufL[0x00];
					INT32 TempS32R = CurBufR[0x00];
					for(INT32 CurSmpl = 0x01; CurSmpl < SmpCnt; CurSmpl++) {
						TempS32L += CurBufL[CurSmpl];
						TempS32R += CurBufR[CurSmpl];
					}
					RetSample->Left += TempS32L * CAA->Volume / SmpCnt;
					RetSample->Right += TempS32R * CAA->Volume / SmpCnt;
					CAA->LSmpl.Left  = CurBufL[SmpCnt - 1];
					CAA->LSmpl.Right = CurBufR[SmpCnt - 1];
				}
			}
			break;
		case 0x01: // Upsampling
			ChipSmpRate = CAA->SmpRate;
			InPosL = (SLINT) (FIXPNT_FACT * CAA->SmpP * ChipSmpRate / SampleRate);
			InPre  = (UINT32) fp2i_floor(InPosL);
			InNow  = (UINT32) fp2i_ceil(InPosL);

			CurBufL[0x00]   = CAA->LSmpl.Left;
			CurBufR[0x00]   = CAA->LSmpl.Right;
			CurBufL[0x01]   = CAA->NSmpl.Left;
			CurBufR[0x01]   = CAA->NSmpl.Right;
			StreamPnt[0x00] = &CurBufL[0x02];
			StreamPnt[0x01] = &CurBufR[0x02];
			GetChipStream(ChipIDP, ChipNum, StreamPnt, InNow - CAA->SmpNext);

			InBase       = FIXPNT_FACT + (UINT32) (InPosL - (SLINT) CAA->SmpNext * FIXPNT_FACT);
			SmpCnt       = FIXPNT_FACT;
			CAA->SmpLast = InPre;
			CAA->SmpNext = InNow;
			for(OutPos = 0x00; OutPos < Length; OutPos++) {
				InPos = InBase + (UINT32) (FIXPNT_FACT * OutPos * ChipSmpRate / SampleRate);

				InPre  = fp2i_floor(InPos);
				InNow  = fp2i_ceil(InPos);
				SmpFrc = getfriction(InPos);

				// Linear interpolation
				TempSmpL = (INT64) CurBufL[InPre] * (FIXPNT_FACT - SmpFrc) +
						   (INT64) CurBufL[InNow] * SmpFrc;
				TempSmpR = (INT64) CurBufR[InPre] * (FIXPNT_FACT - SmpFrc) +
						   (INT64) CurBufR[InNow] * SmpFrc;
				RetSample[OutPos].Left += (INT32) (TempSmpL * CAA->Volume / SmpCnt);
				RetSample[OutPos].Right += (INT32) (TempSmpR * CAA->Volume / SmpCnt);
			}
			CAA->LSmpl.Left  = CurBufL[InPre];
			CAA->LSmpl.Right = CurBufR[InPre];
			CAA->NSmpl.Left  = CurBufL[InNow];
			CAA->NSmpl.Right = CurBufR[InNow];
			CAA->SmpP += Length;
			break;
		case 0x02: // Copying
			CAA->SmpNext = CAA->SmpP * CAA->SmpRate / SampleRate;
			GetChipStream(ChipIDP, ChipNum, StreamBufs, Length);

			for(OutPos = 0x00; OutPos < Length; OutPos++) {
				RetSample[OutPos].Left += CurBufL[OutPos] * CAA->Volume;
				RetSample[OutPos].Right += CurBufR[OutPos] * CAA->Volume;
			}
			CAA->SmpP += Length;
			CAA->SmpLast = CAA->SmpNext;
			break;
		case 0x03: // Downsampling
			ChipSmpRate = CAA->SmpRate;
			InPosL       = (SLINT) (FIXPNT_FACT * (CAA->SmpP + Length) * ChipSmpRate / SampleRate);
			CAA->SmpNext = (UINT32) fp2i_ceil(InPosL);

			CurBufL[0x00]   = CAA->LSmpl.Left;
			CurBufR[0x00]   = CAA->LSmpl.Right;
			StreamPnt[0x00] = &CurBufL[0x01];
			StreamPnt[0x01] = &CurBufR[0x01];
			GetChipStream(ChipIDP, ChipNum, StreamPnt, CAA->SmpNext - CAA->SmpLast);

			InPosL = (SLINT) (FIXPNT_FACT * CAA->SmpP * ChipSmpRate / SampleRate);
		// I'm adding 1.0 to avoid negative indexes
			InBase    = FIXPNT_FACT + (UINT32) (InPosL - (SLINT) CAA->SmpLast * FIXPNT_FACT);
			InPosNext = InBase;
			for(OutPos = 0x00; OutPos < Length; OutPos++) {
				//InPos = InBase + (UINT32)(FIXPNT_FACT * OutPos * ChipSmpRate / SampleRate);
				InPos     = InPosNext;
				InPosNext = InBase + (UINT32) (FIXPNT_FACT * (OutPos + 1) * ChipSmpRate / SampleRate);

				// first frictional Sample
				SmpFrc = getnfriction(InPos);
				if(SmpFrc) {
					InPre    = fp2i_floor(InPos);
					TempSmpL = (INT64) CurBufL[InPre] * SmpFrc;
					TempSmpR = (INT64) CurBufR[InPre] * SmpFrc;
				} else {
					TempSmpL = TempSmpR = 0x00;
				}
				SmpCnt = SmpFrc;

				// last frictional Sample
				SmpFrc = getfriction(InPosNext);
				InPre  = fp2i_floor(InPosNext);
				if(SmpFrc) {
					TempSmpL += (INT64) CurBufL[InPre] * SmpFrc;
					TempSmpR += (INT64) CurBufR[InPre] * SmpFrc;
					SmpCnt += SmpFrc;
				}

				// whole Samples in between
				//InPre = fp2i_floor(InPosNext);
				InNow = fp2i_ceil(InPos);
				SmpCnt += (InPre - InNow) * FIXPNT_FACT; // this is faster
				while(InNow < InPre) {
					TempSmpL += (INT64) CurBufL[InNow] * FIXPNT_FACT;
					TempSmpR += (INT64) CurBufR[InNow] * FIXPNT_FACT;
					//SmpCnt ++;
					InNow++;
				}

				RetSample[OutPos].Left += (INT32) (TempSmpL * CAA->Volume / SmpCnt);
				RetSample[OutPos].Right += (INT32) (TempSmpR * CAA->Volume / SmpCnt);
			}

			CAA->LSmpl.Left  = CurBufL[InPre];
			CAA->LSmpl.Right = CurBufR[InPre];
			CAA->SmpP += Length;
			CAA->SmpLast = CAA->SmpNext;
			break;
		default: return; // do absolutely nothing
	}

	if(CAA->SmpLast >= CAA->SmpRate) {
		CAA->SmpLast -= CAA->SmpRate;
		CAA->SmpNext -= CAA->SmpRate;
		CAA->SmpP -= SampleRate;
	}

	//	CAA = CAA->Paired;
	//	ChipIDP |= 0x80;
	//} while(CAA != NULL);
}

void UpdateEngine(UINT32 SampleCount);

void FillBuffer32(WAVE_32BS *Buffer, UINT32 BufferSize) {
	UpdateEngine(BufferSize);
	if(Buffer == NULL)
		return;

	memset(Buffer, 0x00, sizeof(WAVE_32BS) * BufferSize);
	if(NullSamples == 0xFFFFFFFF)
		return;

	for(UINT32 CurSmpl = 0x00; CurSmpl < BufferSize; CurSmpl++) {
		WAVE_32BS *TempBuf = &Buffer[CurSmpl];

		for(UINT8 CurChip = 0x00; CurChip < OPN_CHIPS; CurChip++)
			UpdateDAC(CurChip, 1);
		for(UINT8 CurChip = 0x00; CurChip < OPN_CHIPS + PSG_CHIPS; CurChip++)
			ResampleChipStream(CurChip, TempBuf, 1);

		if(!TempBuf->Left && !TempBuf->Right)
			NullSamples++;
	}

	if(NullSamples >= SampleRate)
		NullSamples = 0xFFFFFFFF;
}

static void UpdateDAC(UINT8 ChipID, UINT32 Samples) {
	DAC_STATE *TempDAC = &DACState[ChipID];
	if(TempDAC->Sample == NULL)
		return;

	//UINT32 RemDelta = TempDAC->Delta * Samples;
	TempDAC->SmplFric += TempDAC->Delta * Samples;
	if(TempDAC->SmplFric & 0xFFFF0000) {
		TempDAC->SmplPos += TempDAC->SmplFric >> 16;
		TempDAC->SmplFric &= 0x0000FFFF;
		if(TempDAC->SmplPos >= TempDAC->Sample->Size) {
			TempDAC->Sample = NULL;
			return;
		}

		ym2612_w(0x00, 0x00 | 0x00, 0x2A);
		if(!TempDAC->Volume) {
			ym2612_w(0x00, 0x00 | 0x01, TempDAC->Sample->Data[TempDAC->SmplPos]);
		} else {
			INT16 SmplData = TempDAC->Sample->Data[TempDAC->SmplPos] - 0x80;
			SmplData *= DAC_VOL_ARRAY[TempDAC->VolIdx];
			SmplData >>= TempDAC->VolShift;
			SmplData = (SmplData >> 8) + 0x80;
			ym2612_w(0x00, 0x00 | 0x01, (UINT8) SmplData);
		}
	}
}

#define MulDivRoundU(Mul1, Mul2, Div)	(UINT32)( ((UINT64)Mul1 * Mul2 + Div / 2) / Div)

void PlayDACSample(UINT8 ChipID, UINT8 Sound) {
	if(ChipID >= OPN_CHIPS || Sound >= 0x80)
		return;

	DAC_STATE *TempDAC       = &DACState[ChipID];
	const DAC_TABLE *TempTbl = &DACMasterPlaylist[Sound];
	if(TempTbl->Sample & 0x80 || TempDAC->Volume >= 0x10) {
		TempDAC->Sample = NULL;
		return;
	}
	TempDAC->Sample = &DACSmpls[TempTbl->Sample];

	UINT32 BaseFreq;
	UINT32 FreqDiv;
	if(!TempDAC->RateOverrd) {
		BaseFreq = TempTbl->Freq;
		FreqDiv  = SampleRate;
	} else {
		BaseFreq = DAC_BaseRate;
		FreqDiv  = (UINT32) (SampleRate * (DAC_RateDiv + TempDAC->RateOverrd));
	}
	TempDAC->Delta   = MulDivRoundU(0x10000, BaseFreq, FreqDiv);
	TempDAC->SmplPos = 0x00;

	if(TempTbl->Pan) {
		ym2612_w(0x00, 0x02 | 0x00, 0xB6);
		ym2612_w(0x00, 0x02 | 0x01, TempTbl->Pan);
	}
}

void OverrideDACRate(UINT8 ChipID, UINT8 Rate) {
	if(ChipID >= OPN_CHIPS)
		return;

	DAC_STATE *TempDAC  = &DACState[ChipID];
	TempDAC->RateOverrd = Rate;
	if(TempDAC->Sample == NULL || !Rate)
		return;

	const UINT32 FreqDiv = (UINT32) (SampleRate * (DAC_RateDiv + TempDAC->RateOverrd));
	TempDAC->Delta       = MulDivRoundU(0x10000, DAC_BaseRate, FreqDiv);
}

void SetDACVol(UINT8 ChipID, UINT8 Volume) {
	if(ChipID >= OPN_CHIPS)
		return;

	DAC_STATE *TempDAC = &DACState[ChipID];
	TempDAC->Volume    = Volume;
	if(TempDAC->Volume > 0x0F) {
		TempDAC->Volume = 0x10;
		Volume          = 0x0F;
	}

	TempDAC->VolIdx   = Volume & 0x03;
	TempDAC->VolShift = (Volume & 0x0C) >> 2;
}

void Sound_WakeUp(void) {
	NullSamples = 0x00;
}
