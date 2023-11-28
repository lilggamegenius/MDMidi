// MegaDrive MIDI Player
// ---------------------
// MIDI Playback source file

#include <stdio.h>
#include "stdbool.h"
#include <malloc.h>
#include <windows.h>
#include <conio.h>

#include "chips/mamedef.h"
#include "Stream.h"
#include "../../MDMidiDrv/src/Sound.h"
#include "SoundEngine/Loader.h"
#include "chips/2612intf.h"


#define OPNAPI
UINT8 OPNAPI OpenOPNDriver(void);
void OPNAPI CloseOPNDriver(void);
void OPNAPI OPN_Write(UINT8 ChipID, UINT16 Register, UINT8 Data);
static bool OpenVGMFile(const char* FileName);
void InterpretFile(UINT32 SampleCount);

static void SwapBytes(void* Buffer, UINT32 Bytes);
UINT8 MIDI1to0(UINT32 SrcLen, UINT8* SrcData, UINT32* RetDstLen, UINT8** RetDstData);

typedef struct vgm_file_header
{
	UINT32 lngEOFOffset;
	UINT32 lngVersion;
	UINT32 lngTotalSamples;
	UINT32 lngLoopOffset;
	UINT32 lngLoopSamples;
	UINT32 lngDataOffset;
} VGM_HEADER;
typedef struct vgm_gd3_tag
{
	UINT32 fccGD3;
	UINT32 lngVersion;
	UINT32 lngTagLength;
	wchar_t* strTrackNameE;
	wchar_t* strGameNameE;
	wchar_t* strSystemNameE;
	wchar_t* strAuthorNameE;
	wchar_t* strReleaseDate;
	wchar_t* strCreator;
	wchar_t* strNotes;
} GD3_TAG;
typedef struct midi_file_header
{
	UINT16 shtFormat;
	UINT16 shtTracks;
	UINT16 shtResolution;	// Ticks per Quarter
} MID_HEADER;
typedef struct midi_tempo
{
	INT32 BaseTick;
	UINT32 Tempo;
	UINT32 SmplTime;
} MIDI_TEMPO;

VGM_HEADER VGMHead;
MID_HEADER MIDIHead;
UINT32 VGMDataLen;
UINT8* VGMData;
GD3_TAG VGMTag;
UINT16 MidiTrk;

UINT32 TempoCount;
MIDI_TEMPO* MidTempo;
UINT32 CurTempo;
UINT32 BaseTempo;
UINT32 TotalPbSamples;

UINT32 SampleRate;
UINT32 VGMPos;
INT32 VGMSmplPos;
INT32 VGMSmplPlayed;
UINT32 VGMSampleRate;
UINT32 PlayingTime;

bool VGMEnd;
bool PausePlay;
bool ErrMsg;
UINT8 LastMidCmd;
extern char SoundLogFile[MAX_PATH];
static WAVE_32BS TempSmplBuffer[BUFSIZE_MAX / SAMPLESIZE];

