#include <memory.h>
#include <malloc.h>
#include <string.h>	// for strlen()
#include <stddef.h>	// for NULL

#include "stdtype.h"
#include "2612_structs.h"
#include "2612_reader.h"

static const UINT8 SIG_GYB[2] = {26, 12};

#include "2612_checksum.c"


// Note: The checksum routine uses a memcpy to copy the bytes, so memcpy
//       functions mustn't be used to verify it. (instead memcpy is used there, too)
INLINE UINT16 ReadLE16(const UINT8* Data)
{
#if IS_BIG_ENDIAN
	return (Data[0x01] << 8) | (Data[0x00] << 0);
#else
	return *(UINT16*)Data;
#endif
}

INLINE UINT32 ReadLE32(const UINT8* Data)
{
#if IS_BIG_ENDIAN
	return	(Data[0x03] << 24) | (Data[0x02] << 16) |
			(Data[0x01] <<  8) | (Data[0x00] <<  0);
#else
	return *(UINT32*)Data;
#endif
}

INLINE void WriteLE16(UINT8* Data, UINT16 Value)
{
#if IS_BIG_ENDIAN
	Data[0x00] = (Value >>  0) & 0xFF;
	Data[0x01] = (Value >>  8) & 0xFF;
#else
	*(UINT16*)Data = Value;
#endif
	
	return;
}

INLINE void WriteLE32(UINT8* Data, UINT32 Value)
{
#if IS_BIG_ENDIAN
	Data[0x00] = (Value >>  0) & 0xFF;
	Data[0x01] = (Value >>  8) & 0xFF;
	Data[0x02] = (Value >> 16) & 0xFF;
	Data[0x03] = (Value >> 24) & 0xFF;
#else
	*(UINT32*)Data = Value;
#endif
	
	return;
}



UINT8 LoadGYBData_v2(UINT32 DataLen, UINT8* Data, GYB_FILE_V2* GYBData)
{
	UINT8 FileVer;
	UINT32 CurPos;
	UINT8 TempByt;
	UINT32 TempLng;
	UINT32 ChkSum;
	UINT8 CurBnk;
	UINT8 CurIns;
	GYB_INS_BANK_V2* TempBnk;
	GYB_INSTRUMENT_V2* TempIns;
	
	CurPos = 0x00;
	if (memcmp(&Data[CurPos + 0x00], SIG_GYB, 0x02))
		return 0x80;	// invalid file
	FileVer = Data[CurPos + 0x02];
	if (FileVer < 0x01 || FileVer > 0x02)
		return 0x81;	// unknown GYB version
	
	// Prevent noobs from messing with the file
	//memcpy(&ChkSum, &Data[DataLen - 0x04], 0x04)
	ChkSum = *((UINT32*)(Data + DataLen - 0x04));
	if (ChkSum)
		TempLng = CalcGYBChecksum(DataLen - 0x04, Data);
	else
		TempLng = 0x00;	// pros set the Checksum to zero :)
	if (ChkSum != TempLng)
		return 0x88;	// Checksum invalid
	
	GYBData->FileVer = Data[CurPos + 0x02];
	GYBData->Bank[0x00].InsCount = Data[CurPos + 0x03];
	GYBData->Bank[0x01].InsCount = Data[CurPos + 0x04];
	CurPos += 0x05;
	
	for (CurIns = 0x00; CurIns < 0x80; CurIns ++)
	{
		GYBData->Bank[0x00].InsMap[CurIns] = Data[CurPos + 0x00];
		GYBData->Bank[0x01].InsMap[CurIns] = Data[CurPos + 0x01];
		CurPos += 0x02;
	}
	
	if (FileVer == 0x02)
	{
		GYBData->LFOVal = Data[CurPos];
		CurPos ++;
	}
	
	for (CurBnk = 0x00; CurBnk < 0x02; CurBnk ++)
	{
		TempBnk = &GYBData->Bank[CurBnk];
		
		TempBnk->InsData = (GYB_INSTRUMENT_V2*)malloc(0xFF * sizeof(GYB_INSTRUMENT_V2));
		for (CurIns = 0x00; CurIns < TempBnk->InsCount; CurIns ++)
		{
			TempIns = &TempBnk->InsData[CurIns];
			switch(FileVer)
			{
			case 0x01:
				memcpy(&TempIns->Reg[0x00], &Data[CurPos], 0x1D);
				TempIns->Reg[0x1D] = 0x00;
				TempIns->Reg[0x1E] = Data[CurPos + 0x1D];
				TempIns->Reg[0x1F] = 0x00;
				CurPos += 0x1E;
				break;
			case 0x02:
				memcpy(&TempIns->Reg[0x00], &Data[CurPos], 0x20);
				CurPos += 0x20;
				break;
			}
		}
	}
	
	for (CurBnk = 0x00; CurBnk < 0x02; CurBnk ++)
	{
		TempBnk = &GYBData->Bank[CurBnk];
		
		for (CurIns = 0x00; CurIns < TempBnk->InsCount; CurIns ++)
		{
			TempIns = &TempBnk->InsData[CurIns];
			
			TempByt = Data[CurPos];
			CurPos ++;
			
			TempIns->Name = (char*)malloc(TempByt + 1);
			memcpy(TempIns->Name, &Data[CurPos], TempByt);
			TempIns->Name[TempByt] = '\0';
			
			CurPos += TempByt;
		}
	}
	
	return 0x00;
}

