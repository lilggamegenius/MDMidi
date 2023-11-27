#ifndef __2612_STRUCTS_H__
#define __2612_STRUCTS_H__

#include "stdtype.h"

// Note: The various xxAlloc variables contain the number of actually allocated entries.
//       This allows for fewer reallocs.

typedef struct _gyb_instrument_v2
{
	char* Name;
	UINT8 Reg[0x20];
} GYB_INSTRUMENT_V2;
typedef struct _gyb_instrument_bank_v2
{
	UINT8 InsCount;
	UINT8 InsMap[0x80];
	GYB_INSTRUMENT_V2* InsData;
} GYB_INS_BANK_V2;
typedef struct _gyb_file_v2
{
	UINT8 FileVer;
	UINT8 LFOVal;
	// Bank 00 - Melody, Bank 01 - Drums
	GYB_INS_BANK_V2 Bank[0x02];
} GYB_FILE_V2;


typedef struct _gyb_instrument_chord_v3
{
	UINT8 NoteAlloc;
	UINT8 NoteCount;
	INT8* Notes;
} GYB_INS_CHORD_V3;
typedef struct _gyb_instrument_v3
{
	UINT16 InsSize;
	UINT8 Reg[0x1E];
	UINT8 Transp;
	UINT8 AddData;
	GYB_INS_CHORD_V3 ChordNotes;
	char* Name;
} GYB_INSTRUMENT_V3;


typedef struct _gyb_map_sub_item_v3
{
	UINT8 BankMSB;
	UINT8 BankLSB;
	UINT16 FMIns;
} GYB_MAP_ITEM_V3;
typedef struct _gyb_map_item_list_v3
{
	UINT16 EntryAlloc;
	UINT16 EntryCount;
	GYB_MAP_ITEM_V3* Entry;
} GYB_MAP_ILIST_V3;
typedef struct _gyb_map_v3
{
	GYB_MAP_ILIST_V3 Ins[0x80];
} GYB_MAP_V3;

typedef struct _gyb_instrument_bank_v3
{
	UINT16 InsAlloc;
	UINT16 InsCount;
	GYB_INSTRUMENT_V3* InsData;
} GYB_INS_BANK_V3;
typedef struct _gyb_file_v3
{
	UINT8 FileVer;
	UINT8 LFOVal;
	// Map 00 - Melody Instruments, Map 01 - GM Drums
	GYB_MAP_V3 InsMap[0x02];
	// Bank 00 - Melody, Bank 01 - Drums
	GYB_INS_BANK_V3 InsBank[0x02];
} GYB_FILE_V3;

#define GYBBANK_MELODY	0x00
#define GYBBANK_DRUM	0x01

#endif	// __2612_STRUCTS_H__
