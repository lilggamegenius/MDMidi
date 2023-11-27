// MegaDrive MIDI Player
// ---------------------
// MIDI Command Processor
/*3456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456
0000000001111111111222222222233333333334444444444555555555566666666667777777777888888888899999*/

// TODO: Take "rest" notes (vol 1-16) into account

#include <stdio.h>
#include <memory.h>
#include <math.h>
//#include "stdtype.h"
#include "chips/mamedef.h"
#include "stdbool.h"
#include "Structs.h"
void PlayDACSample(UINT8 ChipID, UINT8 Sound);	// from Sound.c
void OverrideDACRate(UINT8 ChipID, UINT8 Rate);	// from Sound.c
void SetDACVol(UINT8 ChipID, UINT8 Volume);		// from Sound.c
void Sound_WakeUp(void);						// from Sound.c
void InitMappingData(void);						// from Loader.c

#include "chips/2612intf.h"
#include "chips/sn764intf.h"


typedef struct fm_psg_channel
{
/*	UINT8 NoteMsk;		// Note Mask for the that play on this FM Channel
						// Bit 0 is used, Bit 1 is used only in Rhythm Mode
	UINT8 MidCh;		// Midi Channel that uses this FM Channel
	UINT8 Ins;			// current FM Instrument on this FM Channel
	UINT8 NoteLast;		// Last Note that was played on this FM Channel
	UINT16 StnFact;		// Sustain Factor
	INT32 Delay;*/
	
	// Use Mode Bits:
	//	Bit 0 (01) - Note is On
	//	Bit 1 (02) - Is panned to the left/right (FM only)
	//	Bit 2 (04) - Hold Notes (disable NoteOff)
	//	Bit 4 (10) - Is in Drum Mode
	//	Bit 5 (20) - had volume change (volume of drum channel is used it not set)
	//	Bit 7 (80) - DAC is Enabled (FM6 only)
	//				 Is in Noise Mode (PSG only)
	UINT8 UseMode;
	UINT8 InsMode;	// Bit 0 (01) - current bank, Bit 4 (10) - no-drum instrument bank
	UINT16 CurIns;
	UINT16 Ins;
	INT8 InsDisplc;
	UINT8 NoteHeight;
	UINT8 NoteVolume;
	UINT8 Volume;
	union
	{
		UINT8 PanAFMS;	// FM Channels only
		UINT8 EnvIdx;	// PSG Channels only
	};
	union
	{
		UINT8 AlgoMask;	// FM Channels only
		UINT8 EnvCache;	// PSG Channels only
	};
	UINT8 NoiseVal;
	
	UINT8 NoteFillMode;
	UINT8 NoteFill;
	UINT8 NoteFillRem;
	
	UINT8 ModulationOn;
	UINT8 ModWait;
	UINT8 ModStepWait;
	UINT8 ModRemSteps;		// Remaining Steps until next frequency change
	INT8 ModFreqDelta;		// 
	UINT16 ModFreqDiff;
	UINT8 ModData[0x04];	// Settings: Wait, Step Size, Step Cnt
	
	UINT16 FNum;
	UINT16 FNumFinal;
	
	UINT32 TickCount;	// number of ticks, that the note is already running
	
	UINT32 EnvTicks;	// Samples until next Envelope Byte
	UINT32 ModTicks;	// Samples until next Modulation calculation
	UINT32 NoteFillTicks;	// Samples until next Note Fill calculation
	
	const GYB_INSTRUMENT_V3* GybIns;
} FMPSG_CHN;

static const UINT8 AlgoTLMask[0x08] =
	{0x08, 0x08, 0x08, 0x08, 0x0C, 0x0E, 0x0E, 0x0F};
	//  4     4     4     4    34   234   234  1234

typedef struct midi_channel
{
	// General Attributes
	bool IsDrum;		// Is Drum Channel
	UINT8 Ins;			// Instrument
	UINT8 BnkSel[0x02];	// Bank Select (MSB, LSB)
	UINT8 ChnVol[0x02];	// Channel Volume (0x00 - Main Volume, 0x01 - Expression)
	INT8 ChnVolB;		// Volume Boost
	UINT16 ChnVolCache;	// ChnVol[0] * ChnVol[1]
	UINT8 Pan;			// Pan Controller
	UINT8 LastNote;
	INT32 Pitch;		// Pitch Bend
	UINT8 ModDepth;		// Modulation Depth (Controller Value)
	UINT8 ModDepth2;	// Modulation Depth LSB (Controller Value)
	UINT8 ModDepth3;	// Volume LSB (Controller Value)
	UINT8 VibRate;		// Vibrato Rate
	UINT8 RPNVal[0x04];	// current RPN/NRPN Setup (MSB, LSB, Data MSB, Data LSB)
	UINT8 PbDepth;		// PitchBend Depth in Semitones
	INT8 TuneFine;
	INT8 TuneCoarse;
	UINT8 SustainVal;
	UINT8 SustainOn;	// Bit 0 - Sustain, Bit 1 - Sostenuto
	UINT8 PortamntOn;
	UINT8 SpCtrl[0x04];	// 00: Noise Mode (03)
						// 01: Note Fill (09)
						// 02: [VB] DAC Play Mode (14)
						// 03: [VB] Note Fill Mode (29)
	UINT8 ModCfg[0x04];
	
	// Calculation Variables
//	UINT16 ModStep;		// Modulation Step
//	INT16 ModPb;		// Modulation Pitch
	INT32 TunePb;		// Tuning Pitch Bend
} MIDI_CHN;


static UINT16 GetOPNNote(UINT8 Note, UINT8 Channel);
static UINT16 GetPSGNote(UINT8 Note, UINT8 Channel);
static void Write_OPN(UINT8 Channel, UINT8 Register, UINT8 Value);
static void Write_PSG(UINT8 Channel, UINT8 Register, UINT16 Value);
static void DoNoteOn(UINT8 MIDIChn, UINT8 Channel, UINT8 Note, UINT8 NoteVol);
static void DoNoteOff(UINT8 Channel);
static void PlayNote(UINT8 Command, UINT8 Value, UINT8 Velocity);
static void SetFMInstrument(UINT8 Channel, UINT8 InsBank, UINT16 Ins);
static void SetFMVolume(UINT8 Channel, UINT8 Volume);
static void SetPSGInstrument(UINT8 Channel, UINT16 Env);
static void SetPSGVolume(UINT8 Channel, UINT8 Volume);
static UINT8 GetFMVolume(float Volume, INT8 VolBoost, UINT8 PanVal);
static UINT8 GetPSGVolume(float Volume, INT8 VolBoost);
static float GetMidiNoteVolume(UINT8 NoteVol, UINT8 Channel);
static UINT16 GetMappedIns(const GYB_MAP_ILIST_V3* MapList, UINT8 BnkMSB, UINT8 BnkLSB);
static UINT16 GetMappedMidiIns(MIDI_CHN* TempMid);
static void ApplyCtrlToChannels(UINT8 MIDIChn, UINT8 Ctrl);
static void SetModulationData(FMPSG_CHN* ChnData, UINT8 ResetFreq);
static void SetNoteFill(FMPSG_CHN* ChnData);
static UINT8 CalcUpdateFrames(UINT32 SampleCount, UINT32* TickValue);
static void PSGAdvanceEnv(UINT8 Channel, UINT8 Steps);
static void ModulationUpdate(UINT8 Channel, UINT8 Steps);
static void NoteFillUpdate(UINT8 Channel, UINT8 Steps);


//FMPSG_CHN DACChn;
FMPSG_CHN FMChn[0x06];
FMPSG_CHN PSGChn[0x04];
MIDI_CHN MidChn[0x10];
UINT8 LFOVal;


GYB_FILE_V3 GYBData;
//DAC_MAP_FILE DACMapData;
PSG_ENV_FILE PSGEnvData;
DRUM_MAPPING DrumMapping;

UINT8 LastMidCmd;
MIDI_CHN MidChn[0x10];
// GM Mode - used to detect additional Drum Channels
//	00 - GM
//	01 - GS
//	02 - XG
UINT8 GMMode;
//INT32 MMstTuning;
//UINT8 MMstVolume;

extern unsigned long int SampleRate;
UINT32 SmplPerFrame;
UINT32 NoiseDrmSmplLimit;
bool S2R_Features;

void InitEngine(void)
{
	InitMappingData();
	
	LFOVal = 0x00;
	//memset(&DACChn, 0x00, sizeof(FMPSG_CHN) * 0x01);
	memset(FMChn, 0x00, sizeof(FMPSG_CHN) * 0x06);
	memset(PSGChn, 0x00, sizeof(FMPSG_CHN) * 0x03);
	
	SmplPerFrame = SampleRate / 60;
	NoiseDrmSmplLimit = SampleRate * 5;
	S2R_Features = false;
	
	return;
}

void StartEngine(void)
{
	UINT8 CurChn;
	MIDI_CHN* TempMid;
	FMPSG_CHN* TempChn;
	
	// Reset Chips
	device_reset_ym2612(0x00);
	device_reset_sn764xx(0x00);
	
	// Reset FM/PSG Channel Structures
	LFOVal = 0x00;
	//memset(&DACChn, 0x00, sizeof(FMPSG_CHN) * 0x01);
	
	for (CurChn = 0x00; CurChn < 0x06; CurChn ++)
	{
		TempChn = &FMChn[CurChn];
		memset(TempChn, 0x00, sizeof(FMPSG_CHN));
		//TempChn->UseMode = 0x00;
		TempChn->Ins = 0xFFFF;
		TempChn->CurIns = 0xFFFF;
	}
	
	for (CurChn = 0x00; CurChn < 0x04; CurChn ++)
	{
		TempChn = &PSGChn[CurChn];
		memset(TempChn, 0x00, sizeof(FMPSG_CHN));
		//TempChn->UseMode = 0x00;
		//TempChn->Ins = 0x00;
		//TempChn->CurIns = 0x00;
	}
	
	for (CurChn = 0x00; CurChn < 0x10; CurChn ++)
	{
		TempMid = &MidChn[CurChn];
		memset(TempMid, 0x00, sizeof(MIDI_CHN));
		
		TempMid->IsDrum = (CurChn == 0x09);
		TempMid->ChnVol[0x00] = 100;
		TempMid->ChnVol[0x01] = 0x7F;
		TempMid->ChnVolB = 0x00;
		TempMid->ChnVolCache = TempMid->ChnVol[0x00] * TempMid->ChnVol[0x01];
		TempMid->Pan = 0x40;
		TempMid->RPNVal[0x00] = 0x7F;
		TempMid->RPNVal[0x01] = 0x7F;
		TempMid->PbDepth = 0x02;
		TempMid->SpCtrl[0x00] = 0x7F;
		
		ApplyCtrlToChannels(CurChn, 0x07);
		ApplyCtrlToChannels(CurChn, 0x0A);
	}
	
	LastMidCmd = 0x00;
	GMMode = 0x00;
	
	/* //Print Note Value list
	// FM Notes
	for (CurChn = 0; CurChn < 96; CurChn ++)
	{
		if (! (CurChn % 12))
			printf("\n");
		printf("0x%04X, ", GetOPNNote(CurChn + 11, 0));
	}
	// PSG Notes
	for (CurChn = 0; CurChn < 96; CurChn ++)
	{
		if (! (CurChn % 12))
			printf("\n");
		printf("0x%04X, ", GetPSGNote(CurChn + 48, 0));
	}*/
	
	return;
}

