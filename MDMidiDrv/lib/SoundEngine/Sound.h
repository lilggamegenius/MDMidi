#pragma once

#include "MDMidDrv.h"

typedef struct waveform_32bit_stereo{
	INT32 Left;
	INT32 Right;
} WAVE_32BS;

MDMidiDrv_CEXPORT UINT8 InitChips(UINT8 Chips, UINT32 Sample_Rate/*, UINT32 MaxSmpPFrame*/);

MDMidiDrv_CEXPORT void DeinitChips(void);

MDMidiDrv_CEXPORT void ResetChips(void);

MDMidiDrv_CEXPORT void FillBuffer32(WAVE_32BS* Buffer, UINT32 BufferSize);

CLINKAGE void PlayDACSample(UINT8 ChipID, UINT8 Sound);
CLINKAGE void OverrideDACRate(UINT8 ChipID, UINT8 Rate);
CLINKAGE void SetDACVol(UINT8 ChipID, UINT8 Volume);
CLINKAGE void Sound_WakeUp();