void FreeGYBData_v2(GYB_FILE_V2* GYBData)
{
	UINT8 CurBnk;
	UINT8 CurIns;
	GYB_INS_BANK_V2* TempBnk;
	GYB_INSTRUMENT_V2* TempIns;
	
	for (CurBnk = 0x00; CurBnk < 0x02; CurBnk ++)
	{
		TempBnk = &GYBData->Bank[CurBnk];
		if (TempBnk->InsData == NULL)
			continue;
		
		for (CurIns = 0x00; CurIns < TempBnk->InsCount; CurIns ++)
		{
			TempIns = &TempBnk->InsData[CurIns];
			
			if (TempIns->Name != NULL)
			{
				free(TempIns->Name);
				TempIns->Name = NULL;
			}
		}
		
		TempBnk->InsCount = 0x00;
		free(TempBnk->InsData);	TempBnk->InsData = NULL;
	}
	GYBData->FileVer = 0x00;
	
	return;
}

UINT8 SaveGYBData_v2(UINT32* RetDataLen, UINT8** RetData, const GYB_FILE_V2* GYBData)
{
	UINT32 DataLen;
	UINT8* Data;
	
	UINT8 FileVer;
	UINT32 CurPos;
	UINT32 TempLng;
	UINT8 CurBnk;
	UINT8 CurIns;
	const GYB_INS_BANK_V2* TempBnk;
	const GYB_INSTRUMENT_V2* TempIns;
	
	FileVer = GYBData->FileVer;
	
	// Header + Ins Count + Mappings + Ins Data + Ins Names + CheckSum
	DataLen = 0x03 + 0x02 + 2 * 0x80 + 0x01;
	for (CurBnk = 0x00; CurBnk < 0x02; CurBnk ++)
	{
		TempBnk = &GYBData->Bank[CurBnk];
		for (CurIns = 0x00; CurIns < TempBnk->InsCount; CurIns ++)
		{
			TempIns = &TempBnk->InsData[CurIns];
			
			if (FileVer == 0x01)
				DataLen += + 0x1E;
			else //if (FileVer == 0x02)
				DataLen += 0x20;
			
			TempLng = strlen(TempIns->Name);
			if (TempLng > 0xFF)
				TempLng = 0xFF;
			DataLen += 0x01 + TempLng;
		}
	}
	DataLen += 0x04;	// Checksum
	Data = (UINT8*)malloc(DataLen);
	
	CurPos = 0x00;
	memcpy(&Data[CurPos + 0x00], SIG_GYB, 0x02);	// File Signature 26 12 (dec)
	Data[CurPos + 0x02] = GYBData->FileVer;	// Version Number
	Data[CurPos + 0x03] = GYBData->Bank[0x00].InsCount;
	Data[CurPos + 0x04] = GYBData->Bank[0x01].InsCount;
	CurPos += 0x05;
	
	for (CurIns = 0x00; CurIns < 0x80; CurIns ++)
	{
		Data[CurPos + 0x00] = GYBData->Bank[0x00].InsMap[CurIns];
		Data[CurPos + 0x01] = GYBData->Bank[0x01].InsMap[CurIns];
		CurPos += 0x02;
	}
	
	if (FileVer == 0x02)
	{
		Data[CurPos] = GYBData->LFOVal;
		CurPos ++;
	}
	
	for (CurBnk = 0x00; CurBnk < 0x02; CurBnk ++)
	{
		TempBnk = &GYBData->Bank[CurBnk];
		for (CurIns = 0x00; CurIns < TempBnk->InsCount; CurIns ++)
		{
			TempIns = &TempBnk->InsData[CurIns];
			if (FileVer == 0x01)
			{
				memcpy(&Data[CurPos], &TempIns->Reg[0x00], 0x1D);
				Data[CurPos + 0x1D] = TempIns->Reg[0x1E];
				CurPos += 0x1E;
			}
			else //if (FileVer == 0x02)
			{
				memcpy(&Data[CurPos], &TempIns->Reg[0x00], 0x20);
				CurPos += 0x20;
			}
		}
	}
	
	for (CurBnk = 0x00; CurBnk < 0x02; CurBnk ++)
	{
		TempBnk = &GYBData->Bank[CurBnk];
		for (CurIns = 0x00; CurIns < TempBnk->InsCount; CurIns ++)
		{
			TempIns = &TempBnk->InsData[CurIns];
			
			TempLng = strlen(TempIns->Name);
			if (TempLng > 0xFF)
				TempLng = 0xFF;
			Data[CurPos] = (UINT8)TempLng;
			CurPos ++;
			
			if (TempLng)
			{
				memcpy(&Data[CurPos], TempIns->Name, TempLng);
				CurPos += TempLng;
			}
		}
	}
	
	TempLng = CalcGYBChecksum(CurPos, Data);
	memcpy(&Data[CurPos], &TempLng, 0x04);
	CurPos += 0x04;
	
	*RetDataLen = DataLen;
	*RetData = Data;
	
	return 0x00;
}