static UINT16 GetOPNNote(UINT8 Note, UINT8 Channel)
{
	double FreqVal;
	INT8 BlockVal;
	UINT16 KeyVal;
	INT32 CurPitch;
	double CurNote;
	MIDI_CHN* TempMid;
	
	// Note: The SMPS 68k frequency table starts with an unused H/B frequency.
	//       That's because note values start with 81, but before reading
	//       the table, only 80 is subtracted. (BUT 81 is subtracted on PSG channels)
	//       The SMPS Z80 frequency table starts with C.
	if (Note < 11)
		return GetOPNNote(11, Channel);
	else if (Note > 11 + 95)
		return GetOPNNote(11 + 95, Channel);
	
	TempMid = MidChn + Channel;
	//CurPitch = MMstTuning + TempMid->TunePb + TempMid->Pitch + TempMid->ModPb;
	CurPitch = TempMid->TunePb + TempMid->Pitch;
	
	CurNote = Note + CurPitch / 8192.0;
	FreqVal = 440.0 * pow(2.0, (CurNote - 69) / 12.0);
	
	// the Octave change takes place at an H (B).
	BlockVal = ((INT16)(CurNote + 1) / 12) - 1;
	if (BlockVal < 0x00)
		BlockVal = 0x00;
	else if (BlockVal > 0x07)
		BlockVal = 0x07;
	//KeyVal = (INT16)(FreqVal * (1 << (21 - BlockVal)) / CHIP_RATE + 0.5);
	KeyVal = (UINT16)((144.0 * FreqVal / 7670454.0) * (1 << (21 - BlockVal)) + 0.5);
	if (KeyVal > 0x07FF)
		KeyVal = 0x07FF;
	
	return (BlockVal << 11) | KeyVal;
}

static UINT16 GetPSGNote(UINT8 Note, UINT8 Channel)
{
	double FreqVal;
	UINT16 KeyVal;
	INT32 CurPitch;
	double CurNote;
	MIDI_CHN* TempMid;
	
	// simulate SMPS PSG frequency table limit (starting with C)
#if 1	// Sonic 1 Frequency Range
	if (Note < 48)
	{
		if (! S2R_Features)	// S1 -> lower limit is 48
			return GetPSGNote(48, Channel);
		else if (Note < 44)	// S2R-> lower limit is 44
			return GetPSGNote(44, Channel);
	}
	else if (Note >= 48 + 69)
	{
		return 0x0000;
	}
#else	// Sonic 3K Frequency Range
	if (Note < 36)
		return GetPSGNote(36, Channel);
	else if (Note >= 48 + 70)
		return 0x0000;
#endif
	
	TempMid = MidChn + Channel;
	//CurPitch = MMstTuning + TempMid->TunePb + TempMid->Pitch + TempMid->ModPb;
	CurPitch = TempMid->TunePb + TempMid->Pitch;
	
	CurNote = Note + CurPitch / 8192.0;
	FreqVal = 440.0 * pow(2.0, (CurNote - 69) / 12.0);
	
	KeyVal = (UINT16)(3579545.0 / (32 * FreqVal) + 0.5);
	if (KeyVal > 0x03FF)
		KeyVal = 0x03FF;
	
	return KeyVal;
}

static void Write_OPN(UINT8 Channel, UINT8 Register, UINT8 Value)
{
	UINT8 Port;
	UINT8 Reg;
	
	if (Register < 0x30)
	{
		Port = 0x00;
		Reg = Register;
		if (Register == 0x28)
			Value |= ((Channel / 3) << 2) | (Channel % 3);
	}
	else
	{
		Port = Channel / 3;
		Reg = Register | (Channel % 3);
	}
	
	Port <<= 1;
	ym2612_w(0x00, Port | 0x00, Reg);
	ym2612_w(0x00, Port | 0x01, Value);
	
	return;
}

static void Write_PSG(UINT8 Channel, UINT8 Register, UINT16 Value)
{
	UINT8 Data;
	
	switch(Register)
	{
	case 0x00:	// Frequency
		Data = 0x80 | (Channel << 5) | (Value & 0x00F);
		sn764xx_w(0x00, 0x00, Data);
		Data = (Value & 0x3F0) >> 4;
		break;
	case 0x01:	// Volume
		Data = 0x90 | (Channel << 5) | (Value & 0x00F);
		break;
	case 0x0F:	// direct write
		Data = Value & 0xFF;
		break;
	}
	sn764xx_w(0x00, 0x00, Data);
	
	return;
}

static void DoNoteOn(UINT8 MIDIChn, UINT8 Channel, UINT8 Note, UINT8 NoteVol)
{
	UINT8 ChnMode;
	MIDI_CHN* TempMid;
	FMPSG_CHN* TempChn;
	UINT8 NewVol;
	
	TempMid = &MidChn[MIDIChn];
	ChnMode = Channel >> 7;
	Channel &= 0x7F;
	switch(ChnMode)
	{
	case 0x00:	// FM
		TempChn = &FMChn[Channel];
		TempChn->NoteHeight = Note - TempChn->InsDisplc;
		TempChn->NoteVolume = NoteVol;
		
		if (! TempMid->PortamntOn && (TempChn->UseMode & 0x01))
		{
			Write_OPN(Channel, 0x28, 0x00);	// Note Off
			TempChn->UseMode &= ~0x01;
		}
		if (TempChn->UseMode & 0x80)
		{
			Write_OPN(0x00, 0x2B, 0x00);
			TempChn->UseMode &= ~0x80;
		}
		
		if ((TempChn->UseMode & 0x30) == 0x10)
			NewVol = GetFMVolume(GetMidiNoteVolume(TempChn->NoteVolume, 0x09),
								MidChn[0x09].ChnVolB, TempChn->UseMode & 0x02);
		else
			NewVol = GetFMVolume(GetMidiNoteVolume(TempChn->NoteVolume, MIDIChn),
								TempMid->ChnVolB, TempChn->UseMode & 0x02);
		if (NewVol != TempChn->Volume)
			SetFMVolume(Channel, NewVol);
		
		TempChn->FNum = GetOPNNote(TempChn->NoteHeight, MIDIChn);
		TempChn->FNumFinal = TempChn->FNum;
		Write_OPN(Channel, 0xA4, (TempChn->FNumFinal & 0xFF00) >> 8);
		Write_OPN(Channel, 0xA0, (TempChn->FNumFinal & 0x00FF) >> 0);
		
		if (! TempMid->PortamntOn || ! (TempChn->UseMode & 0x01))
		{
			Write_OPN(Channel, 0x28, 0xF0);	// Note On
			TempChn->UseMode |= 0x01;
			TempChn->TickCount = 0;
			
			SetModulationData(TempChn, /*! TempMid->PortamntOn*/ true);
			TempChn->ModTicks = SmplPerFrame;
			
			SetNoteFill(TempChn);
			TempChn->NoteFillTicks = SmplPerFrame;
		}
		break;
	case 0x01:	// PSG
		TempChn = &PSGChn[Channel];
		if (Channel == 0x03 && ! TempChn->NoiseVal)
		{
			Channel --;
			TempChn --;
		}
		
		TempChn->NoteHeight = Note;
		TempChn->NoteVolume = NoteVol;
		
		if ((TempChn->UseMode & 0x30) == 0x10)
			NewVol = GetPSGVolume(GetMidiNoteVolume(TempChn->NoteVolume, 0x09),
									MidChn[0x09].ChnVolB);
		else
			NewVol = GetPSGVolume(GetMidiNoteVolume(TempChn->NoteVolume, MIDIChn),
									TempMid->ChnVolB);
		//if (NewVol != TempChn->Volume)
		//	SetPSGVolume(Channel, NewVol);
		
		TempChn->FNum = GetPSGNote(TempChn->NoteHeight, MIDIChn);
		TempChn->FNumFinal = TempChn->FNum;
		if (Channel < 0x03)
			Write_PSG(Channel, 0x00, TempChn->FNumFinal);
		else if (! (TempChn->NoiseVal && (TempChn->NoiseVal & 0x03) != 0x03))
			Write_PSG(0x02, 0x00, TempChn->FNumFinal);	// write only, if "use ch2 freq" is activated
		if (! TempMid->PortamntOn || ! (TempChn->UseMode & 0x01))
		{
			TempChn->UseMode |= 0x01;
			TempChn->TickCount = 0;
			
			TempChn->EnvIdx = 0x00;
			PSGAdvanceEnv(Channel, 0);
			TempChn->EnvTicks = SmplPerFrame;
			
			SetModulationData(TempChn, /*! TempMid->PortamntOn*/ true);
			TempChn->ModTicks = SmplPerFrame;
			
			SetNoteFill(TempChn);
			TempChn->NoteFillTicks = SmplPerFrame;
		}
		SetPSGVolume(Channel, NewVol);	// also does NoteOn
		break;
	}
	Sound_WakeUp();
	
	return;
}

