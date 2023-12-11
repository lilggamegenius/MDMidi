// MegaDrive MIDI Player
// ---------------------
// Loader
#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <tchar.h>
#include <windows.h>

#include "SoundEngine/Structs.h"

#include "2612_reader.h"
#include "SoundEngine/Loader.h"

static const UINT8 SIG_GYB[2] = {26, 12};
static const char *SIG_MAP    = "MAPPINGS";
static const char *SIG_DAC    = "LST_DAC";
static const char *SIG_ENV    = "LST_ENV";

static UINT32 CalcGYBChecksum(UINT32 FileSize, const UINT8 *FileData);

static UINT8 LoadDACSample(
	UINT8 DACSnd,
	const char *FileName,
	UINT8 Compr,
	UINT32 Freq,
	UINT8 Rate,
	UINT8 Pan
);

static void DecompressSampleData(DAC_SAMPLE *DACSmpl, UINT8 Compr);

void InitMappingData(void) {
	for(UINT8 CurDrm = 0x00; CurDrm < 0x80; CurDrm++) {
		DRUM_SND_MAP *TempDrm = &DrumMapping.Drums[CurDrm];

		TempDrm->Type = 0x00;
		TempDrm->ID   = 0x00;
		TempDrm->Chn  = 0x00;
		TempDrm->Note = 0xFF;
	}

	GYBData.FileVer = 0x00;
	for(UINT8 CurBnk = 0x00; CurBnk < 0x02; CurBnk++) {
		for(UINT8 CurDrm                                  = 0x00; CurDrm < 0x80; CurDrm++)
			GYBData.InsMap[CurBnk].Ins[CurDrm].EntryCount = 0x00;
		GYBData.InsBank[CurBnk].InsCount = 0x00;
	}

	/*DACMapData.SndCount = 0x00;
	DACMapData.SndAlloc = 0x60;
	DACMapData.Sounds = (DAC_MAP_SOUND*)malloc(DACMapData.SndAlloc * sizeof(DAC_MAP_SOUND));*/
	PSGEnvData.EnvCount = 0x00;
	//PSGEnvData.EnvAlloc = 0x00;
	PSGEnvData.Envelope = NULL;

	DACSmplCount = 0x00;
	memset(DACSmpls, 0x00, sizeof(DACSmpls));
	memset(DACMasterPlaylist, 0x00, sizeof(DACMasterPlaylist));
	for(UINT8 CurDrm                     = 0x00; CurDrm < 0x80; CurDrm++)
		DACMasterPlaylist[CurDrm].Sample = 0xFF;
}

UINT8 LoadGYBFile(const TCHAR *FileName) {
	FILE *hFile = _tfopen(FileName, _T("rb"));
	if(hFile == NULL)
		return 0xFF;

	UINT16 TempSht;
	fread(&TempSht, 0x02, 0x01, hFile);
	if(memcmp(&TempSht, SIG_GYB, 0x02) != 0) {
		fclose(hFile);
		return 0x80; // invalid GYB file
	}

	fseek(hFile, 0x00, SEEK_END);
	UINT32 FileLen = ftell(hFile);
	fseek(hFile, 0x00, SEEK_SET);
	if(FileLen > 0x10000)
		FileLen = 0x10000; // 64 KB are really enough for 512 instruments

	UINT8 *FileData = (UINT8*) malloc(FileLen);
	if(FileData == NULL) {
		fclose(hFile);
		return 0xFE; // Memory Error
	}
	fread(FileData, 0x01, FileLen, hFile);

	fclose(hFile);

	const UINT8 RetVal = LoadGYBData_v3(FileLen, FileData, &GYBData);
	return RetVal;
}

void FreeGYBFile(void) {
	FreeGYBData_v3(&GYBData);
}