int main(int argc, char* argv[])
{
	/*OpenOPNDriver(0x01);
	OPN_Write(0, 0x2B, 0x80);
	OPN_Write(0, 0x2A, 0x00);
	Sleep(1);
	OPN_Write(0, 0x2A, 0xFF);
	Sleep(1000);
	OPN_Write(0, 0x2B, 0x00);
	sn764xx_w(0, 0, 0xE0);
	sn764xx_w(0, 0, 0xF0);
	Sleep(1000);
	CloseOPNDriver();*/
	
	char FileName[0x100];
	UINT8 RetVal;
	
	printf("MD MIDI Player\n--------------\nby Valley Bell\n");
	printf("\n");
	printf("File Name:\t");
	if (argc <= 0x01)
	{
		gets_s(FileName, sizeof FileName);
	}
	else
	{
		strcpy_s(FileName, sizeof FileName, argv[0x01]);
		printf("%s\n", FileName);
	}
	if (! strlen(FileName))
		return 0;
	
	if (! OpenVGMFile(FileName))
	{
		printf("Error opening the file!\n");
		_getch();
		return 0;
	}
	printf("\n");
	
	//ShowVGMTag();
	//PlayVGM();
	
	RetVal = InitChips(0x01, 44100);
	
	InitEngine();
	//InitMappingData() is already done by InitEngine()
	
	//if (LoadGYBFile("data/Instruments.gyb"))
	//if (LoadGYBFile("data_new/mid2vgm.gyb"))
	//if (LoadGYBFile("I:/B2E/SMPSPlay/s3_full.gyb"))
	if (LoadGYBFile("I:/B2E/SMPSPlay/mid2vgm.gyb"))
		printf("Error loading GYB!\n");
	
	if (LoadPSGEnvFile("data_new/S1+3K_PSG.lst"))
	//if (LoadPSGEnvFile("data_new/S3K_PSG.lst"))
		printf("Error loading PSG file!\n");
	
	//if (LoadMappingFile("data/Mappings.map"))
	//if (LoadMappingFile("data_new/S1neko_Map.map"))
	//if (LoadMappingFile("data_new/S1_FMDrums.map"))
	//if (LoadMappingFile("data_new/S2R_Map.map"))
	//if (LoadMappingFile("I:/B2E/SMPSPlay/S3K.map"))
	//if (LoadMappingFile("I:/B2E/SMPSPlay/music/done/SrcFiles/SCD_PPZB.map"))
	if (LoadMappingFile("I:/B2E/SMPSPlay/S1neko_Map.map"))
		printf("Error loading Mapping File!\n");
	
	if (LoadDACData("data/DAC.ini"))
	//if (LoadDACData("data_new/S3K.ini"))
		printf("Error loading DAC ini!\n");
	
	StartEngine();
	InterpretFile(0);
	OpenOPNDriver();
	while(! VGMEnd)
	{
		if (_kbhit())
		{
			if (_getch() == 0x1B)
				break;
		}
		Sleep(100);
	}
	CloseOPNDriver();
	
	FreeGYBFile();
	FreePSGEnvelopes();
	FreeDACData();
	
	DeinitChips();
	
#ifdef _DEBUG
	printf("Press any key ...");
	_getch();
#endif
	
	return 0;
}

UINT8 OpenOPNDriver(void)
{
	strcpy(SoundLogFile, "WaveLog.wav");
	SoundLogging(false);
	if (StartStream(0x00))
	{
		//printf("Error openning Sound Device!\n");
		CloseOPNDriver();
		return 0xC0;
	}
	
	//PauseStream(true);
	
	return 0x00;
}

void CloseOPNDriver(void)
{
	StopStream(false);
	
	return;
}

void CloseOPNDriver_Unload(void)
{
	StopStream(true);
	
	return;
}

void OPNAPI OPN_Write(UINT8 ChipID, UINT16 Register, UINT8 Data)
{
	UINT8 RegSet;
	
	//if (ChipID >= OPN_CHIPS)
	//	return;
	
	if (Register == 0x28 && (Data & 0xF0))
	{
		// Note On - Resume Stream
	//	NullSamples = 0;
		//PauseStream(false);
	}
	
	//if (NullSamples == 0xFFFFFFFF)	// if chip is paused, do safe update
	//	GetChipStream(0x00, ChipID, StreamBufs, 1);
	
	RegSet = Register >> 8;
	ym2612_w(ChipID, 0x00 | (RegSet << 1), Register & 0xFF);
	ym2612_w(ChipID, 0x01 | (RegSet << 1), Data);
	
	return;
}

void OPNAPI OPN_Mute(UINT8 ChipID, UINT8 MuteMask)
{
	//if (ChipID >= OPN_CHIPS)
	//	return;
	
	ym2612_set_mute_mask(ChipID, MuteMask);
	
	return;
}

static UINT32 GetFileLength(const char* FileName)
{
	FILE* hFile;
	UINT32 FileSize;
	
	hFile = fopen(FileName, "rb");
	if (hFile == NULL)
		return 0xFFFFFFFF;
	
	fseek(hFile, 0x00, SEEK_END);
	FileSize = ftell(hFile);
	
	fclose(hFile);
	
	return FileSize;
}

