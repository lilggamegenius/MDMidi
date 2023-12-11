#include <malloc.h>
#include <string.h>	// for strlen()

#include "SoundEngine/stdtype.h"
#include "2612_structs.h"
#include "2612_reader.h"

#include "SoundEngine/chips/mamedef.h"

static const UINT8 SIG_GYB[2] = {26, 12};

// Ported to C by Valley Bell within 5 minutes for NeoLogiX

static UINT32 CalcGYBChecksum(UINT32 FileSize, const UINT8* FileData){
	UINT8 ChkArr[0x04];

	// nineko really made a crazy checksum formula here
	UINT8 ChkByt2 = 0;
	UINT32 TempLng = FileSize;
	while (TempLng) {
		ChkByt2 += TempLng % 10;
		TempLng /= 10;
	}
	ChkByt2 *= 3;

	UINT8 ChkByt3 = 0;
	UINT32 QSum = 0;
	const UINT8 BytMask = 1 << (FileSize & 0x07);
	for (UINT32 CurPos = 0x00; CurPos < FileSize; CurPos++) {
		if ((FileData[CurPos] & BytMask) == BytMask)
			ChkByt3++;
		QSum += FileData[CurPos];
	}
	const UINT16 InsCount = FileData[0x03] + FileData[0x04]; // Melody + Drum Instruments
	const UINT8 ChkByt1 = (FileSize + QSum) % (InsCount + 1);
	QSum %= 999;

	ChkArr[0x00] = ChkByt2 + QSum % 37;
	ChkArr[0x01] = ChkByt1;
	ChkArr[0x02] = ChkByt3;
	// This formula is ... just ... crazy
	// [ (q*x1*x2*x3) + (q*x1*x2) + (q*x2*x3) + (q*x1*x3) + x1+x2+x3+84 ] % 199
	ChkArr[0x03] = (QSum * (ChkByt1 + 1) * (ChkByt2 + 2) * (ChkByt3 + 3) +
					QSum * (ChkByt1 + 1) * (ChkByt2 + 2) +
					QSum * (ChkByt2 + 2) * (ChkByt3 + 3) +
					QSum * (ChkByt1 + 1) * (ChkByt3 + 3) +
					ChkByt1 + ChkByt2 + ChkByt3 + 84) % 199;

	return *(UINT32 *)ChkArr;
}

// Note: The checksum routine uses a memcpy to copy the bytes, so memcpy
//       functions mustn't be used to verify it. (instead memcpy is used there, too)
INLINE UINT16 ReadLE16(const UINT8* Data){
#if IS_BIG_ENDIAN
	return (Data[0x01] << 8) | (Data[0x00] << 0);
#else
	return *(UINT16 *)Data;
#endif
}

INLINE UINT32 ReadLE32(const UINT8* Data){
#if IS_BIG_ENDIAN
	return	(Data[0x03] << 24) | (Data[0x02] << 16) |
			(Data[0x01] <<  8) | (Data[0x00] <<  0);
#else
	return *(UINT32 *)Data;
#endif
}

INLINE void WriteLE16(UINT8* Data, UINT16 Value){
#if IS_BIG_ENDIAN
	Data[0x00] = (Value >>  0) & 0xFF;
	Data[0x01] = (Value >>  8) & 0xFF;
#else
	*(UINT16 *)Data = Value;
#endif
}

INLINE void WriteLE32(UINT8* Data, UINT32 Value){
#if IS_BIG_ENDIAN
	Data[0x00] = (Value >>  0) & 0xFF;
	Data[0x01] = (Value >>  8) & 0xFF;
	Data[0x02] = (Value >> 16) & 0xFF;
	Data[0x03] = (Value >> 24) & 0xFF;
#else
	*(UINT32 *)Data = Value;
#endif
}

