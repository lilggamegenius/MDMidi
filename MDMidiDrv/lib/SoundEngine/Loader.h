#pragma once

#include "MDMidDrv.h"

CLINKAGE MDMidiDrv_EXPORT void InitMappingData(void);

CLINKAGE MDMidiDrv_EXPORT UINT8 LoadGYBFile(const TCHAR* FileName);
CLINKAGE MDMidiDrv_EXPORT void FreeGYBFile(void);

CLINKAGE MDMidiDrv_EXPORT UINT8 LoadMappingFile(const TCHAR* FileName);
// Mapping Files don't use malloc

CLINKAGE MDMidiDrv_EXPORT UINT8 LoadPSGEnvFile(const TCHAR* FileName);
CLINKAGE MDMidiDrv_EXPORT void FreePSGEnvelopes(void);

CLINKAGE MDMidiDrv_EXPORT UINT8 LoadDACData(const TCHAR* FileName);
CLINKAGE MDMidiDrv_EXPORT void FreeDACData(void);
