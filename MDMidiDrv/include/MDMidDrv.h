#pragma once

#include "MDMidiDrv_Export.h"

#ifdef __cplusplus
#define CLINKAGE extern "C"
CLINKAGE
{
#else
#define CLINKAGE
#endif


MDMidiDrv_EXPORT void InitEngine(void);	// from MainEngine.c
MDMidiDrv_EXPORT void StartEngine(void);

#ifdef __cplusplus
}
#endif