static void DoNoteOff(UINT8 Channel)
{
	UINT8 ChnMode;
	
	ChnMode = Channel >> 7;
	Channel &= 0x7F;
	switch(ChnMode)
	{
	case 0x00:	// FM
		if (FMChn[Channel].UseMode & 0x01)
		{
			Write_OPN(Channel, 0x28, 0x00);	// Note Off
			FMChn[Channel].UseMode &= ~0x01;
		}
		break;
	case 0x01:	// PSG
		if (Channel == 0x03 && ! PSGChn[Channel].NoiseVal)
			Channel --;
		if (PSGChn[Channel].UseMode & 0x01)
		{
			PSGChn[Channel].UseMode &= ~0x01;
			SetPSGVolume(Channel, 0x0F);	// Note Off
		}
		break;
	}
	
	return;
}

static void PlayNote(UINT8 Command, UINT8 Value, UINT8 Velocity)
{
	UINT8 MIDIChn;
	bool NoteOn;
	UINT8 ChipChn;
	MIDI_CHN* TempMid;
	DRUM_SND_MAP* TempDrm;
	FMPSG_CHN* TempChn;
	UINT8 DrmIns;
	UINT8 DrmNote;
	
	MIDIChn = Command & 0x0F;
	TempMid = &MidChn[MIDIChn];
	if (Command & 0x10)	// 0x90
		NoteOn = Velocity ? true : false;
	else				// 0x80
		NoteOn = false;
	
	if (MIDIChn < 0x06)	// 00-05
	{
		// play FM note
		ChipChn = MIDIChn;
		TempChn = &FMChn[ChipChn];
		if (NoteOn && Velocity >= 4)
		{
			TempMid->LastNote = Value;
			if (TempChn->UseMode & 0x10)
			{
				// if in Drum mode, then switch to Melody mode and
				// restore the FM instrument
				TempChn->UseMode &= ~0x10;
				SetFMInstrument(ChipChn, (TempChn->InsMode & 0x10) >> 4, TempChn->Ins);
			}
			DoNoteOn(MIDIChn, 0x00 | ChipChn, Value, Velocity);
		}
		else if (NoteOn)	// mid2smps treats low-velocity NoteOn as NoteOff/rest
		{
			DoNoteOff(0x00 | ChipChn);
			TempMid->LastNote = 0xFF;
		}
		else if (TempMid->LastNote == Value)
		{
			if (! TempMid->PortamntOn && ! (TempChn->UseMode & 0x04))
				DoNoteOff(0x00 | ChipChn);
			TempMid->LastNote = 0xFF;
		}
	}
	else if (MIDIChn < 0x09)
	{
		return;	// not mapped
	}
	else if (MIDIChn == 0x09)	// 09
	{
		if (! NoteOn)
			return;	// Note Offs are ignored
		
		NoteOn = (Velocity >= 0x10);
		// play drum note
		TempDrm = &DrumMapping.Drums[Value];
		ChipChn = TempDrm->Chn;
		DrmIns = TempDrm->ID;
		switch(TempDrm->Type)
		{
		case 0x00:	// None
			return;
		case 0x01:	// DAC
			if (! NoteOn)
				return;	// on the DAC channel, Note Offs are ignored
			if (Velocity < 4)
				return;	// mid2smps treats this as 'rest'
			
			TempChn = &FMChn[0x05];
			if (! (TempChn->UseMode & 0x80))
			{
				Write_OPN(0x00, 0x2B, 0x80);
				TempChn->UseMode |= 0x80;
			}
			
			TempChn->NoteVolume = Velocity;
			if (S2R_Features)
			{
				DrmNote = GetFMVolume(GetMidiNoteVolume(TempChn->NoteVolume, MIDIChn),
										TempMid->ChnVolB * 2 + 8, TempChn->UseMode & 0x02);
				SetDACVol(0x00, DrmNote >> 1);
			}
			PlayDACSample(0x00, DrmIns & 0x7F);
			Sound_WakeUp();
			break;
			/*ChipChn = 0x05;
			DrmIns = GYBData.Bank[0x01].InsMap[Value];
			if (DrmIns == 0xFF)
				break;*/
		case 0x02:	// FM
			if (NoteOn && Velocity >= 4)
			{
				if (TempDrm->Note == 0xFF)
					DrmNote = GYBData.InsBank[GYBBANK_DRUM].InsData[DrmIns].Transp;
				else
					DrmNote = TempDrm->Note;
				
				if (FMChn[ChipChn].UseMode & 0x20)
					MIDIChn = ChipChn;
				FMChn[ChipChn].UseMode |= 0x10;
				SetFMInstrument(ChipChn, 0x01, DrmIns);
				DoNoteOn(MIDIChn, 0x00 | ChipChn, DrmNote, Velocity);
			}
			else
			{
				DoNoteOff(0x00 | ChipChn);
			}
			break;
		case 0x03:	// PSG
			ChipChn = 0x03;
			
			if (NoteOn && Velocity >= 17)
			{
				TempChn = &PSGChn[ChipChn];
				if (! TempChn->NoiseVal)
				{
					TempChn->NoiseVal = 0xE7;
					Write_PSG(ChipChn, 0x0F, TempChn->NoiseVal);
				}
				TempChn->UseMode |= 0x80;
				
				if (TempChn->UseMode & 0x20)
					MIDIChn = 0x0A + ChipChn;
				SetPSGInstrument(ChipChn, DrmIns);
				TempChn->UseMode |= 0x10;
				if (ChipChn & 0x02)	// Channel 2 and 3 share the envelope
				{
					SetPSGInstrument(ChipChn ^ 0x01, DrmIns);
					PSGChn[ChipChn ^ 0x01].UseMode |= 0x10;
				}
				if (TempDrm->Note == 0xFF)
					DrmNote = 0x7F;
				else
					DrmNote = TempDrm->Note;
				DoNoteOn(MIDIChn, 0x80 | ChipChn, 48 + DrmNote, Velocity);
			}
			else
			{
				DoNoteOff(0x80 | ChipChn);
			}
			break;
		default:
			return;
		}
	}
	else if (MIDIChn < 0x0E)	// 0A-0D
	{
		// play PSG note
		ChipChn = MIDIChn - 0x0A;
		if (ChipChn == 0x02 && (PSGChn[0x03].NoiseVal & 0x03) == 0x03)
			ChipChn = 0x03;	// if Ch4 is in "Use Ch2 Frequency" mode, link both channels together
		TempChn = &PSGChn[ChipChn];
		if (NoteOn && Velocity >= 17)
		{
			TempMid->LastNote = Value;
			if (TempChn->UseMode & 0x10)
			{
				TempChn->UseMode &= ~0x10;
				SetPSGInstrument(ChipChn, TempChn->Ins);
			}
			if (MIDIChn == 0x0D)
				Value += 48;
			DoNoteOn(MIDIChn, 0x80 | ChipChn, Value, Velocity);
		}
		else if (NoteOn)	// mid2smps treats low-velocity NoteOn as NoteOff/rest
		{
			DoNoteOff(0x80 | ChipChn);
			TempMid->LastNote = 0xFF;
		}
		else if (TempMid->LastNote == Value)
		{
			if (! TempMid->PortamntOn && ! (TempChn->UseMode & 0x04))
				DoNoteOff(0x80 | ChipChn);
			TempMid->LastNote = 0xFF;
		}
	}
	else
	{
		return;	// not mapped
	}
	
	return;
}

static void SetFMInstrument(UINT8 Channel, UINT8 InsBank, UINT16 Ins)
{
	FMPSG_CHN* TempChn;
	const UINT8* InsData;
	UINT8 CurPos;
	UINT8 CurReg;
	
	TempChn = &FMChn[Channel];
	TempChn->CurIns = Ins;
	if (Ins == 0xFFFF)	// handle unmapped instruments
	{
		// Mute it.
		CurReg = 0x30;
		for (CurPos = 0x00; CurPos < 0x1C; CurPos ++, CurReg += 0x04)
		{
			if (CurPos >= 0x04 && CurPos < 0x08)
				Write_OPN(Channel, CurReg, 0x7F);
			else
				Write_OPN(Channel, CurReg, 0x00);
		}
		Write_OPN(Channel, 0xB0, 0x00);
		TempChn->AlgoMask = 0x00;
		//TempChn->PanAFMS &= 0xC0;
		Write_OPN(Channel, 0xB4, TempChn->PanAFMS);
		
		TempChn->GybIns = NULL;
		TempChn->InsDisplc = 0x00;
		return;
	}
	
	TempChn->InsMode &= ~0x01;
	TempChn->InsMode |= InsBank;
	TempChn->GybIns = &GYBData.InsBank[InsBank & 0x01].InsData[Ins];
	InsData = TempChn->GybIns->Reg;
	
	CurReg = 0x30;
	for (CurPos = 0x00; CurPos < 0x1C; CurPos ++, CurReg += 0x04)
	{
		if (CurPos >= 0x04 && CurPos < 0x08)
			continue;	// skip volume registers
		
		Write_OPN(Channel, CurReg, InsData[CurPos]);
	}
	
	TempChn->AlgoMask = AlgoTLMask[InsData[0x1C] & 0x07];
	Write_OPN(Channel, 0xB0, InsData[0x1C]);
	
	//TempChn->PanAFMS &= 0xC0;
	TempChn->PanAFMS |= InsData[0x1D] & 0x3F;
	Write_OPN(Channel, 0xB4, TempChn->PanAFMS);
	
	SetFMVolume(Channel, TempChn->Volume);
	
	TempChn->InsDisplc = (InsBank & 0x01) ? 0x00 : TempChn->GybIns->Transp;
	
	return;
}

static void SetFMVolume(UINT8 Channel, UINT8 Volume)
{
	FMPSG_CHN* TempChn;
	const UINT8* TLData;
	UINT8 CurOp;
	UINT8 CurReg;
	UINT8 TLMask;
	UINT8 TLVal;
	
	TempChn = &FMChn[Channel];
	if (TempChn->CurIns == 0xFFFF || TempChn->GybIns == NULL)
		return;
	
	TLData = &TempChn->GybIns->Reg[0x04];
	
	TempChn->Volume = Volume;
	TLMask = TempChn->AlgoMask;
	CurReg = 0x40;
	for (CurOp = 0x00; CurOp < 0x04; CurOp ++, CurReg += 0x04)
	{
		TLVal = TLData[CurOp];
		if (TLMask & 0x01)
			TLVal += Volume;
		if (TLVal > 0x7F)
			TLVal = 0x7F;
		Write_OPN(Channel, CurReg, TLVal);
		
		TLMask >>= 1;
	}
	
	return;
}