#define FCC_MIDH	0x6468544D		// 'MThd'
#define FCC_MIDT	0x6B72544D		// 'MTrk'
static bool OpenVGMFile(const char* FileName)
{
	FILE* hFile;
	UINT32 FileSize;
	UINT32 fccHeader;
	UINT32 CurPos;
	UINT8 TempByt;
	//UINT16 TempSht;
	UINT32 TempLng;
	UINT8* TempOp;
	
	FileSize = GetFileLength(FileName);
	
	hFile = fopen(FileName, "rb");
	if (hFile == NULL)
		return false;
	
	fseek(hFile, 0x00, SEEK_SET);
	fread(&fccHeader, 0x04, 0x01, hFile);
	if (fccHeader != FCC_MIDH)
		goto OpenErr;
	
	VGMTag.strTrackNameE = L"";
	//VGMTag.strGameNameE = L"";
	VGMTag.strGameNameE = (wchar_t*)malloc(0x10 * sizeof(wchar_t*));
	wcscpy(VGMTag.strGameNameE, L"    Player");
	//VGMTag.strSystemNameE = L"";
	VGMTag.strSystemNameE = L"PC / MS-DOS";
	VGMTag.strAuthorNameE = L"";
	VGMTag.strReleaseDate = L"";
	VGMTag.strCreator = L"";
	VGMTag.strNotes = L"";
	
	VGMDataLen = FileSize;
	
	// Read Data
	VGMData = (UINT8*)malloc(VGMDataLen);
	if (VGMData == NULL)
		goto OpenErr;
	fseek(hFile, 0x00, SEEK_SET);
	fread(VGMData, 0x01, VGMDataLen, hFile);
	fclose(hFile);
	hFile = NULL;
	
ReReadMIDIHead:
	CurPos = 0x00;
	memcpy(&TempLng, &VGMData[CurPos + 0x04], 0x04);
	SwapBytes(&TempLng, 0x04);
	CurPos += 0x08;
	
	memcpy(&MIDIHead, &VGMData[CurPos], sizeof(MID_HEADER));
	SwapBytes(&MIDIHead.shtFormat, 0x02);
	SwapBytes(&MIDIHead.shtTracks, 0x02);
	SwapBytes(&MIDIHead.shtResolution, 0x02);
	if (MIDIHead.shtFormat == 0x00 && MIDIHead.shtTracks > 0x01)
	{
		printf("Bad MIDI Header - Format 0 with multiple tracks!\n");
		VGMData[CurPos + 0x01] = 0x01;
		MIDIHead.shtFormat = 0x01;
	}
	CurPos += TempLng;
	
	if (MIDIHead.shtFormat == 0x01 && MIDIHead.shtTracks > 0x01)
	{
		TempByt = MIDI1to0(VGMDataLen, VGMData, &FileSize, &TempOp);
		free(VGMData);
		if (TempByt)
		{
			printf("MIDI1to0 Conversion failed!\n");
			return false;
		}
		
		VGMData = TempOp;
		VGMDataLen = FileSize;
		MIDIHead.shtTracks = 0x01;
		
		goto ReReadMIDIHead;
	}
	
	VGMTag.strGameNameE[0x00] = 'M';
	VGMTag.strGameNameE[0x01] = 'I';
	VGMTag.strGameNameE[0x02] = 'D';
	
	memset(&VGMHead, 0x00, sizeof(VGM_HEADER));
	VGMHead.lngEOFOffset = VGMDataLen;
	VGMHead.lngVersion = 0x00000100 | MIDIHead.shtFormat;
	VGMHead.lngDataOffset = 0x0016;
	VGMSampleRate = MIDIHead.shtResolution;
	BaseTempo = 500000;
	VGMHead.lngTotalSamples = 0;
	
	//if (! OPL_MODE)
	//	OPL_MODE = 0x03;
	
	if (hFile)
		fclose(hFile);
	return true;

OpenErr:

	if (hFile)
		fclose(hFile);
	return false;
}

