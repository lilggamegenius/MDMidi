// Stream.h: Header File for constants and structures related to Sound Output
//

#include <mmsystem.h>

typedef struct waveform_16bit_stereo
{
	INT16 Left;
	INT16 Right;
} WAVE_16BS;

#define SAMPLESIZE		sizeof(WAVE_16BS)
#define BUFSIZE_MAX		0x1000		// Maximum Buffer Size in Bytes
#define AUDIOBUFFERS	200			// Maximum Buffer Count
//	Windows:	BUFFERSIZE = SampleRate / 100 * SAMPLESIZE (44100 / 100 * 4 = 1764)
//				1 Audio-Buffer = 10 msec, Min: 5
//				Win95- / WinVista-safe: 500 msec

UINT8 SaveFile(UINT32 FileLen, void* TempData);
UINT8 SoundLogging(UINT8 Mode);
UINT8 StartStream(UINT8 DeviceID);
UINT8 StopStream(bool SkipWOClose);
void PauseStream(bool PauseOn);
void FillBuffer(WAVE_16BS* Buffer, UINT32 BufferSize);