static void SetPSGInstrument(UINT8 Channel, UINT16 Env)
{
	FMPSG_CHN* TempChn;
	
	TempChn = &PSGChn[Channel];
	if (Env > PSGEnvData.EnvCount)
		Env = 0xFFFF;	// avoid invalid envelopes
	TempChn->CurIns = Env;
	if (TempChn->UseMode & 0x01)
	{
		TempChn->EnvIdx = 0x00;
		PSGAdvanceEnv(Channel, 0);
		SetPSGVolume(Channel, TempChn->Volume);
	}
	
	return;
}

static void SetPSGVolume(UINT8 Channel, UINT8 Volume)
{
	FMPSG_CHN* TempChn;
	UINT8 EnvVol;
	
	TempChn = &PSGChn[Channel];
	TempChn->Volume = (TempChn->UseMode & 0x01) ? Volume : 0x0F;
	
	EnvVol = TempChn->Volume + TempChn->EnvCache;
	if (EnvVol > 0x0F)
		EnvVol = 0x0F;
	Write_PSG(Channel, 0x01, EnvVol);
	
	return;
}

static UINT8 GetFMVolume(float Volume, INT8 VolBoost, UINT8 PanVal)
{
	const UINT8 MAX_LVL = 0x7F;
	const float PAN_DB = -3.0102999566398f;	//20 * Log10(Sin(PI / 2))
	float DBVol;
	float FMVol;
	
	if (Volume > 0.0f)
	{
		// MIDI -> DB:	40 * Log10(MidVol / MaxMidVol)  (formula from GM Dev. Guide)
		// Pan -> DB:	DB += 40 * Log10(Sin(PI / 2 * ((PanVal - 1) / 0x7E)))
		// DB -> OPN:	-DB * (4/3)  (same as OPL)
		
		// simplified calculations for hard-panned notes
		if (! PanVal)
			// Centered notes need to have a little lower volume than hard-panned ones
			// to compensate the additional speaker.
			DBVol = (float)(40 * log10(Volume));
		else
			DBVol = (float)(40 * log10(Volume) - PAN_DB);
		if (DBVol > 0.0f)
			DBVol = 0.0f;
		
		FMVol = 0x08 + 4.0f * -DBVol / 3.0f - VolBoost;
		if (FMVol < 0x00)
			FMVol = 0x00;
		else if (FMVol > MAX_LVL)
			FMVol = MAX_LVL;
	}
	else
	{
		FMVol = MAX_LVL;
	}
	return (UINT8)(FMVol + 0.5f);
}

static UINT8 GetPSGVolume(float Volume, INT8 VolBoost)
{
	float DBVol;
	float PSGVol;
	
	if (Volume > 0.0f)
	{
		// MIDI -> DB:	40 * Log10(MidVol / MaxMidVol)  (formula from GM Dev. Guide)
		// DB -> PSG:	-DBVol / 2 (PSG has 2 db per step)
		DBVol = (float)(40.0 * log10(Volume));
		if (DBVol > 0.0f)
			DBVol = 0.0f;
		
		PSGVol = -DBVol / 2.0f - VolBoost;
		if (PSGVol >= 17)
			PSGVol = 0x0F;
		else if (PSGVol > 0x0E)
			PSGVol = 0x0E;
		else if (PSGVol < 0x00)
			PSGVol = 0x00;
	}
	else
	{
		PSGVol = 0x0F;
	}
	return (UINT8)(PSGVol + 0.5f);
}

static float GetMidiNoteVolume(UINT8 NoteVol, UINT8 Channel)
{
	MIDI_CHN* TempMid;
	UINT32 AllVol;
	
	TempMid = &MidChn[Channel];
	AllVol = NoteVol * TempMid->ChnVolCache;
	
	return AllVol / 2048383.0f;	// 0x7F^3
}

static UINT16 GetMappedIns(const GYB_MAP_ILIST_V3* MapList, UINT8 BnkMSB, UINT8 BnkLSB)
{
	const GYB_MAP_ITEM_V3* TempItm;
	UINT16 CurEnt;
	UINT8 ConditVal;
	
	for (CurEnt = 0x00; CurEnt < MapList->EntryCount; CurEnt ++)
	{
		TempItm = &MapList->Entry[CurEnt];
		// Condition is true if:
		//	Map->Bank == Midi->Bank
		//	or one of both values is set to 0xFF ("all" for Map, "ignore" for Midi)
		// For each part of the condition, the respective bits are cleared.
		ConditVal = 0x03;
		if (BnkMSB == 0xFF || TempItm->BankMSB == 0xFF || TempItm->BankMSB == BnkMSB)
			ConditVal &= ~0x01;
		
		if (BnkLSB == 0xFF || TempItm->BankLSB == 0xFF || TempItm->BankLSB == BnkLSB)
			ConditVal &= ~0x02;
		
		if (! ConditVal)
			return TempItm->FMIns;
	}
	
	return 0xFFFF;
}

static UINT16 GetMappedMidiIns(MIDI_CHN* TempMid)
{
	UINT8 InsBank;
	UINT16 FMInsVal;
	
	// At first, check the special instrument maps at Bank MSB 50h/51h.
	if ((TempMid->BnkSel[0x00] & 0x7E) == 0x50)
	{
		// MSB 50h: unmapped melody bank
		// MSB 51h: unmapped drum bank
		InsBank = TempMid->BnkSel[0x00] & 0x01;
		// LSB 0: instruments 0000h-007Fh
		// LSB 1: instruments 0080h-00FFh
		// etc.
		FMInsVal = (TempMid->BnkSel[0x01] << 7) | TempMid->Ins;
		return (InsBank << 15) | FMInsVal;	// 8000 = drum bank
	}
	else if ((TempMid->BnkSel[0x00] & 0x7E) == 0x58)
	{
		// Note: These two are not supported by mid2smps!
		// MSB 58h: drum instruments (normal drum map)
		// MSB 59h: drum instruments (GYB drum map)
		if (TempMid->BnkSel[0x00] == 0x58)
		{
			FMInsVal = DrumMapping.Drums[TempMid->Ins].ID;
			if (FMInsVal == 0xFF)
				return 0xFFFF;
			else
				return 0x8000 | FMInsVal;
		}
		else
		{
			return GetMappedIns(&GYBData.InsMap[GYBBANK_DRUM].Ins[TempMid->Ins],
								TempMid->BnkSel[0x01], 0xFF);
		}
	}
	
	// Then check the mappings lists.
	return GetMappedIns(&GYBData.InsMap[GYBBANK_MELODY].Ins[TempMid->Ins],
						TempMid->BnkSel[0x00], TempMid->BnkSel[0x01]);
}

