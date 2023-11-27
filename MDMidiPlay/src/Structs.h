#ifndef __STRUCTS_H__
#define __STRUCTS_H__

#include "stdtype.h"
#include "2612_structs.h"

/*typedef struct
{
	UINT8 ID;
	char* Name;
} DAC_MAP_SOUND;
typedef struct
{
	UINT8 SndCount;
	UINT8 SndAlloc;
	DAC_MAP_SOUND* Sounds;
} DAC_MAP_FILE;*/

#define DACCOMPR_NONE	0x00
#define DACCOMPR_DPCM	0x01
typedef struct
{
	char* File;
	UINT32 Size;
	UINT8* Data;
	//UINT8 UsageID;
} DAC_SAMPLE;
typedef struct
{
	UINT8 Sample;
	UINT8 Pan;
	UINT8 Rate;
	UINT32 Freq;
} DAC_TABLE;

typedef struct
{
	char* Name;
	UINT8 DataLen;
	UINT8* Data;
} PSG_ENVELOPE;
typedef struct
{
	UINT8 EnvCount;
	//UINT8 EnvAlloc;
	PSG_ENVELOPE* Envelope;
} PSG_ENV_FILE;

typedef struct
{
	UINT8 Type;	// 0 - none, 1 - DAC, 2 - FM, 3 - PSG
	UINT8 ID;
	UINT8 Chn;	// for FM only
	UINT8 Note;
} DRUM_SND_MAP;
typedef struct
{
	DRUM_SND_MAP Drums[0x80];
} DRUM_MAPPING;


extern GYB_FILE_V3 GYBData;
//extern DAC_MAP_FILE DACMapData;
extern PSG_ENV_FILE PSGEnvData;
extern DRUM_MAPPING DrumMapping;
extern UINT8 DACSmplCount;
extern DAC_SAMPLE DACSmpls[0x80];			// 0x80 different samples are maximum
extern DAC_TABLE DACMasterPlaylist[0x80];
extern UINT32 DAC_BaseRate;
extern float DAC_RateDiv;

#endif	// __STRUCTS_H__