UINT8 LoadGYBData_v3(UINT32 DataLen, UINT8* Data, GYB_FILE_V3* GYBData)
{
	UINT8 FileVer;
	UINT32 BaseOfs;
	UINT32 InsBankOfs;
	UINT32 InsMapOfs;
	UINT32 CurPos;
	UINT32 ChkSum;
	UINT8 CurBnk;
	UINT16 CurIns;
	UINT16 CurEnt;
	UINT32 InsBaseOfs;
	UINT8 TempByt;
	UINT32 TempLng;
	GYB_INS_BANK_V3* TempBnk;
	GYB_INSTRUMENT_V3* TempIns;
	GYB_MAP_ILIST_V3* TempMLst;
	GYB_INS_CHORD_V3* TempCh;
	
	BaseOfs = 0x00;
	CurPos = BaseOfs;
	if (memcmp(&Data[CurPos + 0x00], SIG_GYB, 0x02))
		return 0x80;	// invalid file
	FileVer = Data[CurPos + 0x02];
	if (FileVer < 0x01 || FileVer > 0x03)
		return 0x81;	// unknown GYB version
	
	if (FileVer < 0x03)
	{
		GYB_FILE_V2 GYBData2;
		
		TempByt = LoadGYBData_v2(DataLen, Data, &GYBData2);
		if (TempByt)
			return TempByt;
		
		TempByt = ConvertGYBData_2to3(GYBData, &GYBData2);
		
		FreeGYBData_v2(&GYBData2);
		return TempByt;
	}
	
	TempLng = ReadLE32(&Data[CurPos + 0x04]);
	if (TempLng > DataLen)
		return 0x82;	// file incomplete
	DataLen = TempLng;
	
	// Prevent noobs from messing with the file
	//memcpy(&ChkSum, &Data[DataLen - 0x04], 0x04)
	ChkSum = *((UINT32*)(Data + DataLen - 0x04));
	if (ChkSum)
		TempLng = CalcGYBChecksum(DataLen - 0x04, Data);
	else
		TempLng = 0x00;	// pros set the Checksum to zero :)
	if (ChkSum != TempLng)
		return 0x88;	// Checksum invalid
	
	GYBData->FileVer = Data[CurPos + 0x02];
	GYBData->LFOVal = Data[CurPos + 0x03];
	InsBankOfs = ReadLE32(&Data[CurPos + 0x08]);
	InsMapOfs = ReadLE32(&Data[CurPos + 0x0C]);
	CurPos += 0x10;
	
	
	CurPos = BaseOfs + InsMapOfs;
	for (CurBnk = 0x00; CurBnk < 0x02; CurBnk ++)
	{
		for (CurIns = 0x00; CurIns < 0x80; CurIns ++)
		{
			TempMLst = &GYBData->InsMap[CurBnk].Ins[CurIns];
			
			TempMLst->EntryCount = ReadLE16(&Data[CurPos]);
			TempMLst->EntryAlloc = TempMLst->EntryCount ? TempMLst->EntryCount : 0x01;
			TempMLst->Entry = (GYB_MAP_ITEM_V3*)malloc(TempMLst->EntryAlloc * sizeof(GYB_MAP_ITEM_V3));
			CurPos += 0x02;
			
			for (CurEnt = 0x00; CurEnt < TempMLst->EntryCount; CurEnt ++)
			{
				TempMLst->Entry[CurEnt].BankMSB = Data[CurPos + 0x00];
				TempMLst->Entry[CurEnt].BankLSB = Data[CurPos + 0x01];
				TempMLst->Entry[CurEnt].FMIns = ReadLE16(&Data[CurPos + 0x02]);
				CurPos += 0x04;
			}
		}
	}
	
	
	CurPos = BaseOfs + InsBankOfs;
	for (CurBnk = 0x00; CurBnk < 0x02; CurBnk ++)
	{
		TempBnk = &GYBData->InsBank[CurBnk];
		
		TempBnk->InsCount = ReadLE16(&Data[CurPos]);
		TempBnk->InsAlloc = TempBnk->InsCount ? TempBnk->InsCount : 0x01;	// allocate at least 1
		TempBnk->InsAlloc = (TempBnk->InsAlloc + 0xFF) & ~0xFF;	// round up to 0x100
		TempBnk->InsData = (GYB_INSTRUMENT_V3*)malloc(TempBnk->InsAlloc * sizeof(GYB_INSTRUMENT_V3));
		CurPos += 0x02;
		
		InsBaseOfs = CurPos;
		for (CurIns = 0x00; CurIns < TempBnk->InsCount; CurIns ++)
		{
			TempIns = &TempBnk->InsData[CurIns];
			
			CurPos = InsBaseOfs;
			TempIns->InsSize = ReadLE16(&Data[CurPos]);
			InsBaseOfs += TempIns->InsSize;	// offset of next instrument
			CurPos += 0x02;
			
			memcpy(&TempIns->Reg[0x00], &Data[CurPos + 0x00], 0x1E);
			TempIns->Transp = Data[CurPos + 0x1E];
			TempIns->AddData = Data[CurPos + 0x1F];
			CurPos += 0x20;
			
			
			TempCh = &TempIns->ChordNotes;
			if (TempIns->AddData & 0x01)
			{
				TempCh->NoteCount = Data[CurPos];
				CurPos ++;
				
				TempCh->NoteAlloc = TempCh->NoteCount ? TempCh->NoteCount : 0x01;
				TempCh->Notes = (INT8*)malloc(TempCh->NoteAlloc * sizeof(INT8));
				memcpy(TempCh->Notes, &Data[CurPos], TempCh->NoteCount);
				CurPos += TempCh->NoteCount;
			}
			else
			{
				TempCh->NoteAlloc = 0x00;
				TempCh->NoteCount = 0x00;
				TempCh->Notes = NULL;
			}
			
			
			TempByt = Data[CurPos];
			CurPos ++;
			
			TempIns->Name = (char*)malloc(TempByt + 1);
			memcpy(TempIns->Name, &Data[CurPos], TempByt);
			TempIns->Name[TempByt] = '\0';
			CurPos += TempByt;
		}
	}
	
	return 0x00;
}