static void ApplyCtrlToChannels(UINT8 MIDIChn, UINT8 Ctrl)
{
	MIDI_CHN* TempMid;
	UINT8 Channel;
	FMPSG_CHN* TempChn;
	UINT8 TempByt;
	
	TempMid = &MidChn[MIDIChn];
	if (MIDIChn < 0x06)
	{
		Channel = MIDIChn;
		TempChn = &FMChn[Channel];
		switch(Ctrl)
		{
		case 0xC0:	// Instrument Change
			TempChn->Ins = GetMappedMidiIns(TempMid);
			if (TempChn->Ins == 0xFFFF)
			{
				TempByt = 0x00;
			}
			else
			{
				TempByt = (TempChn->Ins >> 15) & 0x01;
				TempChn->Ins &= 0x7FFF;
				if (TempChn->Ins >= GYBData.InsBank[TempByt].InsCount)
					TempChn->Ins = 0xFFFF;
			}
			
			TempChn->InsMode &= ~0x10;	// clear bit 4, remove all others
			TempChn->InsMode |= (TempByt << 4);
			if (! (TempChn->UseMode & 0x10))	// if not in Drum mode, set instrument
				SetFMInstrument(TempChn - FMChn, TempByt, TempChn->Ins);
			break;
		case 0xE0:	// Pitch Bend
			if (TempChn->UseMode & 0x01)
			{
				TempChn->FNum = GetOPNNote(TempChn->NoteHeight, MIDIChn);
				TempChn->FNumFinal = TempChn->FNum + TempChn->ModFreqDiff;
				Write_OPN(Channel, 0xA4, (TempChn->FNumFinal & 0xFF00) >> 8);
				Write_OPN(Channel, 0xA0, (TempChn->FNumFinal & 0x00FF) >> 0);
			}
			break;
		case 0x07:	// Volume
		case 0x0B:	// Expression
			TempChn->UseMode |= 0x20;
			if (TempChn->UseMode & 0x01)	// Note playing?
			{
				TempByt = GetFMVolume(GetMidiNoteVolume(TempChn->NoteVolume, MIDIChn),
										TempMid->ChnVolB, TempChn->UseMode & 0x02);
				if (TempByt != TempChn->Volume)
					SetFMVolume(Channel, TempByt);
			}
			break;
		case 0x0A:	// Pan
			switch((TempMid->Pan * 3) >> 7)
			{
			case 0x00:	// Left
				TempByt = 0x80;
				TempChn->UseMode |= 0x02;
				break;
			case 0x02:	// Right
				TempByt = 0x40;
				TempChn->UseMode |= 0x02;
				break;
			case 0x01:	// Center
			default:
				TempByt = 0xC0;
				TempChn->UseMode &= ~0x02;
				break;
			}
			TempByt |= (TempChn->PanAFMS & 0x3F);
			if (TempByt != TempChn->PanAFMS)
			{
				Write_OPN(Channel, 0xB4, TempByt);
				TempChn->PanAFMS = TempByt;
				
				if (TempChn->UseMode & 0x01)	// Note playing?
				{
					if ((TempChn->UseMode & 0x30) == 0x10)
					{
						MIDIChn = 0x09;
						TempMid = &MidChn[MIDIChn];
					}
					TempByt = GetFMVolume(GetMidiNoteVolume(TempChn->NoteVolume, MIDIChn),
											TempMid->ChnVolB, TempChn->UseMode & 0x02);
					if (TempByt != TempChn->Volume)
						SetFMVolume(Channel, TempByt);
				}
			}
			break;
		case 0x01:	// Modulation
			if (TempMid->ModDepth < 8)
			{
				TempChn->ModulationOn = 0x00;
			}
			else
			{
				TempChn->ModulationOn = 0x01;
				TempChn->ModData[0x00] = TempMid->ModCfg[0x00];
				TempChn->ModData[0x01] = TempMid->ModCfg[0x01];
				TempChn->ModData[0x02] = TempMid->ModCfg[0x02];
				TempChn->ModData[0x03] = TempMid->ModCfg[0x03];
				// 02 and 03 can be negative
				if (TempChn->ModData[0x02] & 0x40)
					TempChn->ModData[0x02] |= 0x80;
				if (TempChn->ModData[0x03] & 0x40)
					TempChn->ModData[0x03] |= 0x80;
				if (TempChn->ModData[0x01] & 0x60)
				{
					// Bits 6 and 5 of 01 swap Bit 6 of 02 and 03 respectively
					if (TempChn->ModData[0x01] & 0x40)
						TempChn->ModData[0x02] ^= 0x40;
					if (TempChn->ModData[0x01] & 0x20)
						TempChn->ModData[0x03] ^= 0x40;
					TempChn->ModData[0x01] &= ~0x60;
				}
				
				if (! TempChn->ModData[0x01])
					TempChn->ModData[0x01] = 0x01;
				if (! TempChn->ModData[0x02])
					TempChn->ModData[0x02] = TempMid->ModDepth / 0x10;
				if (! TempChn->ModData[0x03])
					TempChn->ModData[0x03] = 0x04;
				SetModulationData(TempChn, /*! TempMid->PortamntOn*/ true);
			}
			break;
		case 0x21:	// Modulation LSB
			TempByt  = (TempMid->ModDepth2 & 0x70) >> 4;	// Frequency Modulation (bits 0-2)
			TempByt |= (TempMid->ModDepth2 & 0x60) >> 1;	// Amplitude Modulation (bits 4-5)
			TempByt |= (TempChn->PanAFMS & 0xC0);
			if (TempByt != TempChn->PanAFMS)
			{
				Write_OPN(Channel, 0xB4, TempByt);
				TempChn->PanAFMS = TempByt;
			}
			break;
		case 0x09:	// SMPS: Note Fill
			TempChn->NoteFill = TempMid->SpCtrl[0x01];
			SetNoteFill(TempChn);
			break;
		case 0x29:	// SMPS VB: Set Note Fill Mode
			TempChn->NoteFillMode = TempMid->SpCtrl[0x03];
			// Modes:
			//	00 - stop after NoteFill frames
			//	01 - stop after (NoteLen - NoteFill) frames#
			//	02 - stop after (NoteLen * NoteFill / 0x80) frames#
			// # - impossible without knowing when it's going to end
			break;
		case 0x40:	// Sustain Pedal - Hold Notes
			if (TempMid->SustainVal < 0x10)
			{
				if (TempMid->LastNote == 0xFF && ! TempMid->PortamntOn)
					DoNoteOff(0x00 | Channel);
				TempChn->UseMode &= ~0x04;
			}
			else
			{
				TempChn->UseMode |= 0x04;
			}
			break;
		case 0x41:	// Portamento On/Off
			if (TempMid->LastNote == 0xFF && ! TempMid->PortamntOn &&
				! (TempChn->UseMode & 0x04))
				DoNoteOff(0x00 | Channel);
			break;
		case 0x4C:	// LFO Speed (Vibrato Rate)
			if (! TempMid->VibRate)
				LFOVal = 0x00;
			else
				LFOVal = 0x08 | (TempMid->VibRate >> 4);	// 00..7F -> 08-0F
			Write_OPN(0x00, 0x22, LFOVal);
			break;
		case 0x79:	// Reset Channel
			TempChn->UseMode &= 0x8F;	// It's important to keep the DAC flag.
			break;
		case 0x80:	// Note Off
			DoNoteOff(0x00 | Channel);
			break;
		case 0x81:	// Sound Off
			SetFMVolume(Channel, 0x7F);
			break;
		}
	}
	else if (MIDIChn == 0x09)
	{
		Channel = 0x05;	// DAC channel
		TempChn = &FMChn[Channel];
		switch(Ctrl)
		{
		case 0x07:	// Volume
		case 0x0B:	// Expression
			if (S2R_Features)
			{
				TempByt = GetFMVolume(GetMidiNoteVolume(TempChn->NoteVolume, MIDIChn),
										TempMid->ChnVolB * 2 + 8, TempChn->UseMode & 0x02);
				SetDACVol(0x00, TempByt >> 1);
			}
			
			TempChn = FMChn;
			for (Channel = 0x00; Channel < 0x06; Channel ++, TempChn ++)
			{
				// drum mode + note playing + use drum volume
				if ((TempChn->UseMode & 0x31) == 0x11)
				{
					TempByt = GetFMVolume(GetMidiNoteVolume(TempChn->NoteVolume, MIDIChn),
											TempMid->ChnVolB, TempChn->UseMode & 0x02);
					if (TempByt != TempChn->Volume)
						SetFMVolume(Channel, TempByt);
				}
			}
			TempChn = PSGChn;
			for (Channel = 0x00; Channel < 0x04; Channel ++, TempChn ++)
			{
				// drum mode + note playing + use drum volume
				if ((TempChn->UseMode & 0x31) == 0x11)
				{
					TempByt = GetPSGVolume(GetMidiNoteVolume(TempChn->NoteVolume, MIDIChn),
											TempMid->ChnVolB);
					if (TempByt != TempChn->Volume)
						SetPSGVolume(Channel, TempByt);
				}
			}
			break;
		case 0x0A:	// Pan
			switch((TempMid->Pan * 3) >> 7)
			{
			case 0x00:	// Left
				TempByt = 0x80;
				TempChn->UseMode |= 0x02;
				break;
			case 0x02:	// Right
				TempByt = 0x40;
				TempChn->UseMode |= 0x02;
				break;
			case 0x01:	// Center
			default:
				TempByt = 0xC0;
				TempChn->UseMode &= ~0x02;
				break;
			}
			TempByt |= (TempChn->PanAFMS & 0x3F);
			if (TempByt != TempChn->PanAFMS)
			{
				Write_OPN(Channel, 0xB4, TempByt);
				TempChn->PanAFMS = TempByt;
			}
			break;
		case 0x14:	// SMPS VB: Set DAC Play Mode
			//TempMid->SpCtrl[0x02];
			break;
		case 0x79:	// Reset Channel
			for (Channel = 0x00; Channel < 0x06; Channel ++)
			{
				if (FMChn[Channel].UseMode & 0x10)
					FMChn[Channel].UseMode &= 0x8F;	// keep the DAC On bit (0x80)
			}
			for (Channel = 0x00; Channel < 0x04; Channel ++)
			{
				if (PSGChn[Channel].UseMode & 0x10)
					PSGChn[Channel].UseMode &= 0x0F;
			}
			break;
		case 0x80:	// Notes Off
			for (Channel = 0x00; Channel < 0x06; Channel ++)
			{
				if (FMChn[Channel].UseMode & 0x10)
					DoNoteOff(0x00 | Channel);
			}
			for (Channel = 0x00; Channel < 0x04; Channel ++)
			{
				if (PSGChn[Channel].UseMode & 0x10)
					DoNoteOff(0x80 | Channel);
			}
			break;
		case 0x81:	// Sound Off
			for (Channel = 0x00; Channel < 0x06; Channel ++)
			{
				if (FMChn[Channel].UseMode & 0x10)
					SetFMVolume(Channel, 0x7F);
			}
			PlayDACSample(0x00, 0x00);
			for (Channel = 0x00; Channel < 0x04; Channel ++)
			{
				if (PSGChn[Channel].UseMode & 0x10)
					SetPSGVolume(Channel, 0x0F);
			}
			break;
		case 0xE0:	// Pitch Bend
			if (S2R_Features)
				OverrideDACRate(0x00, (TempMid->Pitch / 64) & 0x7F);
			break;
		}
	}
	else if (MIDIChn >= 0x0A && MIDIChn <= 0x0D)
	{
		Channel = MIDIChn - 0x0A;
		TempChn = &PSGChn[Channel];
		switch(Ctrl)
		{
		case 0xC0:	// Instrument Change
			if (TempMid->Ins < 0x50)
				TempChn->Ins = TempMid->Ins;
			else
				TempChn->Ins = TempMid->BnkSel[0x01];
			if (! (TempChn->UseMode & 0x10))
			{
				SetPSGInstrument(Channel, TempChn->Ins);
				if ((Channel & 0x02))	// Channel 2 and 3 share the envelope ...
				{
					// ... if Ch4 is in "Use Ch2 Frequency" mode (or NoiseVal isn't set)
					if (! (PSGChn[0x03].NoiseVal && (PSGChn[0x03].NoiseVal & 0x03) != 0x03))
						SetPSGInstrument(Channel ^ 0x01, TempChn->Ins);
				}
			}
			break;
		case 0xE0:	// Pitch Bend
			if (TempChn->UseMode & 0x01)
			{
				TempChn->FNum = GetPSGNote(TempChn->NoteHeight, MIDIChn);
				TempChn->FNumFinal = TempChn->FNum + TempChn->ModFreqDiff;
				if (Channel < 0x03)
					Write_PSG(Channel, 0x00, TempChn->FNumFinal);
				else
					Write_PSG(0x02, 0x00, TempChn->FNumFinal);
			}
			break;
		case 0x07:	// Volume
		case 0x0B:	// Expression
			TempChn->UseMode |= 0x20;
			if (TempChn->UseMode & 0x01)	// Note playing?
			{
				TempByt = GetPSGVolume(GetMidiNoteVolume(TempChn->NoteVolume, MIDIChn),
										TempMid->ChnVolB);
				if (TempByt != TempChn->Volume)
					SetPSGVolume(Channel, TempByt);
			}
			break;
		case 0x03:	// SMPS: Set Noise Mode
			if (Channel != 0x03)	// is this the Noise channel?
				break;
			
			if (TempMid->SpCtrl[0x00] == 0x7F)
				TempByt = 0x00;
			else
				TempByt = 0xE0 | (TempMid->SpCtrl[0x00] & 0x07);
			
			if ((TempByt ^ TempChn->NoiseVal) & 0x80)
				DoNoteOff(0x80 | Channel);	// noise mode change - turn tone off
			TempChn->NoiseVal = TempByt;
			
			if (TempChn->NoiseVal)
			{
				Write_PSG(0x00, 0x0F, TempChn->NoiseVal);
				TempChn->UseMode |= 0x80;
			}
			else
			{
				TempChn->UseMode &= ~0x80;
			}
			break;
		case 0x01:	// Modulation
			if (TempMid->ModDepth < 8)
			{
				TempChn->ModulationOn = 0x00;
			}
			else
			{
				TempChn->ModulationOn = 0x01;
				TempChn->ModData[0x00] = TempMid->ModCfg[0x00];
				TempChn->ModData[0x01] = TempMid->ModCfg[0x01];
				TempChn->ModData[0x02] = TempMid->ModCfg[0x02];
				TempChn->ModData[0x03] = TempMid->ModCfg[0x03];
				// 02 and 03 can be negative
				if (TempChn->ModData[0x02] & 0x40)
					TempChn->ModData[0x02] |= 0x80;
				if (TempChn->ModData[0x03] & 0x40)
					TempChn->ModData[0x03] |= 0x80;
				if (TempChn->ModData[0x01] & 0x60)
				{
					// Bits 6 and 5 of 01 swap Bit 6 of 02 and 03 respectively
					if (TempChn->ModData[0x01] & 0x40)
						TempChn->ModData[0x02] ^= 0x40;
					if (TempChn->ModData[0x01] & 0x20)
						TempChn->ModData[0x03] ^= 0x40;
					TempChn->ModData[0x01] &= ~0x60;
				}
				
				if (! TempChn->ModData[0x01])
					TempChn->ModData[0x01] = 0x02;
				if (! TempChn->ModData[0x02])
					TempChn->ModData[0x02] = TempMid->ModDepth / 0x1C;
				if (! TempChn->ModData[0x03])
					TempChn->ModData[0x03] = 0x02;
				SetModulationData(TempChn, /*! TempMid->PortamntOn*/ true);
			}
			break;
		case 0x09:	// SMPS: Note Fill
			TempChn->NoteFill = TempMid->SpCtrl[0x01];
			SetNoteFill(TempChn);
			break;
		case 0x29:	// SMPS VB: Set Note Fill Mode
			TempChn->NoteFillMode = TempMid->SpCtrl[0x03];
			break;
		case 0x40:	// Sustain Pedal - Hold Notes
			if (TempMid->SustainVal < 0x10)
			{
				if (TempMid->LastNote == 0xFF && ! TempMid->PortamntOn)
					DoNoteOff(0x80 | Channel);
				TempChn->UseMode &= ~0x04;
			}
			else
			{
				TempChn->UseMode |= 0x04;
			}
			break;
		case 0x41:	// Portamento On/Off
			if (TempMid->LastNote == 0xFF && ! TempMid->PortamntOn &&
				! (TempChn->UseMode & 0x04))
				DoNoteOff(0x80 | Channel);
			break;
		case 0x79:	// Reset Channel
			TempChn->UseMode &= 0x0F;
			break;
		case 0x80:	// Note Off
			DoNoteOff(0x80 | Channel);
			break;
		case 0x81:	// Sound Off
			SetPSGVolume(Channel, 0x0F);
			break;
		}
	}
	
	return;
}