UINT8 LoadGYBData_v2(UINT32 DataLen, UINT8* Data, GYB_FILE_V2* GYBData){
	UINT32 CurPos = 0x00;
	if (memcmp(&Data[CurPos + 0x00], SIG_GYB, 0x02) != 0) {
		return 0x80; // invalid file
	}
	const UINT8 FileVer = Data[CurPos + 0x02];
	if (FileVer < 0x01 || FileVer > 0x02) {
		return 0x81; // unknown GYB version
	}

	// Prevent noobs from messing with the file
	//memcpy(&ChkSum, &Data[DataLen - 0x04], 0x04)
	{
		const UINT32 ChkSum = *(UINT32 *)(Data + DataLen - 0x04);
		UINT32 TempLng;
		if (ChkSum)
			TempLng = CalcGYBChecksum(DataLen - 0x04, Data);
		else
			TempLng = 0x00; // pros set the Checksum to zero :)
		if (ChkSum != TempLng)
			return 0x88; // Checksum invalid
	}

	GYBData->FileVer = Data[CurPos + 0x02];
	GYBData->Bank[0x00].InsCount = Data[CurPos + 0x03];
	GYBData->Bank[0x01].InsCount = Data[CurPos + 0x04];
	CurPos += 0x05;

	for (UINT8 CurIns = 0x00; CurIns < 0x80; CurIns++) {
		GYBData->Bank[0x00].InsMap[CurIns] = Data[CurPos + 0x00];
		GYBData->Bank[0x01].InsMap[CurIns] = Data[CurPos + 0x01];
		CurPos += 0x02;
	}

	if (FileVer == 0x02) {
		GYBData->LFOVal = Data[CurPos];
		CurPos++;
	}

	for (UINT8 CurBnk = 0x00; CurBnk < 0x02; CurBnk++) {
		GYB_INS_BANK_V2* TempBnk = &GYBData->Bank[CurBnk];

		TempBnk->InsData = (GYB_INSTRUMENT_V2 *)malloc(0xFF * sizeof(GYB_INSTRUMENT_V2));
		for (UINT8 CurIns = 0x00; CurIns < TempBnk->InsCount; CurIns++) {
			GYB_INSTRUMENT_V2* TempIns = &TempBnk->InsData[CurIns];
			switch (FileVer) {
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
				default: break;
			}
		}
	}

	for (UINT8 CurBnk = 0x00; CurBnk < 0x02; CurBnk++) {
		const GYB_INS_BANK_V2* TempBnk = &GYBData->Bank[CurBnk];

		for (UINT8 CurIns = 0x00; CurIns < TempBnk->InsCount; CurIns++) {
			GYB_INSTRUMENT_V2* TempIns = &TempBnk->InsData[CurIns];

			const UINT8 TempByt = Data[CurPos];
			CurPos++;

			TempIns->Name = (char *)malloc(TempByt + 1);
			memcpy(TempIns->Name, &Data[CurPos], TempByt);
			TempIns->Name[TempByt] = '\0';

			CurPos += TempByt;
		}
	}

	return 0x00;
}

void FreeGYBData_v2(GYB_FILE_V2* GYBData){
	for (UINT8 CurBnk = 0x00; CurBnk < 0x02; CurBnk++) {
		GYB_INS_BANK_V2* TempBnk = &GYBData->Bank[CurBnk];
		if (TempBnk->InsData == NULL)
			continue;

		for (UINT8 CurIns = 0x00; CurIns < TempBnk->InsCount; CurIns++) {
			GYB_INSTRUMENT_V2* TempIns = &TempBnk->InsData[CurIns];

			if (TempIns->Name != NULL) {
				free(TempIns->Name);
				TempIns->Name = NULL;
			}
		}

		TempBnk->InsCount = 0x00;
		free(TempBnk->InsData);
		TempBnk->InsData = NULL;
	}
	GYBData->FileVer = 0x00;
}