void FreeGYBData_v3(GYB_FILE_V3* GYBData)
{
	UINT8 CurBnk;
	UINT16 CurIns;
	GYB_INS_BANK_V3* TempBnk;
	GYB_INSTRUMENT_V3* TempIns;
	GYB_MAP_ILIST_V3* TempMLst;
	
	for (CurBnk = 0x00; CurBnk < 0x02; CurBnk ++)
	{
		for (CurIns = 0x00; CurIns < 0x80; CurIns ++)
		{
			TempMLst = &GYBData->InsMap[CurBnk].Ins[CurIns];
			TempMLst->EntryCount = 0x00;
			TempMLst->EntryAlloc = 0x00;
			if (TempMLst->Entry != NULL)
			{
				free(TempMLst->Entry);
				TempMLst->Entry = NULL;
			}
		}
		
		TempBnk = &GYBData->InsBank[CurBnk];
		if (TempBnk->InsData == NULL)
			continue;
		
		for (CurIns = 0x00; CurIns < TempBnk->InsCount; CurIns ++)
		{
			TempIns = &TempBnk->InsData[CurIns];
			
			if (TempIns->ChordNotes.Notes != NULL)
			{
				free(TempIns->ChordNotes.Notes);
				TempIns->ChordNotes.Notes = NULL;
			}
			if (TempIns->Name != NULL)
			{
				free(TempIns->Name);
				TempIns->Name = NULL;
			}
		}
		
		TempBnk->InsCount = 0x00;
		free(TempBnk->InsData);	TempBnk->InsData = NULL;
	}
	GYBData->FileVer = 0x00;
	
	return;
}