static void SetModulationData(FMPSG_CHN* ChnData, UINT8 ResetFreq)
{
	// Reset Modulation Settings
	ChnData->ModWait =				ChnData->ModData[0x00];
	ChnData->ModStepWait =			ChnData->ModData[0x01];
	ChnData->ModFreqDelta = (INT8)	ChnData->ModData[0x02];
	ChnData->ModRemSteps =			ChnData->ModData[0x03] / 2;
	if (ResetFreq)
		ChnData->ModFreqDiff =		0x0000;
	
	return;
}

static void SetNoteFill(FMPSG_CHN* ChnData)
{
	if (! ChnData->NoteFillMode)
		ChnData->NoteFillRem = ChnData->NoteFill;
	else
		ChnData->NoteFillRem = 0x00;
	
	return;
}

// --- Engine Timer Update ---
void UpdateEngine(UINT32 SampleCount)
{
	FMPSG_CHN* TempChn;
	UINT8 CurChn;
//	UINT32 CurFrm;
	UINT8 FrmCount;
	UINT8 OldVal;
	
	for (CurChn = 0x00; CurChn < 0x06; CurChn ++)
	{
		TempChn = &FMChn[CurChn];
		if (! (TempChn->UseMode & 0x01))
			continue;
		
		TempChn->TickCount += SampleCount;
		if (TempChn->ModulationOn)
		{
			FrmCount = CalcUpdateFrames(SampleCount, &TempChn->ModTicks);
			if (FrmCount)
				ModulationUpdate(0x00 | CurChn, FrmCount);
		}
		if (TempChn->NoteFillRem)
		{
			FrmCount = CalcUpdateFrames(SampleCount, &TempChn->NoteFillTicks);
			if (FrmCount)
				NoteFillUpdate(0x00 | CurChn, FrmCount);
		}
		
		if ((TempChn->UseMode & 0x80) && TempChn->TickCount >= NoiseDrmSmplLimit)
			DoNoteOff(0x00 | CurChn);
	}
	
	for (CurChn = 0x00; CurChn < 0x04; CurChn ++)
	{
		TempChn = &PSGChn[CurChn];
		if (! (TempChn->UseMode & 0x01))
			continue;
		
		TempChn->TickCount += SampleCount;
		if (TempChn->EnvIdx != 0xFF)
		{
			// Note: Once every 60 Hz frame, the envelope index is increased.
			FrmCount = CalcUpdateFrames(SampleCount, &TempChn->EnvTicks);
			if (FrmCount)
			{
				OldVal = TempChn->EnvCache;
				PSGAdvanceEnv(CurChn, FrmCount);
				if (OldVal != TempChn->EnvCache)
					SetPSGVolume(CurChn, TempChn->Volume);
			}
		}
		
		if (TempChn->ModulationOn)
		{
			FrmCount = CalcUpdateFrames(SampleCount, &TempChn->ModTicks);
			if (FrmCount)
				ModulationUpdate(0x80 | CurChn, FrmCount);
		}
		if (TempChn->NoteFillRem)
		{
			FrmCount = CalcUpdateFrames(SampleCount, &TempChn->NoteFillTicks);
			if (FrmCount)
				NoteFillUpdate(0x80 | CurChn, FrmCount);
		}
		
		if ((TempChn->UseMode & 0x10) && TempChn->EnvIdx == 0xFF &&
			TempChn->TickCount >= NoiseDrmSmplLimit)
			DoNoteOff(0x80 | CurChn);
	}
	
	return;
}

// Neither of those two functions seem to work even slightly correctly.
/*static UINT8 CalcUpdateFrames_Old1(UINT32 SampleCount, UINT32* TickValue)
{
	// This was the old way. It works the same and is shorter,
	// but it should be also be a lot slower because of more calculations.
	UINT32 FrmCount;
	
	FrmCount = 0;
	while(SampleCount >= *TickValue)
	{
		SampleCount -= *TickValue;
		*TickValue = SmplPerFrame;
		FrmCount ++;
	}
	*TickValue -= SampleCount;
	
	return (UINT8)FrmCount;
}

static UINT8 CalcUpdateFrames_Old2(UINT32 SampleCount, UINT32* TickValue)
{
	// Function rewritten to be faster.
	UINT32 FrmCount;
	
	FrmCount = 0;
	while(SampleCount >= SmplPerFrame)
	{
		SampleCount -= SmplPerFrame;
		FrmCount ++;
	}
	if (SampleCount < *TickValue)
	{
		*TickValue -= SampleCount;
	}
	else
	{
		FrmCount ++;
		*TickValue += SmplPerFrame - SampleCount;
	}
	
	return (UINT8)FrmCount;
}*/

static UINT8 CalcUpdateFrames(UINT32 SampleCount, UINT32* TickValue)
{
	UINT32 FrmCount;
	
	*TickValue += SampleCount;
#if 0
	FrmCount = *TickValue / SmplPerFrame;
	*TickValue = *TickValue % SmplPerFrame;
#else
	// faster for small updates
	FrmCount = 0;
	while(*TickValue >= SmplPerFrame)
	{
		*TickValue -= SmplPerFrame;
		FrmCount ++;
	}
#endif
	
	return (UINT8)FrmCount;
}

static void PSGAdvanceEnv(UINT8 Channel, UINT8 Steps)
{
	FMPSG_CHN* TempChn;
	PSG_ENVELOPE* TempEnv;
	UINT8 EnvVal;
	
	TempChn = &PSGChn[Channel];
	if (TempChn->EnvIdx == 0xFF)
		return;
	if (! TempChn->CurIns)
	{
		TempChn->EnvIdx = 0xFF;
		TempChn->EnvCache = 0x00;
		return;
	}
	else if (TempChn->CurIns == 0xFFFF)
	{
		TempChn->EnvIdx = 0xFF;
		TempChn->EnvCache = 0x0F;
		return;
	}
	
	if (! Steps)
	{
		// process this index again
		TempChn->EnvIdx --;
		Steps ++;
	}
	while(Steps)
	{
		TempChn->EnvIdx ++;
		TempEnv = &PSGEnvData.Envelope[TempChn->CurIns - 1];
		EnvVal = TempEnv->Data[TempChn->EnvIdx];
		if (EnvVal & 0x80)
		{
			switch(EnvVal)
			{
			case 0x80:
			case 0x81:	// Stay at this volume level
				TempChn->EnvIdx = 0xFF;
				return;
			case 0x83:	// stop the note
				TempChn->EnvIdx = 0xFF;
				DoNoteOff(0x80 | Channel);
				return;
			}
		}
		else
		{
			TempChn->EnvCache = EnvVal;
		}
		if (TempChn->EnvIdx >= TempEnv->DataLen)
		{
			TempChn->EnvIdx = 0xFF;
			return;
		}
		
		Steps --;
	}
	
	return;
}