static void SwapBytes(void* Buffer, UINT32 Bytes)
{
	// Used to convert Little Endian to Big Endian
	char* TempBuf;
	UINT32 PosA;
	UINT32 PosB;
	char TempByt;
	
	TempBuf = (char*)Buffer;
	PosA = 0x00;
	PosB = Bytes;
	do
	{
		PosB --;
		TempByt = TempBuf[PosB];
		TempBuf[PosB] = TempBuf[PosA];
		TempBuf[PosA] = TempByt;
		PosA ++;
	} while(PosA < PosB);
	
	return;
}

static __inline INT32 SampleVGM2Playback(INT32 SampleVal)
{
	signed __int64 TempQud;
	MIDI_TEMPO* TempTempo;
	UINT32 TickCount;
	
	TempTempo = MidTempo + CurTempo;
	
	TickCount = SampleVal - TempTempo->BaseTick;
	TempQud = (signed __int64)TickCount * SampleRate * TempTempo->Tempo / 1000000 / VGMSampleRate;
	
	return TempTempo->SmplTime + (INT32)TempQud;
}

static __inline INT32 SamplePlayback2VGM(INT32 SampleVal)
{
	signed __int64 TempQud;
	UINT32 TickCount;
	MIDI_TEMPO* TempTempo;
	
	TempTempo = MidTempo + CurTempo;
	
	TickCount = SampleVal - TempTempo->SmplTime;
	TempQud = (signed __int64)TickCount * VGMSampleRate * 1000000 / TempTempo->Tempo / SampleRate;	
	
	return TempTempo->BaseTick + (INT32)TempQud;
}

static void InterpretMIDI(UINT32* SampleCount);
void InterpretFile(UINT32 SampleCount)
{
	UINT32 TempLng;
	
	if (VGMEnd)
	{
		PlayingTime += SampleCount;
		return;
	}
	if (PausePlay)
		return;
	
	//if (Interpreting && SampleCount == 1)
	//	return;
	/*while(Interpreting)
		Sleep(1);
	
	Interpreting = true;*/
	if (! SampleCount)
	{
		InterpretMIDI(&SampleCount);
	}
	else
	{
		while(SampleCount)
		{
			TempLng = SampleCount;
			InterpretMIDI(&TempLng);
			VGMSmplPlayed += TempLng;
			PlayingTime += TempLng;
			SampleCount -= TempLng;
		}
	}
	
	/*if (FMPort && FadePlay)
	{
		TempLng = PlayingTime % (SampleRate / 5);
		if (! TempLng)
		{
			RefreshVolume();
		}
	}*/
	
	//Interpreting = false;
	
	return;
}

static UINT32 GetMIDIDelay(UINT32* DelayLen)
{
	UINT32 CurPos;
	UINT32 DelayVal;
	
	CurPos = VGMPos;
	DelayVal = 0x00;
	do
	{
		DelayVal = (DelayVal << 7) | (VGMData[CurPos] & 0x7F);
	} while(VGMData[CurPos ++] & 0x80);
	
	if (DelayVal > 0x01000000)
	{
		if (ErrMsg)
			printf("Unrealistic large delay found!\n");
		DelayVal &= 0xFF;
	}
	if (DelayLen != NULL)
		*DelayLen = CurPos - VGMPos;
	return DelayVal;
}