UINT8 ConvertGYBData_2to3(GYB_FILE_V3* DstGyb3, const GYB_FILE_V2* SrcGyb2)
{
	UINT8 CurBnk;
	UINT16 CurIns;
	const GYB_INS_BANK_V2* TempSBnk;
	const GYB_INSTRUMENT_V2* TempSIns;
	GYB_INS_BANK_V3* TempBnk;
	GYB_INSTRUMENT_V3* TempIns;
	GYB_MAP_ILIST_V3* TempMLst;
	
	if (SrcGyb2->FileVer < 0x01 || SrcGyb2->FileVer > 0x02)
		return 0x81;
	
	DstGyb3->FileVer = 0x03;
	DstGyb3->LFOVal = SrcGyb2->LFOVal;
	
	for (CurBnk = 0x00; CurBnk < 0x02; CurBnk ++)
	{
		TempSBnk = &SrcGyb2->Bank[CurBnk];
		for (CurIns = 0x00; CurIns < 0x80; CurIns ++)
		{
			TempMLst = &DstGyb3->InsMap[CurBnk].Ins[CurIns];
			TempMLst->EntryAlloc = 0x01;
			TempMLst->Entry = (GYB_MAP_ITEM_V3*)malloc(TempMLst->EntryAlloc * sizeof(GYB_MAP_ITEM_V3));
			
			TempMLst->Entry[0x00].BankMSB = 0xFF;	// all
			TempMLst->Entry[0x00].BankLSB = 0xFF;	// all
			if (TempSBnk->InsMap[CurIns] == 0xFF)
			{
				TempMLst->EntryCount = 0x00;
				TempMLst->Entry[0x00].FMIns = 0xFFFF;
			}
			else
			{
				TempMLst->EntryCount = 0x01;
				TempMLst->Entry[0x00].FMIns = TempSBnk->InsMap[CurIns];
				// Bank 00 - Melody (0000..7FFF)
				// Bank 01 - Drums  (8000..FFFE)
				TempMLst->Entry[0x00].FMIns |= (CurBnk << 15);
			}
		}
		
		TempBnk = &DstGyb3->InsBank[CurBnk];
		TempBnk->InsCount = TempSBnk->InsCount;
		TempBnk->InsAlloc = TempBnk->InsCount ? TempBnk->InsCount : 0x01;	// allocate at least 1
		TempBnk->InsAlloc = (TempBnk->InsAlloc + 0xFF) & ~0xFF;	// round up to 0x100
		TempBnk->InsData = (GYB_INSTRUMENT_V3*)malloc(TempBnk->InsAlloc * sizeof(GYB_INSTRUMENT_V3));
		
		for (CurIns = 0x00; CurIns < TempBnk->InsCount; CurIns ++)
		{
			TempSIns = &TempSBnk->InsData[CurIns];
			TempIns = &TempBnk->InsData[CurIns];
			
			memcpy(&TempIns->Reg[0x00], &TempSIns->Reg[0x00], 0x1E);
			TempIns->Transp = TempSIns->Reg[0x1E];
			TempIns->AddData = 0x00;
			
			TempIns->ChordNotes.NoteCount = 0x00;
			TempIns->ChordNotes.NoteAlloc = 0x00;
			TempIns->ChordNotes.Notes = NULL;
			
			TempIns->Name = _strdup(TempSIns->Name);
		}
	}
	
	return 0x00;
}

