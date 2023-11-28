#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct waveform_32bit_stereo
{
	INT32 Left;
	INT32 Right;
} WAVE_32BS;

UINT8 InitChips(UINT8 Chips, UINT32 Sample_Rate/*, UINT32 MaxSmpPFrame*/);
void DeinitChips(void);
void ResetChips(void);
void FillBuffer32(WAVE_32BS* Buffer, UINT32 BufferSize);

#ifdef __cplusplus
}
#endif