UINT8 LoadMappingFile(const TCHAR *FileName) {
	FILE *hFile = _tfopen(FileName, _T("rb"));
	if(hFile == NULL)
		return 0xFF;

	char TempStr[0x08];
	fread(TempStr, 0x01, 0x08, hFile);
	if(memcmp(TempStr, SIG_MAP, 0x08) != 0) {
		fclose(hFile);
		return 0x80; // invalid file
	}

	for(UINT8 CurDrm = 0x00; CurDrm < 0x80; CurDrm++) {
		DRUM_SND_MAP *TempDrm = &DrumMapping.Drums[CurDrm];

		TempDrm->Type = (UINT8) fgetc(hFile);
		TempDrm->ID   = (UINT8) fgetc(hFile);
		TempDrm->Chn  = (UINT8) fgetc(hFile);
		TempDrm->Note = (UINT8) fgetc(hFile);
	}

	fclose(hFile);

	return 0x00;
}

UINT8 LoadPSGEnvFile(const TCHAR *FileName) {
	FILE *hFile = _tfopen(FileName, _T("rb"));
	if(hFile == NULL)
		return 0xFF;

	char TempStr[0x07];
	fread(TempStr, 0x01, 0x07, hFile);
	if(memcmp(TempStr, SIG_ENV, 0x07) != 0) {
		fclose(hFile);
		return 0x80; // invalid file
	}

	PSGEnvData.EnvCount = (UINT8) fgetc(hFile);
	//if (PSGEnvData.EnvCount > PSGEnvData.EnvAlloc)
	//	PSGEnvData.EnvCount = PSGEnvData.EnvAlloc;
	PSGEnvData.Envelope = (PSG_ENVELOPE*) malloc(PSGEnvData.EnvCount * sizeof(PSG_ENVELOPE));
	for(UINT8 CurEnv = 0x00; CurEnv < PSGEnvData.EnvCount; CurEnv++) {
		PSG_ENVELOPE *TempEnv = &PSGEnvData.Envelope[CurEnv];
		const UINT8 TempByt   = fgetc(hFile);
		if(TempByt) {
			TempEnv->Name = (char*) malloc(TempByt + 1);
			fread(TempEnv->Name, 0x01, TempByt, hFile);
			TempEnv->Name[TempByt] = '\0';
		} else {
			TempEnv->Name = NULL;
		}

		TempEnv->DataLen = (UINT8) fgetc(hFile);
		TempEnv->Data    = (UINT8*) malloc(TempEnv->DataLen);
		fread(TempEnv->Data, 0x01, TempEnv->DataLen, hFile);
	}

	fclose(hFile);

	return 0x00;
}

void FreePSGEnvelopes(void) {
	if(PSGEnvData.Envelope == NULL)
		return;

	for(UINT8 CurEnv = 0x00; CurEnv < PSGEnvData.EnvCount; CurEnv++) {
		const PSG_ENVELOPE *TempEnv = &PSGEnvData.Envelope[CurEnv];
		if(TempEnv->Name != NULL)
			free(TempEnv->Name);
		if(TempEnv->Data != NULL);
		free(TempEnv->Data);
	}
	free(PSGEnvData.Envelope);
	PSGEnvData.Envelope = NULL;
	PSGEnvData.EnvCount = 0x00;
}