void DoShortMidiEvent(UINT8 Command, UINT8 Value1, UINT8 Value2);
void DoLongMidiEvent(UINT8 Command, UINT8 Value, UINT32 DataLen, UINT8* Data);
static void InterpretMIDI(UINT32* SampleCount)
{
	INT32 SmplPlayed;
	UINT8 Command;
	UINT8 Channel;
	//UINT8 TempByt;
	//UINT16 TempSht;
	UINT32 TempLng;
	INT32 TempSLng;
	//float SmplDivdr;
	UINT32 DataLen;
	//INT32 PitchVal;
	bool FileEnd;
	bool LoopBack;
	MIDI_TEMPO* TempTempo;
	char TempStr[0x10];
	bool RecalcPbTime;
	
	if (! *SampleCount)
	{
		MidiTrk = 0x0000;
		//TempLng = VGMPos;
		SmplPlayed = VGMSmplPos;
		VGMPos = VGMHead.lngDataOffset;
		FileEnd = false;
		TempoCount = 0x01;
		
		TempTempo = (MIDI_TEMPO*)TempStr;	// Used instead of an extra buffer
		TempTempo->BaseTick = 0;
		TempTempo->Tempo = BaseTempo;
		TempTempo->SmplTime = 0;
		while(! FileEnd && VGMPos < VGMDataLen)
		{
			VGMSmplPos += GetMIDIDelay(&DataLen);
			VGMPos += DataLen;
			
			Command = VGMData[VGMPos];
			if (Command & 0x80)
				VGMPos ++;
			else
				Command = LastMidCmd;
			Channel = Command & 0x0F;
			
			switch(Command & 0xF0)
			{
			case 0xF0:
				switch(Command)
				{
				case 0xF0:
					DataLen = GetMIDIDelay(&TempLng);
					VGMPos += TempLng + DataLen;
					break;
				case 0xFF:
					LoopBack = false;
					switch(VGMData[VGMPos])
					{
					case 0x2F:	// End Of File
						FileEnd = true;
						break;
					case 0x51:
						TempLng = VGMSmplPos - TempTempo->BaseTick;
						//if (! TempLng)
						//	TempoCount --;
						TempLng = (UINT32)((unsigned __int64)TempLng * SampleRate *
									TempTempo->Tempo / 1000000 / VGMSampleRate);
						
						TempTempo->BaseTick = VGMSmplPos;
						TempTempo->SmplTime += TempLng;
						
						TempLng = 0x00000000;
						memcpy(&TempLng, &VGMData[VGMPos + 0x02], 0x03);
						SwapBytes(&TempLng, 0x03);
						TempTempo->Tempo = TempLng;
						
						TempoCount ++;
						break;
					}
					if (! LoopBack)
					{
						VGMPos ++;
						DataLen = GetMIDIDelay(&TempLng);
						VGMPos += TempLng + DataLen;
					}
					break;
				default:
					VGMPos += 0x01;
				}
				break;
			case 0x80:
			case 0x90:
			case 0xA0:
			case 0xB0:
			case 0xE0:
				VGMPos += 0x02;
				break;
			case 0xC0:
			case 0xD0:
				VGMPos += 0x01;
				break;
			}
			if (Command < 0xF0)
				LastMidCmd = Command;
		}
		VGMHead.lngLoopOffset = 0x00000000;
		VGMHead.lngTotalSamples = VGMSmplPos;
		VGMHead.lngLoopSamples = VGMHead.lngTotalSamples;
		VGMHead.lngEOFOffset = VGMPos;
		
		TempLng = VGMSmplPos - TempTempo->BaseTick;
		TempLng = (UINT32)((unsigned __int64)TempLng * SampleRate *
					TempTempo->Tempo / 1000000 / VGMSampleRate);
		TotalPbSamples = TempTempo->SmplTime + TempLng;
		
		MidTempo = (MIDI_TEMPO*)malloc(TempoCount * sizeof(MIDI_TEMPO));
		CurTempo = 0x00;
		TempTempo = MidTempo + CurTempo;
		TempTempo->BaseTick = 0;
		TempTempo->Tempo = BaseTempo;
		TempTempo->SmplTime = 0;
		TempoCount = 0x00;
		
		VGMPos = VGMHead.lngDataOffset;
		VGMSmplPos = SmplPlayed;
		MidiTrk = 0x0000;
		return;
	}
	
	if (*SampleCount > SampleRate)
		*SampleCount = SampleRate;	// dirty hack - needs bugfix
	SmplPlayed = SamplePlayback2VGM(VGMSmplPlayed + *SampleCount);
	while(true)
	{
		if (VGMPos >= VGMDataLen)
		{
			VGMEnd = true;
			break;
		}
		
		TempLng = GetMIDIDelay(&DataLen);
		if (VGMSmplPos + (INT32)TempLng > SmplPlayed)
			break;
		VGMSmplPos += TempLng;
		VGMPos += DataLen;
		
		Command = VGMData[VGMPos];
		if (Command & 0x80)
			VGMPos ++;
		else
			Command = LastMidCmd;
		switch(Command & 0xF0)
		{
		case 0xF0:	// SysEx and Meta Events
			switch(Command)
			{
			case 0xF0:	// System Exclusive Data
				DataLen = GetMIDIDelay(&TempLng);
				VGMPos += TempLng + DataLen;
				DoLongMidiEvent(Command, 0x00, DataLen, &VGMData[VGMPos]);
				break;
			case 0xFF:	// Meta Events
				LoopBack = false;
				switch(VGMData[VGMPos + 0x00])
				{
				case 0x2F:	// End Of File
					VGMEnd = true;
					break;
				case 0x51:	// Tempo
					TempLng = 0x00000000;
					memcpy(&TempLng, &VGMData[VGMPos + 0x02], 0x03);
					SwapBytes(&TempLng, 0x03);
					/*VGMSampleRate = (UINT32)((unsigned __int64)
									MIDIHead.shtResolution * 1000000 / TempLng);*/
					TempSLng = SampleVGM2Playback(VGMSmplPos);
					
					if (MidTempo[CurTempo].BaseTick != VGMSmplPos)
						CurTempo ++;
					TempTempo = MidTempo + CurTempo;
					TempTempo->BaseTick = VGMSmplPos;
					TempTempo->Tempo = TempLng;
					TempTempo->SmplTime = TempSLng;
					if (CurTempo + 0x01 > TempoCount)
						TempoCount = CurTempo + 0x01;
					
					RecalcPbTime = true;
					break;
				}
				
				if (! LoopBack)
				{
					VGMPos ++;
					DataLen = GetMIDIDelay(&TempLng);
					VGMPos += TempLng + DataLen;
				}
				break;
			default:
				VGMPos += 0x01;
				break;
			}
			
			if (RecalcPbTime)
			{
				TempLng = SampleVGM2Playback(VGMSmplPos);
				*SampleCount = TempLng - VGMSmplPlayed;
				//SmplPlayed = SamplePlayback2VGM(VGMSmplPlayed + *SampleCount);
				RecalcPbTime = false;
				return;
			}
			
			break;
		case 0x80:	// Note Off
		case 0x90:	// Note On
		case 0xA0:	// Note Aftertouch
		case 0xB0:	// Controller
		case 0xE0:	// Pitch Bend
		//	if ((Command & 0x0F) >= 0x06 && (Command & 0x0F) <= 0x08)
		//		Command += 0x04;
			DoShortMidiEvent(Command, VGMData[VGMPos + 0x00], VGMData[VGMPos + 0x01]);
			VGMPos += 0x02;
			break;
		case 0xC0:	// Patch Change
		case 0xD0:	// Channel Aftertouch
		//	if ((Command & 0x0F) >= 0x06 && (Command & 0x0F) <= 0x08)
		//		Command += 0x04;
			DoShortMidiEvent(Command, VGMData[VGMPos + 0x00], 0x00);
			VGMPos += 0x01;
			break;
		}
		if (Command < 0xF0)
			LastMidCmd = Command;
		
		if (VGMEnd)
			break;
	}
	
	return;
}

INLINE INT16 Limit2Short(INT32 Value)
{
	INT32 NewValue;
	
	NewValue = Value;
	if (NewValue < -0x8000)
		NewValue = -0x8000;
	if (NewValue > 0x7FFF)
		NewValue = 0x7FFF;
	
	return (INT16)NewValue;
}

void FillBuffer(WAVE_16BS* Buffer, UINT32 BufferSize)
{
	UINT32 CurSmpl;

	InterpretFile(BufferSize);
	if (Buffer == NULL)
	{
		FillBuffer32(NULL, BufferSize);
		return;
	}
	FillBuffer32(TempSmplBuffer, BufferSize);

	for (CurSmpl = 0x00; CurSmpl < BufferSize; CurSmpl ++)
	{
		Buffer[CurSmpl].Left = Limit2Short(TempSmplBuffer[CurSmpl].Left >> 7);
		Buffer[CurSmpl].Right = Limit2Short(TempSmplBuffer[CurSmpl].Right >> 7);
	}

	return;
}