static void ModulationUpdate(UINT8 Channel, UINT8 Steps)
{
	UINT8 ChnMode;
	FMPSG_CHN* TempChn;
	
	ChnMode = Channel >> 7;
	Channel &= 0x7F;
	if (! ChnMode)
		TempChn = &FMChn[Channel];
	else
		TempChn = &PSGChn[Channel];
	
	if (! TempChn->ModulationOn)
		return;
	
	// Wait for a certain amount of frames
	if (TempChn->ModWait)
	{
		if (Steps < TempChn->ModWait)
		{
			TempChn->ModWait -= Steps;
			return;
		}
		else
		{
			Steps -= TempChn->ModWait;
			TempChn->ModWait = 0x00;
		}
	}
	
	while(Steps)
	{
		Steps --;
		TempChn->ModStepWait --;
		if (TempChn->ModStepWait)
			continue;
		TempChn->ModStepWait = TempChn->ModData[0x01];
		
		if (TempChn->ModRemSteps)
		{
			TempChn->ModFreqDiff += TempChn->ModFreqDelta;
			TempChn->ModRemSteps --;
			TempChn->FNumFinal = TempChn->FNum + TempChn->ModFreqDiff;
			switch(ChnMode)
			{
			case 0x00:	// FM
				Write_OPN(Channel, 0xA4, (TempChn->FNumFinal & 0xFF00) >> 8);
				Write_OPN(Channel, 0xA0, (TempChn->FNumFinal & 0x00FF) >> 0);
				break;
			case 0x01:	// PSG
				if (Channel < 0x03)
					Write_PSG(Channel, 0x00, TempChn->FNumFinal);
				else
					Write_PSG(0x02, 0x00, TempChn->FNumFinal);
				break;
			}
		}
		else
		{
			TempChn->ModRemSteps = TempChn->ModData[0x03];
			TempChn->ModFreqDelta = -TempChn->ModFreqDelta;
		}
	}
	
	return;
}

static void NoteFillUpdate(UINT8 Channel, UINT8 Steps)
{
	UINT8 ChnMode;
	FMPSG_CHN* TempChn;
	
	ChnMode = Channel >> 7;
	Channel &= 0x7F;
	if (! ChnMode)
		TempChn = &FMChn[Channel];
	else
		TempChn = &PSGChn[Channel];
	
	if (! TempChn->NoteFillRem)
		return;
	
	// Wait for a certain amount of frames
	if (Steps < TempChn->NoteFillRem)
	{
		TempChn->NoteFillRem -= Steps;
		return;
	}
	
	DoNoteOff((ChnMode << 7) | Channel);
	
	return;
}

// --- MIDI Event Handlers ---
void DoShortMidiEvent(UINT8 Command, UINT8 Value1, UINT8 Value2)
{
	UINT8 Channel;
	//UINT8 TempByt;
	UINT16 TempSht;
	//UINT32 TempLng;
	INT32 PitchVal;
	MIDI_CHN* TempMid;
	
	Channel = Command & 0x0F;
	switch(Command & 0xF0)
	{
	case 0x80:	// Note Off
	case 0x90:	// Note On
		PlayNote(Command, Value1, Value2);
		break;
	case 0xA0:	// Note Aftertouch
		break;
	case 0xB0:	// Controller
		TempMid = MidChn + Channel;
		switch(Value1)
		{
		case 0x00:	// Bank Select MSB
			TempMid->BnkSel[0x00] = Value2;
			switch(GMMode)
			{
			case 0x00:	// GM Mode
				break;
			case 0x01:	// GS Mode
				break;
			case 0x02:	// XG Mode
				TempMid->IsDrum = (Value2 == 0x7F);
				break;
			case 0x03:	// MT32 Mode
				break;
			}
			break;
		case 0x01:	// Modulation
			TempMid->ModDepth = Value2;
			ApplyCtrlToChannels(Channel, 0x01);
			break;
		case 0x03:	// SMPS: Set Noise Mode
			TempMid->SpCtrl[0x00] = Value2;
			ApplyCtrlToChannels(Channel, 0x03);
			break;
		case 0x06:	// Data Entry MSB
			TempSht = (TempMid->RPNVal[0x00] << 8) | (TempMid->RPNVal[0x01] << 0);
			TempMid->RPNVal[0x02] = Value2;
			
			switch(TempSht)
			{
			case 0x0000:	// Pitch Bend Sensitivity
				TempMid->PbDepth = TempMid->RPNVal[0x02];
				if (TempMid->PbDepth > 0x20)
					TempMid->PbDepth = 0x20;	// Maximum Range is 24 semitones
				break;
			case 0x0001:	// Fine Tuning
				TempMid->TuneFine = TempMid->RPNVal[0x02] - 0x40;
				
				TempMid->TunePb = TempMid->TuneCoarse * 8192 +
									TempMid->TuneFine * 128;	// (8192 / 64)
				break;
			case 0x0002:	// Coarse Tuning
				TempMid->TuneCoarse = TempMid->RPNVal[0x02] - 0x40;
				if (TempMid->TuneCoarse < -24)
					TempMid->TuneCoarse = -24;
				else if (TempMid->TuneCoarse > +24)
					TempMid->TuneCoarse = +24;
				
				TempMid->TunePb = TempMid->TuneCoarse * 8192 +
									TempMid->TuneFine * 128;	// (8192 / 64)
				break;
			case 0xD3B2:	// D3 B2 == NRPN 53 32 ('S2')
				// En-/Disable Sonic 2 Recreation Mode (with pitched DAC)
				if (Value2 == 0x52)	// 'R'
				{
					// NRPN MSB, LSB, Data MSB = 53 32 52 -> "S2R"
					S2R_Features = true;
				}
				else
				{
					S2R_Features = false;
					OverrideDACRate(0x00, 0x00);
					SetDACVol(0x00, 0x00);
				}
				break;
			}
			break;
		case 0x07:	// Main Volume
			TempMid->ChnVol[0x00] = Value2;
			TempMid->ChnVolCache = TempMid->ChnVol[0x00] * TempMid->ChnVol[0x01];
			ApplyCtrlToChannels(Channel, 0x07);
			break;
		case 0x09:	// SMPS: Note Fill
			TempMid->SpCtrl[0x01] = Value2;
			ApplyCtrlToChannels(Channel, 0x09);
			break;
		case 0x0A:	// Pan
			TempMid->Pan = Value2;
			ApplyCtrlToChannels(Channel, 0x0A);
			break;
		case 0x0B:	// Expression
			//TempMid->ChnVol[0x01] = Value2;
			TempMid->ChnVol[0x01] = 0x7F;	// for mid2smps compatibility
			TempMid->ChnVolCache = TempMid->ChnVol[0x00] * TempMid->ChnVol[0x01];
			ApplyCtrlToChannels(Channel, 0x0B);
			break;
		case 0x10:	// General Purpose #1
		case 0x11:	// General Purpose #2
		case 0x12:	// General Purpose #3
		case 0x13:	// General Purpose #4
			TempMid->ModCfg[Value1 & 0x03] = Value2;
			break;
		case 0x14:	// SMPS VB: Set DAC Play Mode
			TempMid->SpCtrl[0x02] = Value2;
			ApplyCtrlToChannels(Channel, 0x14);
			break;
		case 0x20:	// Bank Select LSB
			TempMid->BnkSel[0x01] = Value2;
			switch(GMMode)
			{
			case 0x00:	// GM Mode
				break;
			case 0x01:	// GS Mode
				break;
			case 0x02:	// XG Mode
				break;
			case 0x03:	// MT32 Mode
				break;
			}
			break;
		case 0x21:	// Modulation LSB
			TempMid->ModDepth2 = Value2;
			ApplyCtrlToChannels(Channel, 0x21);
			break;
		case 0x26:	// Data Entry LSB
			TempMid->RPNVal[0x03] = Value2 & 0x7F;
			break;
		case 0x27:	// Main Volume LSB
			TempMid->ModDepth3 = Value2;
			ApplyCtrlToChannels(Channel, 0x21);	// refresh Modulation LSB
			break;
		case 0x29:	// SMPS VB: Set Note Fill Mode
			TempMid->SpCtrl[0x03] = Value2;
			ApplyCtrlToChannels(Channel, 0x29);
			break;
		case 0x40:	// Damper Pedal (Sustain)
			TempMid->SustainVal = Value2;
			TempMid->SustainOn &= ~0x01;
			TempMid->SustainOn |= (Value2 & 0x40) >> 6;
			ApplyCtrlToChannels(Channel, 0x40);
			break;
		case 0x41:	// Portamento On/Off
			TempMid->PortamntOn = Value2 & 0x40;
			ApplyCtrlToChannels(Channel, 0x41);
			break;
		case 0x42:	// Sostenuto
			TempMid->SustainOn &= ~0x02;
			TempMid->SustainOn |= (Value2 & 0x40) >> 5;
			ApplyCtrlToChannels(Channel, 0x42);
			break;
		case 0x4C:	// Vibrato Rate
			TempMid->VibRate = Value2;
			ApplyCtrlToChannels(Channel, 0x4C);
			break;
		case 0x5D:	// Chorus Depth
			TempMid->ChnVolB = (INT8)(Value2 | ((Value2 & 0x40) << 1));
			ApplyCtrlToChannels(Channel, 0x5D);	// set volume boost
			break;
		case 0x60:	// Data Increment
			break;
		case 0x61:	// Data Decrement
			break;
		case 0x62:	// NRPN LSB
			TempMid->RPNVal[0x01] = 0x80 | Value2;
			break;
		case 0x63:	// NRPN MSB
			TempMid->RPNVal[0x00] = 0x80 | Value2;
			break;
		case 0x64:	// RPN LSB
			TempMid->RPNVal[0x01] = 0x00 | Value2;
			break;
		case 0x65:	// RPN MSB
			TempMid->RPNVal[0x00] = 0x00 | Value2;
			break;
		case 0x78:	// All Sounds Off
			ApplyCtrlToChannels(Channel, 0x81);	// set volume to min
			break;
		case 0x79:	// Reset All Controllers
			// According to the GM Spec.:
			// Modulation = 0, Expression = 127, Volume = 100, Pan = 64
			// Sustain/Portamento/Sustenuto/Soft/Legati/Hold 2 = 0
			// reset NRPNs/RPNs (e.g. PB Range = 2)
			// reset Pitch Wheel, Chn Aftertouch, Note Aftertouch
			
			TempMid->ModDepth = 0x00;		// Ctrl 01: Modulation
			ApplyCtrlToChannels(Channel, 0x01);
			//TempMid->SpCtrl[0x00] = Value2;	// Ctrl 03: Set Noise Mode
			//ApplyCtrlToChannels(Channel, 0x03);
			TempMid->ChnVol[0x00] = 100;	// Ctrl 07/0B: Volume/Expression
			TempMid->ChnVol[0x01] = 0x7F;
			TempMid->ChnVolB = 0x00;
			TempMid->ChnVolCache = TempMid->ChnVol[0x00] * TempMid->ChnVol[0x01];
			//ApplyCtrlToChannels(Channel, 0x07);	// left out by intention
			TempMid->ModCfg[0x00] = 0x00;	// Ctrl 10-13: Modulation Settings
			TempMid->ModCfg[0x01] = 0x00;
			TempMid->ModCfg[0x02] = 0x00;
			TempMid->ModCfg[0x03] = 0x00;
			TempMid->VibRate = 0x00;		// Ctrl 76: LFO Speed
			ApplyCtrlToChannels(Channel, 0x4C);
			TempMid->ModDepth2 = 0x00;		// Ctrl 33: LFO Modulation
			TempMid->ModDepth3 = 0x00;
			ApplyCtrlToChannels(Channel, 0x21);
			TempMid->SpCtrl[0x00] = 0x7F;	// Ctrl  3: Set Noise Mode
			ApplyCtrlToChannels(Channel, 0x03);
			TempMid->SpCtrl[0x01] = 0x00;	// Ctrl  9:	Note Fill
			ApplyCtrlToChannels(Channel, 0x09);
			TempMid->SpCtrl[0x02] = 0x00;	// Ctrl 14: Set DAC Play Mode
			ApplyCtrlToChannels(Channel, 0x14);
			TempMid->SpCtrl[0x03] = 0x00;	// Ctrl 29: Set Note Fill Mode
			ApplyCtrlToChannels(Channel, 0x29);
			TempMid->SustainVal = 0x00;		// Ctrl 40: Sustain On/Off
			TempMid->SustainOn = 0x00;		// Ctrl 40 + 42: Sostenuto On/Off
			ApplyCtrlToChannels(Channel, 0x40);
			ApplyCtrlToChannels(Channel, 0x42);
			TempMid->PortamntOn = 0x00;		// Ctrl 41: Portamento On/Off
			ApplyCtrlToChannels(Channel, 0x41);
			TempMid->Pitch = 0x0000;		// Pitch Bend
			ApplyCtrlToChannels(Channel, 0xE0);
			ApplyCtrlToChannels(Channel, 0x79);	// reset channel flags
			
			S2R_Features = false;
			OverrideDACRate(0x00, 0x00);
			SetDACVol(0x00, 0x00);
			break;
		case 0x7B:	// All Notes Off
			// Stop all Notes on the current Channel
			ApplyCtrlToChannels(Channel, 0x80);	// turn note off
			break;
		}
		break;
	case 0xC0:	// Instrument Change
		MidChn[Command & 0x0F].Ins = Value1 & 0x7F;
		ApplyCtrlToChannels(Channel, 0xC0);
		break;
	case 0xD0:	// Channel Aftertouch
		break;
	case 0xE0:	// Pitch Bend
		PitchVal = (Value1 << 0) | (Value2 << 7);
		PitchVal -= 0x2000;
		PitchVal *= MidChn[Channel].PbDepth;
		MidChn[Channel].Pitch = PitchVal;
		ApplyCtrlToChannels(Channel, 0xE0);
		break;
	case 0xF0:	// SysEx / Metadata
		if (Command == 0xFF)
			StartEngine();	// TODO: Reset all data
		break;
	default:
		if (LastMidCmd & 0x80)
			DoShortMidiEvent(LastMidCmd, Command, Value1);
		break;
	}
	
	return;
}