UINT8 LoadDACData(const TCHAR *FileName) {
	char BasePath[0x100];
	#ifdef _UNICODE
	WideCharToMultiByte(CP_ACP, 0, FileName, -1, BasePath, 0x100, NULL, NULL);
	#else
	strcpy(BasePath, FileName);
	#endif
	char *TempPnt = strrchr(BasePath, '\\');
	if(TempPnt == NULL)
		TempPnt = strrchr(BasePath, '/');
	if(TempPnt != NULL) {
		TempPnt++;
		*TempPnt = '\0';
	}

	FILE *hFile = _tfopen(FileName, _T("rt"));
	if(hFile == NULL)
		return 0xFF;

	UINT8 IniSection = 0xFE;
	UINT8 DACCompr;
	UINT32 DACFreq;
	UINT8 DACPan;
	UINT8 DACRate;
	UINT32 DACBaseRate;
	float DACDivider;
	char DACFile[0x100];
	while(!feof(hFile)) {
		char TempStr[0x100];
		TempPnt = fgets(TempStr, 0x100, hFile);
		if(TempPnt == NULL)
			break;
		if(TempStr[0x00] == '\n' || TempStr[0x00] == '\0')
			continue;
		if(TempStr[0x00] == ';') {
			// skip comment lines
			// fgets has set a null-terminator at char 0xFF
			while(TempStr[strlen(TempStr) - 1] != '\n') {
				fgets(TempStr, 0x100, hFile);
				if(TempStr[0x00] == '\0')
					break;
			}
			continue;
		}

		if(TempStr[0x00] == '[') {
			TempPnt = strchr(TempStr, ']');
			if(TempPnt == NULL)
				continue;
			*TempPnt = '\0';

			if(!(IniSection & 0x80))
				LoadDACSample(IniSection, DACFile, DACCompr, DACFreq, DACRate, DACPan);

			IniSection = (UINT8) strtoul(TempStr + 1, NULL, 0x10);
			if(IniSection >= 0x81 && IniSection <= 0xDF)
				IniSection &= 0x7F;
			if(!(IniSection & 0x80)) {
				strcpy(DACFile, "");
				DACCompr = 0x00;
				DACFreq  = 0x00;
				DACRate  = 0x00;
				DACPan   = 0x00;
			} else {
				IniSection = 0xFF;
			}
			continue;
		}

		if(IniSection == 0xFF) // ignore all invalid sections
			continue;

		TempPnt = strchr(TempStr, '=');
		if(TempPnt == NULL || TempPnt == TempStr)
			continue; // invalid line

		size_t TempInt = strlen(TempPnt) - 1;
		if(TempPnt[TempInt] == '\n')
			TempPnt[TempInt] = '\0';

		*TempPnt = '\0';
		TempPnt++;
		while(*TempPnt == ' ')
			TempPnt++;

		TempInt = strlen(TempStr) - 1;
		while(TempInt > 0 && TempStr[TempInt] == ' ') {
			TempStr[TempInt] = '\0';
			TempInt--;
		}

		if(!(IniSection & 0x80)) {
			if(!_stricmp(TempStr, "File")) {
				strcpy(DACFile, BasePath);
				strcat(DACFile, TempPnt);
			} else if(!_stricmp(TempStr, "Compr")) {
				if(!_stricmp(TempPnt, "None"))
					DACCompr = DACCOMPR_NONE;
				else if(!_stricmp(TempPnt, "DPCM"))
					DACCompr = DACCOMPR_DPCM;
				else
					DACCompr = strtol(TempPnt, NULL, 0) ? 0x01 : 0x00;
			} else if(!_stricmp(TempStr, "Freq")) {
				DACFreq = (UINT32) strtoul(TempPnt, NULL, 0);
			} else if(!_stricmp(TempStr, "Rate")) {
				DACRate = (UINT8) strtoul(TempPnt, NULL, 0); // this should only be an 8-bit value
				DACFreq = (UINT32) (DACBaseRate / (DACDivider + DACRate) + 0.5);
			} else if(!_stricmp(TempStr, "Pan")) {
				DACPan = (UINT8) strtol(TempPnt, NULL, 0);
			}
		} else if(IniSection == 0xFE) {
			if(!_stricmp(TempStr, "BaseRate")) {
				DACBaseRate = (UINT32) strtoul(TempPnt, NULL, 0);
			} else if(!_stricmp(TempStr, "RateDiv")) {
				DACDivider = (float) strtod(TempPnt, NULL);
			}
		}
	}
	if(!(IniSection & 0x80))
		LoadDACSample(IniSection, DACFile, DACCompr, DACFreq, DACRate, DACPan);
	DAC_BaseRate = DACBaseRate;
	DAC_RateDiv  = DACDivider;

	fclose(hFile);

	return 0x00;
}

