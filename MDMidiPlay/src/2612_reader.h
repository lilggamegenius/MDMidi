#ifndef __2612_READER_H__
#define __2612_READER_H__

#include "stdtype.h"
#include "2612_structs.h"

UINT8 LoadGYBData_v2(UINT32 DataLen, UINT8* Data, GYB_FILE_V2* GYBData);
void FreeGYBData_v2(GYB_FILE_V2* GYBData);
UINT8 SaveGYBData_v2(UINT32* RetDataLen, UINT8** RetData, const GYB_FILE_V2* GYBData);

UINT8 LoadGYBData_v3(UINT32 DataLen, UINT8* Data, GYB_FILE_V3* GYBData);
void FreeGYBData_v3(GYB_FILE_V3* GYBData);
UINT8 SaveGYBData_v3(UINT32* RetDataLen, UINT8** RetData, const GYB_FILE_V3* GYBData);

UINT8 ConvertGYBData_2to3(GYB_FILE_V3* DstGyb3, const GYB_FILE_V2* SrcGyb2);
void FixGYBDataStruct_v3(GYB_FILE_V3* GYBData);	// Note: also called from SaveGYBData_v3

#endif	// __2612_READER_H__
