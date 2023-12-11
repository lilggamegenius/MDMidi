#pragma once

#include <tchar.h>

#include "MDMidDrv.h"
#include "SoundEngine/stdtype.h"

MDMidiDrv_CEXPORT void InitMappingData(void);

MDMidiDrv_CEXPORT UINT8 LoadGYBFile(const TCHAR* FileName);
MDMidiDrv_CEXPORT void FreeGYBFile(void);

MDMidiDrv_CEXPORT UINT8 LoadMappingFile(const TCHAR* FileName);
// Mapping Files don't use malloc

MDMidiDrv_CEXPORT UINT8 LoadPSGEnvFile(const TCHAR* FileName);
MDMidiDrv_CEXPORT void FreePSGEnvelopes(void);

MDMidiDrv_CEXPORT UINT8 LoadDACData(const TCHAR* FileName);
MDMidiDrv_CEXPORT void FreeDACData(void);

CLINKAGE void InitMappingData();