void FixGYBDataStruct_v3(GYB_FILE_V3* GYBData)
{
	// This routine sets and fixes all "length" values and bit masks.
	UINT8 CurBnk;
	UINT16 CurIns;
	UINT16 InsSize;
	UINT32 TempLng;
	GYB_INS_BANK_V3* TempBnk;
	GYB_INSTRUMENT_V3* TempIns;
	
	for (CurBnk = 0x00; CurBnk < 0x02; CurBnk ++)
	{
		TempBnk = &GYBData->InsBank[CurBnk];
		for (CurIns = 0x00; CurIns < TempBnk->InsCount; CurIns ++)
		{
			TempIns = &TempBnk->InsData[CurIns];
			
			InsSize = 0x02 + 0x20;
			
			TempIns->AddData = 0x00;
			if (TempIns->ChordNotes.NoteCount)
			{
				TempIns->AddData |= 0x01;
				InsSize += 0x01 + TempIns->ChordNotes.NoteCount * 0x01;
			}
			
			TempLng = strlen(TempIns->Name);
			if (TempLng > 0xFF)
				TempLng = 0xFF;
			InsSize += 0x01 + (UINT16)TempLng;
			
			TempIns->InsSize = InsSize;
		}
	}
	
	return;
}

UINT8 SaveGYBData_v3(UINT32* RetDataLen, UINT8** RetData, const GYB_FILE_V3* GYBData)
{
	UINT32 DataLen;
	UINT8* Data;
	UINT32 InsBankOfs;
	UINT32 InsMapOfs;
	UINT32 CurPos;
	UINT8 CurBnk;
	UINT16 CurIns;
	UINT16 CurEnt;
	UINT32 InsBaseOfs;
	UINT32 TempLng;
	const GYB_INS_BANK_V3* TempBnk;
	const GYB_INSTRUMENT_V3* TempIns;
	const GYB_MAP_ILIST_V3* TempMLst;
	const GYB_INS_CHORD_V3* TempCh;
	
	// Fix and recalculate all values
	FixGYBDataStruct_v3((GYB_FILE_V3*)GYBData);
	
	CurPos = 0x10;
	
	CurPos = (CurPos + 0x0F) & ~0x0F;	// round up to 0x10 bytes
	InsMapOfs = CurPos;
	for (CurBnk = 0x00; CurBnk < 0x02; CurBnk ++)
	{
		for (CurIns = 0x00; CurIns < 0x80; CurIns ++)
		{
			TempMLst = &GYBData->InsMap[CurBnk].Ins[CurIns];
			CurPos += 0x02 + TempMLst->EntryCount * 0x04;
		}
	}
	
	CurPos = (CurPos + 0x0F) & ~0x0F;	// round up to 0x10 bytes
	InsBankOfs = CurPos;
	for (CurBnk = 0x00; CurBnk < 0x02; CurBnk ++)
	{
		TempBnk = &GYBData->InsBank[CurBnk];
		for (CurIns = 0x00; CurIns < TempBnk->InsCount; CurIns ++)
			CurPos += TempBnk->InsData[CurIns].InsSize;
	}
	
	CurPos = ((CurPos + 0x03) & ~0x0F) + 0x0C;	// round up, make last digit 0x0C
	DataLen = CurPos + 0x04;
	Data = (UINT8*)malloc(DataLen);
	
	CurPos = 0x00;
	memcpy(&Data[CurPos + 0x00], SIG_GYB, 0x02);	// File Signature 26 12 (dec)
	Data[CurPos + 0x02] = GYBData->FileVer;	// Version Number
	Data[CurPos + 0x03] = GYBData->LFOVal;
	WriteLE32(&Data[CurPos + 0x04], DataLen);
	WriteLE32(&Data[CurPos + 0x08], InsBankOfs);
	WriteLE32(&Data[CurPos + 0x0C], InsMapOfs);
	CurPos += 0x10;
	
	
	CurPos = InsMapOfs;
	for (CurBnk = 0x00; CurBnk < 0x02; CurBnk ++)
	{
		for (CurIns = 0x00; CurIns < 0x80; CurIns ++)
		{
			TempMLst = &GYBData->InsMap[CurBnk].Ins[CurIns];
			WriteLE16(&Data[CurPos], TempMLst->EntryCount);
			CurPos += 0x02;
			
			for (CurEnt = 0x00; CurEnt < TempMLst->EntryCount; CurEnt ++)
			{
				Data[CurPos + 0x00] = TempMLst->Entry[CurEnt].BankMSB;
				Data[CurPos + 0x01] = TempMLst->Entry[CurEnt].BankLSB;
				WriteLE16(&Data[CurPos + 0x02], TempMLst->Entry[CurEnt].FMIns);
				CurPos += 0x04;
			}
		}
	}
	
	
	CurPos = InsBankOfs;
	for (CurBnk = 0x00; CurBnk < 0x02; CurBnk ++)
	{
		TempBnk = &GYBData->InsBank[CurBnk];
		WriteLE16(&Data[CurPos], TempBnk->InsCount);
		CurPos += 0x02;
		
		InsBaseOfs = CurPos;
		for (CurIns = 0x00; CurIns < TempBnk->InsCount; CurIns ++)
		{
			TempIns = &TempBnk->InsData[CurIns];
			
			CurPos = InsBaseOfs;
			WriteLE16(&Data[CurPos], TempIns->InsSize);
			InsBaseOfs += TempIns->InsSize;	// offset of next instrument
			CurPos += 0x02;
			
			memcpy(&Data[CurPos + 0x00], &TempIns->Reg[0x00], 0x1E);
			Data[CurPos + 0x1E] = TempIns->Transp;
			Data[CurPos + 0x1F] = TempIns->AddData;
			CurPos += 0x20;
			
			if (TempIns->AddData & 0x01)
			{
				TempCh = &TempIns->ChordNotes;
				
				Data[CurPos] = TempCh->NoteCount;
				CurPos ++;
				
				memcpy(&Data[CurPos], TempCh->Notes, TempCh->NoteCount);
				CurPos += TempCh->NoteCount;
			}
			
			
			TempLng = strlen(TempIns->Name);
			if (TempLng > 0xFF)
				TempLng = 0xFF;
			Data[CurPos] = (UINT8)TempLng;
			CurPos ++;
			if (TempLng)
			{
				memcpy(&Data[CurPos], TempIns->Name, TempLng);
				CurPos += TempLng;
			}
		}
	}
	
	CurPos = DataLen - 0x04;
	TempLng = CalcGYBChecksum(CurPos, Data);
	memcpy(&Data[CurPos], &TempLng, 0x04);
	CurPos += 0x04;
	
	*RetDataLen = DataLen;
	*RetData = Data;
	
	return 0x00;
}