void DoLongMidiEvent(UINT8 Command, UINT8 Value, UINT32 DataLen, UINT8* Data)
{
#if ENABLE_GSXG
	UINT8 Channel;
	UINT8 TempByt;
	UINT16 TempSht;
	//UINT32 TempLng;
	INT32 TempSLng;
	INT32 PitchVal;
	MIDI_CHN* TempMid;
#endif
	
	switch(Command)
	{
	case 0xF0:
#if ENABLE_GSXG
		while(DataLen && Data[0x00] == 0xF0)
		{
			 Data ++;	DataLen --;
		}
		
		// Device Numbers are ignored
		switch(Data[0x00])
		{
		case 0x41:	// Roland ID
			// Data[0x01] == 0x1n - Device Number n
			if (Data[0x02] == 0x42 && Data[0x03] == 0x12 && (Data[0x04] & 0x3F) == 0x00)
			{
				// Data[0x05]	Address High
				// Data[0x06]	Address Low
				switch(Data[0x05] & 0x70)
				{
				case 0x00:
					switch(Data[0x06])	
					{
					case 0x7F:	// GS On
						if (! Data[0x07])
							GMMode = 0x01;	// GS On - GS Mode
						else
							GMMode = 0x00;	// GS Off - GM Mode
						break;
					}
					break;
				case 0x10:
					if (GMMode != 0x01 && GMMode != 0x00)
						break;
					
					// Part Order
					// 10 1 2 3 4 5 6 7 8 9 11 12 13 14 15 16
					TempByt = Data[0x05] & 0x0F;
					if (TempByt == 0x00)
						Channel = 0x09;
					else if (TempByt <= 0x09)
						Channel = TempByt - 0x01;
					else //if (TempByt >= 0x0A)
						Channel = TempByt;
					TempMid = MidChn + Channel;
					switch(Data[0x06])
					{
					case 0x15:	// Drum Channel
						// Lock Ch 9 in GM Mode
						if (GMMode == 0x01 || (GMMode == 0x00 && Channel != 0x09))
							TempMid->IsDrum = (Data[0x07] > 0x00);
						break;
					}
					break;
				}
			}
			break;
		case 0x43:	// YAMAHA ID
			// Data[0x01] == 0x?n - Device Number n
			switch(Data[0x01] & 0x70)
			{
			case 0x00:	// XG Bulk Dump
				break;
			case 0x10:	// XG Parameter Change
				switch(Data[0x02])	// Model ID
				{
				case 0x4C:	// XG Model ID
					// Data[0x03]	Address High
					// Data[0x04]	Address Mid
					// Data[0x05]	Address Low
					switch(Data[0x03])	// Address High
					{
					case 0x00:	// System
						if (Data[0x04] != 0x00)
							break;
						
						if (Data[0x05] != 0x7E && GMMode != 0x02)
							break;
						switch(Data[0x05])	// Address Low
						{
						case 0x00:	// Master Tune (Address Low 00-03)
							TempSht = ((Data[0x06] & 0x0F) << 12) |
									  ((Data[0x07] & 0x0F) <<  8) |
									  ((Data[0x08] & 0x0F) <<  4) |
									  ((Data[0x09] & 0x0F) <<  0);
							MMstTuning = (TempSht - 0x0400) * 8;	// (8192 / 0x0400)
							for (Channel = 0x00; Channel < 0x10; Channel ++)
							{
								TempMid = MidChn + Channel;
								//SetControllerAll(0xE0 | Channel, 0x01, TempMid->Pitch);
							}
							break;
						case 0x04:	// Master Volume
							MMstVolume = Data[0x06];
							for (Channel = 0x00; Channel < 0x10; Channel ++)
							{
								TempMid = MidChn + Channel;
								//SetControllerAll(0xB0 | Channel, 0x87, 0x00);
							}
							break;
						case 0x06:	// Transpose
							PitchVal = Data[0x06] - 0x40;
							if (PitchVal < -0x18)
								PitchVal = -0x18;
							else if (PitchVal > +0x18)
								PitchVal = +0x18;
							break;
						case 0x7D:	// Drum Setup Reset
							TempByt = Data[0x06];	// Reset this Drum Setup
							break;
						case 0x7E:	// XG System On
							if (! Data[0x06])
								GMMode = 0x02;	// XG On - XG Mode
							else
								GMMode = 0x00;	// XG Off - GM Mode
							break;
						case 0x7F:
							if (! Data[0x06])
							{
								// Reset On
								// ...
							}
							break;
						}
						break;
					/*case 0x01:	// Information
						break;
					case 0x02:	// Effect 1
						break;
					case 0x08:	// Multi Part
						break;
					case 0x30:	// Drum Setup 1
						break;
					case 0x31:	// Drum Setup 2
						break;*/
					}
					break;
				case 0x27:	// Master Tuning Model ID
					switch(Data[0x03])
					{
					case 0x30:	// Master Tuning Sub ID
						// Data[0x06]	Master Tuning MSB
						// Data[0x07]	Master Tuning LSB
						TempSht = ((Data[0x06] & 0x7F) << 7) |
								  ((Data[0x07] & 0x7F) << 0);
						MMstTuning = (TempSht - 0x0400) * 8;	// (8192 / 0x0400)
						for (Channel = 0x00; Channel < 0x10; Channel ++)
						{
							TempMid = MidChn + Channel;
							//SetControllerAll(0xE0 | Channel, 0x01, TempMid->Pitch);
						}
						break;
					}
					break;
				}
				break;
			/*case 0x20:	// XG Dump Request
				break;
			case 0x30:	// XG Parameter Request
				break;*/
			}
			break;
		case 0x7E:	// Universal Non-Real Time Message
			// Data[0x01] == 0x??	7F - ID of target device, ?n - Device Number n
			// Data[0x02] == 0x09	Sub-ID #1 - General MIDI
			// Data[0x03] == 0x01	Sub-ID #2 - General MIDI On
			if (Data[0x02] == 0x09 && Data[0x03] == 0x01)
			{
				GMMode = 0x00;
				for (Channel = 0x00; Channel < 0x10; Channel ++)
				{
					TempMid = MidChn + Channel;
					TempMid->IsDrum = (Channel == 0x09);	// GM Drum Channel (Ch 10)
				}
			}
			break;
		case 0x7F:	// Universal Real Time Message
			// Data[0x01] == 0x??	7F - ID of target device, ?n - Device Number n
			// Data[0x02] == 0x04	Sub-ID #1 - Device Control Message
			// Data[0x03] == 0x01	Sub-ID #2 - Master Volume
			if (Data[0x02] == 0x04 && Data[0x03] == 0x01)
			{
				// Data[0x04]	Volume LSB (ignored)
				// Data[0x05]	Volume MSB
				MMstVolume = Data[0x05];
				for (Channel = 0x00; Channel < 0x10; Channel ++)
				{
					TempMid = MidChn + Channel;
					//SetControllerAll(0xB0 | Channel, 0x87, 0x00);
				}
			}
			break;
		}
#endif
		break;
	}
	
	return;
}