static UINT8 LoadDACSample(
	UINT8 DACSnd,
	const char *FileName,
	UINT8 Compr,
	UINT32 Freq,
	UINT8 Rate,
	UINT8 Pan
) {
	if(*FileName == '\0')
		return 0x01;
	if(DACSnd & 0x80)
		return 0x01;

	DAC_TABLE *TempTbl = &DACMasterPlaylist[DACSnd];
	TempTbl->Freq      = Freq;
	TempTbl->Rate      = Rate;
	TempTbl->Pan       = Pan;

	UINT8 CurSmpl;
	DAC_SAMPLE *TempSmpl;
	if(TempTbl->Sample != 0xFF) {
		TempSmpl = &DACSmpls[TempTbl->Sample];
	} else {
		for(CurSmpl = 0x00; CurSmpl < DACSmplCount; CurSmpl++) {
			if(!_stricmp(FileName, DACSmpls[CurSmpl].File))
				break;
		}
		TempSmpl = &DACSmpls[CurSmpl];
		if(CurSmpl >= DACSmplCount)
			DACSmplCount++;

		TempTbl->Sample = CurSmpl;
	}

	if(TempSmpl->File != NULL && !_stricmp(FileName, TempSmpl->File))
		return 0x00; // already loaded

	if(TempSmpl->File != NULL)
		free(TempSmpl->File);
	size_t TempInt = strlen(FileName) + 1;
	TempSmpl->File = (char*) malloc(TempInt * sizeof(char));
	strcpy(TempSmpl->File, FileName);

	FILE *hFile = fopen(TempSmpl->File, "rb");
	if(hFile == NULL) {
		printf("Error opening %s\n", TempSmpl->File);

		free(TempSmpl->File);
		TempSmpl->File  = NULL;
		TempTbl->Sample = 0xFF;

		if(CurSmpl == DACSmplCount - 1)
			DACSmplCount--;
		return 0xFF;
	}

	fseek(hFile, 0x00, SEEK_END);
	TempInt = ftell(hFile);
	if(TempInt > 0x100000)
		TempInt = 0x100000; // limit to 1 MB
	TempSmpl->Size = (UINT16) TempInt;

	fseek(hFile, 0x00, SEEK_SET);
	TempSmpl->Data = (UINT8*) malloc(TempSmpl->Size);
	fread(TempSmpl->Data, 0x01, TempSmpl->Size, hFile);

	fclose(hFile);

	DecompressSampleData(TempSmpl, Compr);

	return 0x00;
}

static const UINT8 DPCM_Tbl[] = {
	0x00,
	0x01,
	0x02,
	0x04,
	0x08,
	0x10,
	0x20,
	0x40,
	0x80,
	0xFF,
	0xFE,
	0xFC,
	0xF8,
	0xF0,
	0xE0,
	0xC0
};

static void DecompressSampleData(DAC_SAMPLE *DACSmpl, UINT8 Compr) {
	UINT32 CurSPos; // Source Pos
	UINT32 CurDPos; // Destination Pos
	UINT32 DSize;
	UINT8 *DData;
	UINT8 DPCMVal;

	if(Compr == DACCOMPR_NONE)
		return;

	const UINT32 SSize = DACSmpl->Size;
	UINT8 *SData       = DACSmpl->Data;
	switch(Compr) {
		case DACCOMPR_DPCM: DSize = SSize << 1;
			DData = (UINT8*) malloc(DSize);

			DPCMVal = 0x80;
			CurDPos = 0x00;
			for(CurSPos = 0x00; CurSPos < SSize; CurSPos++) {
				DPCMVal += DPCM_Tbl[(SData[CurSPos] & 0xF0) >> 4];
				DData[CurDPos] = DPCMVal;
				CurDPos++;

				DPCMVal += DPCM_Tbl[(SData[CurSPos] & 0x0F) >> 0];
				DData[CurDPos] = DPCMVal;
				CurDPos++;
			}
			break;
		default: return;
	}

	DACSmpl->Size = DSize;
	DACSmpl->Data = DData;
	free(SData);
}

void FreeDACData(void) {
	for(UINT8 CurSmpl = 0x00; CurSmpl < DACSmplCount; CurSmpl++) {
		DAC_SAMPLE *TempSmpl = &DACSmpls[CurSmpl];
		free(TempSmpl->File);
		TempSmpl->File = NULL;
		TempSmpl->Size = 0;
		free(TempSmpl->Data);
		TempSmpl->Data = NULL;
	}
	DACSmplCount = 0x00;
}