UINT8 SaveGYBData_v2(UINT32* RetDataLen, UINT8** RetData, const GYB_FILE_V2* GYBData){
	const UINT8 FileVer = GYBData->FileVer;

	// Header + Ins Count + Mappings + Ins Data + Ins Names + CheckSum
	size_t DataLen = 0x03 + 0x02 + 2 * 0x80 + 0x01;
	for (UINT8 CurBnk = 0x00; CurBnk < 0x02; CurBnk++) {
		const GYB_INS_BANK_V2* TempBnk = &GYBData->Bank[CurBnk];
		for (UINT8 CurIns = 0x00; CurIns < TempBnk->InsCount; CurIns++) {
			const GYB_INSTRUMENT_V2* TempIns = &TempBnk->InsData[CurIns];

			if (FileVer == 0x01)
				DataLen += +0x1E;
			else //if (FileVer == 0x02)
				DataLen += 0x20;

			size_t TempLng = strlen(TempIns->Name);
			if (TempLng > 0xFF)
				TempLng = 0xFF;
			DataLen += 0x01 + TempLng;
		}
	}
	DataLen += 0x04; // Checksum
	UINT8* Data = malloc(DataLen);

	size_t CurPos = 0x00;
	memcpy(&Data[CurPos + 0x00], SIG_GYB, 0x02); // File Signature 26 12 (dec)
	Data[CurPos + 0x02] = GYBData->FileVer; // Version Number
	Data[CurPos + 0x03] = GYBData->Bank[0x00].InsCount;
	Data[CurPos + 0x04] = GYBData->Bank[0x01].InsCount;
	CurPos += 0x05;

	for (UINT8 CurIns = 0x00; CurIns < 0x80; CurIns++) {
		Data[CurPos + 0x00] = GYBData->Bank[0x00].InsMap[CurIns];
		Data[CurPos + 0x01] = GYBData->Bank[0x01].InsMap[CurIns];
		CurPos += 0x02;
	}

	if (FileVer == 0x02) {
		Data[CurPos] = GYBData->LFOVal;
		CurPos++;
	}

	for (UINT8 CurBnk = 0x00; CurBnk < 0x02; CurBnk++) {
		const GYB_INS_BANK_V2* TempBnk = &GYBData->Bank[CurBnk];
		for (UINT8 CurIns = 0x00; CurIns < TempBnk->InsCount; CurIns++) {
			const GYB_INSTRUMENT_V2* TempIns = &TempBnk->InsData[CurIns];
			if (FileVer == 0x01) {
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

	for (UINT8 CurBnk = 0x00; CurBnk < 0x02; CurBnk++) {
		const GYB_INS_BANK_V2* TempBnk = &GYBData->Bank[CurBnk];
		for (UINT8 CurIns = 0x00; CurIns < TempBnk->InsCount; CurIns++) {
			const GYB_INSTRUMENT_V2* TempIns = &TempBnk->InsData[CurIns];

			size_t TempLng = strlen(TempIns->Name);
			if (TempLng > 0xFF)
				TempLng = 0xFF;
			Data[CurPos] = (UINT8)TempLng;
			CurPos++;

			if (TempLng) {
				memcpy(&Data[CurPos], TempIns->Name, TempLng);
				CurPos += TempLng;
			}
		}
	}

	const UINT32 TempLng = CalcGYBChecksum(CurPos, Data);
	memcpy(&Data[CurPos], &TempLng, 0x04);
	CurPos += 0x04;

	*RetDataLen = DataLen;
	*RetData = Data;

	return 0x00;
}

UINT8 LoadGYBData_v3(UINT32 DataLen, UINT8* Data, GYB_FILE_V3* GYBData){
	UINT8 CurBnk;
	UINT16 CurIns;
	UINT8 TempByt;

	const UINT32 BaseOfs = 0x00;
	UINT32 CurPos = BaseOfs;
	if (memcmp(&Data[CurPos + 0x00], SIG_GYB, 0x02) != 0)
		return 0x80; // invalid file
	const UINT8 FileVer = Data[CurPos + 0x02];
	if (FileVer < 0x01 || FileVer > 0x03)
		return 0x81; // unknown GYB version

	if (FileVer < 0x03) {
		GYB_FILE_V2 GYBData2;

		TempByt = LoadGYBData_v2(DataLen, Data, &GYBData2);
		if (TempByt)
			return TempByt;

		TempByt = ConvertGYBData_2to3(GYBData, &GYBData2);

		FreeGYBData_v2(&GYBData2);
		return TempByt;
	}

	UINT32 TempLng = ReadLE32(&Data[CurPos + 0x04]);
	if (TempLng > DataLen)
		return 0x82; // file incomplete
	DataLen = TempLng;

	// Prevent noobs from messing with the file
	//memcpy(&ChkSum, &Data[DataLen - 0x04], 0x04)
	const UINT32 ChkSum = *(UINT32 *)(Data + DataLen - 0x04);
	if (ChkSum)
		TempLng = CalcGYBChecksum(DataLen - 0x04, Data);
	else
		TempLng = 0x00; // pros set the Checksum to zero :)
	if (ChkSum != TempLng)
		return 0x88; // Checksum invalid

	GYBData->FileVer = Data[CurPos + 0x02];
	GYBData->LFOVal = Data[CurPos + 0x03];
	const UINT32 InsBankOfs = ReadLE32(&Data[CurPos + 0x08]);
	const UINT32 InsMapOfs = ReadLE32(&Data[CurPos + 0x0C]);
	CurPos += 0x10;

	CurPos = BaseOfs + InsMapOfs;
	for (CurBnk = 0x00; CurBnk < 0x02; CurBnk++) {
		for (CurIns = 0x00; CurIns < 0x80; CurIns++) {
			GYB_MAP_ILIST_V3* TempMLst = &GYBData->InsMap[CurBnk].Ins[CurIns];

			TempMLst->EntryCount = ReadLE16(&Data[CurPos]);
			TempMLst->EntryAlloc = TempMLst->EntryCount ? TempMLst->EntryCount : 0x01;
			TempMLst->Entry = (GYB_MAP_ITEM_V3 *)malloc(TempMLst->EntryAlloc * sizeof(GYB_MAP_ITEM_V3));
			CurPos += 0x02;

			for (UINT16 CurEnt = 0x00; CurEnt < TempMLst->EntryCount; CurEnt++) {
				TempMLst->Entry[CurEnt].BankMSB = Data[CurPos + 0x00];
				TempMLst->Entry[CurEnt].BankLSB = Data[CurPos + 0x01];
				TempMLst->Entry[CurEnt].FMIns = ReadLE16(&Data[CurPos + 0x02]);
				CurPos += 0x04;
			}
		}
	}

	CurPos = BaseOfs + InsBankOfs;
	for (CurBnk = 0x00; CurBnk < 0x02; CurBnk++) {
		GYB_INS_BANK_V3* TempBnk = &GYBData->InsBank[CurBnk];

		TempBnk->InsCount = ReadLE16(&Data[CurPos]);
		TempBnk->InsAlloc = TempBnk->InsCount ? TempBnk->InsCount : 0x01; // allocate at least 1
		TempBnk->InsAlloc = TempBnk->InsAlloc + 0xFF & ~0xFF; // round up to 0x100
		TempBnk->InsData = (GYB_INSTRUMENT_V3 *)malloc(TempBnk->InsAlloc * sizeof(GYB_INSTRUMENT_V3));
		CurPos += 0x02;

		UINT32 InsBaseOfs = CurPos;
		for (CurIns = 0x00; CurIns < TempBnk->InsCount; CurIns++) {
			GYB_INSTRUMENT_V3* TempIns = &TempBnk->InsData[CurIns];

			CurPos = InsBaseOfs;
			TempIns->InsSize = ReadLE16(&Data[CurPos]);
			InsBaseOfs += TempIns->InsSize; // offset of next instrument
			CurPos += 0x02;

			memcpy(&TempIns->Reg[0x00], &Data[CurPos + 0x00], 0x1E);
			TempIns->Transp = Data[CurPos + 0x1E];
			TempIns->AddData = Data[CurPos + 0x1F];
			CurPos += 0x20;

			GYB_INS_CHORD_V3* TempCh = &TempIns->ChordNotes;
			if (TempIns->AddData & 0x01) {
				TempCh->NoteCount = Data[CurPos];
				CurPos++;

				TempCh->NoteAlloc = TempCh->NoteCount ? TempCh->NoteCount : 0x01;
				TempCh->Notes = (INT8 *)malloc(TempCh->NoteAlloc * sizeof(INT8));
				memcpy(TempCh->Notes, &Data[CurPos], TempCh->NoteCount);
				CurPos += TempCh->NoteCount;
			}
			else {
				TempCh->NoteAlloc = 0x00;
				TempCh->NoteCount = 0x00;
				TempCh->Notes = NULL;
			}

			TempByt = Data[CurPos];
			CurPos++;

			TempIns->Name = (char *)malloc(TempByt + 1);
			memcpy(TempIns->Name, &Data[CurPos], TempByt);
			TempIns->Name[TempByt] = '\0';
			CurPos += TempByt;
		}
	}

	return 0x00;
}

void FreeGYBData_v3(GYB_FILE_V3* GYBData){
	UINT16 CurIns;

	for (UINT8 CurBnk = 0x00; CurBnk < 0x02; CurBnk++) {
		for (CurIns = 0x00; CurIns < 0x80; CurIns++) {
			GYB_MAP_ILIST_V3* TempMLst = &GYBData->InsMap[CurBnk].Ins[CurIns];
			TempMLst->EntryCount = 0x00;
			TempMLst->EntryAlloc = 0x00;
			if (TempMLst->Entry != NULL) {
				free(TempMLst->Entry);
				TempMLst->Entry = NULL;
			}
		}

		GYB_INS_BANK_V3* TempBnk = &GYBData->InsBank[CurBnk];
		if (TempBnk->InsData == NULL)
			continue;

		for (CurIns = 0x00; CurIns < TempBnk->InsCount; CurIns++) {
			GYB_INSTRUMENT_V3* TempIns = &TempBnk->InsData[CurIns];

			if (TempIns->ChordNotes.Notes != NULL) {
				free(TempIns->ChordNotes.Notes);
				TempIns->ChordNotes.Notes = NULL;
			}
			if (TempIns->Name != NULL) {
				free(TempIns->Name);
				TempIns->Name = NULL;
			}
		}

		TempBnk->InsCount = 0x00;
		free(TempBnk->InsData);
		TempBnk->InsData = NULL;
	}
	GYBData->FileVer = 0x00;
}

UINT8 ConvertGYBData_2to3(GYB_FILE_V3* DstGyb3, const GYB_FILE_V2* SrcGyb2){
	UINT16 CurIns;

	if (SrcGyb2->FileVer < 0x01 || SrcGyb2->FileVer > 0x02)
		return 0x81;

	DstGyb3->FileVer = 0x03;
	DstGyb3->LFOVal = SrcGyb2->LFOVal;

	for (UINT8 CurBnk = 0x00; CurBnk < 0x02; CurBnk++) {
		const GYB_INS_BANK_V2* TempSBnk = &SrcGyb2->Bank[CurBnk];
		for (CurIns = 0x00; CurIns < 0x80; CurIns++) {
			GYB_MAP_ILIST_V3* TempMLst = &DstGyb3->InsMap[CurBnk].Ins[CurIns];
			TempMLst->EntryAlloc = 0x01;
			TempMLst->Entry = (GYB_MAP_ITEM_V3 *)malloc(TempMLst->EntryAlloc * sizeof(GYB_MAP_ITEM_V3));

			TempMLst->Entry[0x00].BankMSB = 0xFF; // all
			TempMLst->Entry[0x00].BankLSB = 0xFF; // all
			if (TempSBnk->InsMap[CurIns] == 0xFF) {
				TempMLst->EntryCount = 0x00;
				TempMLst->Entry[0x00].FMIns = 0xFFFF;
			}
			else {
				TempMLst->EntryCount = 0x01;
				TempMLst->Entry[0x00].FMIns = TempSBnk->InsMap[CurIns];
				// Bank 00 - Melody (0000..7FFF)
				// Bank 01 - Drums  (8000..FFFE)
				TempMLst->Entry[0x00].FMIns |= CurBnk << 15;
			}
		}

		GYB_INS_BANK_V3* TempBnk = &DstGyb3->InsBank[CurBnk];
		TempBnk->InsCount = TempSBnk->InsCount;
		TempBnk->InsAlloc = TempBnk->InsCount ? TempBnk->InsCount : 0x01; // allocate at least 1
		TempBnk->InsAlloc = TempBnk->InsAlloc + 0xFF & ~0xFF; // round up to 0x100
		TempBnk->InsData = (GYB_INSTRUMENT_V3 *)malloc(TempBnk->InsAlloc * sizeof(GYB_INSTRUMENT_V3));

		for (CurIns = 0x00; CurIns < TempBnk->InsCount; CurIns++) {
			const GYB_INSTRUMENT_V2* TempSIns = &TempSBnk->InsData[CurIns];
			GYB_INSTRUMENT_V3* TempIns = &TempBnk->InsData[CurIns];

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

void FixGYBDataStruct_v3(GYB_FILE_V3* GYBData){
	// This routine sets and fixes all "length" values and bit masks.

	for (UINT8 CurBnk = 0x00; CurBnk < 0x02; CurBnk++) {
		const GYB_INS_BANK_V3* TempBnk = &GYBData->InsBank[CurBnk];
		for (UINT16 CurIns = 0x00; CurIns < TempBnk->InsCount; CurIns++) {
			GYB_INSTRUMENT_V3* TempIns = &TempBnk->InsData[CurIns];

			UINT16 InsSize = 0x02 + 0x20;

			TempIns->AddData = 0x00;
			if (TempIns->ChordNotes.NoteCount) {
				TempIns->AddData |= 0x01;
				InsSize += 0x01 + TempIns->ChordNotes.NoteCount * 0x01;
			}

			size_t TempLng = strlen(TempIns->Name);
			if (TempLng > 0xFF)
				TempLng = 0xFF;
			InsSize += 0x01 + (UINT16)TempLng;

			TempIns->InsSize = InsSize;
		}
	}
}

UINT8 SaveGYBData_v3(UINT32* RetDataLen, UINT8** RetData, const GYB_FILE_V3* GYBData){

	// Fix and recalculate all values
	FixGYBDataStruct_v3((GYB_FILE_V3 *)GYBData);

	size_t CurPos = 0x10;

	CurPos = CurPos + 0x0F & ~0x0F; // round up to 0x10 bytes
	const UINT32 InsMapOfs = CurPos;
	for (UINT8 CurBnk = 0x00; CurBnk < 0x02; CurBnk++) {
		for (UINT16 CurIns = 0x00; CurIns < 0x80; CurIns++) {
			const GYB_MAP_ILIST_V3* TempMLst = &GYBData->InsMap[CurBnk].Ins[CurIns];
			CurPos += 0x02 + TempMLst->EntryCount * 0x04;
		}
	}

	CurPos = CurPos + 0x0F & ~0x0F; // round up to 0x10 bytes
	const UINT32 InsBankOfs = CurPos;
	for (UINT8 CurBnk = 0x00; CurBnk < 0x02; CurBnk++) {
		const GYB_INS_BANK_V3* TempBnk = &GYBData->InsBank[CurBnk];
		for (UINT16 CurIns = 0x00; CurIns < TempBnk->InsCount; CurIns++)
			CurPos += TempBnk->InsData[CurIns].InsSize;
	}

	CurPos = (CurPos + 0x03 & ~0x0F) + 0x0C; // round up, make last digit 0x0C
	const UINT32 DataLen = CurPos + 0x04;
	UINT8* Data = malloc(DataLen);

	CurPos = 0x00;
	memcpy(&Data[CurPos + 0x00], SIG_GYB, 0x02); // File Signature 26 12 (dec)
	Data[CurPos + 0x02] = GYBData->FileVer; // Version Number
	Data[CurPos + 0x03] = GYBData->LFOVal;
	WriteLE32(&Data[CurPos + 0x04], DataLen);
	WriteLE32(&Data[CurPos + 0x08], InsBankOfs);
	WriteLE32(&Data[CurPos + 0x0C], InsMapOfs);
	CurPos += 0x10;

	CurPos = InsMapOfs;
	for (UINT8 CurBnk = 0x00; CurBnk < 0x02; CurBnk++) {
		for (UINT16 CurIns = 0x00; CurIns < 0x80; CurIns++) {
			const GYB_MAP_ILIST_V3* TempMLst = &GYBData->InsMap[CurBnk].Ins[CurIns];
			WriteLE16(&Data[CurPos], TempMLst->EntryCount);
			CurPos += 0x02;

			for (UINT16 CurEnt = 0x00; CurEnt < TempMLst->EntryCount; CurEnt++) {
				Data[CurPos + 0x00] = TempMLst->Entry[CurEnt].BankMSB;
				Data[CurPos + 0x01] = TempMLst->Entry[CurEnt].BankLSB;
				WriteLE16(&Data[CurPos + 0x02], TempMLst->Entry[CurEnt].FMIns);
				CurPos += 0x04;
			}
		}
	}

	CurPos = InsBankOfs;
	for (UINT8 CurBnk = 0x00; CurBnk < 0x02; CurBnk++) {
		const GYB_INS_BANK_V3* TempBnk = &GYBData->InsBank[CurBnk];
		WriteLE16(&Data[CurPos], TempBnk->InsCount);
		CurPos += 0x02;

		UINT32 InsBaseOfs = CurPos;
		for (UINT16 CurIns = 0x00; CurIns < TempBnk->InsCount; CurIns++) {
			const GYB_INSTRUMENT_V3* TempIns = &TempBnk->InsData[CurIns];

			CurPos = InsBaseOfs;
			WriteLE16(&Data[CurPos], TempIns->InsSize);
			InsBaseOfs += TempIns->InsSize; // offset of next instrument
			CurPos += 0x02;

			memcpy(&Data[CurPos + 0x00], &TempIns->Reg[0x00], 0x1E);
			Data[CurPos + 0x1E] = TempIns->Transp;
			Data[CurPos + 0x1F] = TempIns->AddData;
			CurPos += 0x20;

			if (TempIns->AddData & 0x01) {
				const GYB_INS_CHORD_V3* TempCh = &TempIns->ChordNotes;

				Data[CurPos] = TempCh->NoteCount;
				CurPos++;

				memcpy(&Data[CurPos], TempCh->Notes, TempCh->NoteCount);
				CurPos += TempCh->NoteCount;
			}

			size_t TempLng = strlen(TempIns->Name);
			if (TempLng > 0xFF)
				TempLng = 0xFF;
			Data[CurPos] = (UINT8)TempLng;
			CurPos++;
			if (TempLng) {
				memcpy(&Data[CurPos], TempIns->Name, TempLng);
				CurPos += TempLng;
			}
		}
	}

	CurPos = DataLen - 0x04;
	const UINT32 TempLng = CalcGYBChecksum(CurPos, Data);
	memcpy(&Data[CurPos], &TempLng, 0x04);
	CurPos += 0x04;

	*RetDataLen = DataLen;
	*RetData = Data;

	return 0x00;
}
