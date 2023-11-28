#pragma once

#ifdef __cplusplus
#define CLINKAGE extern "C"
#else
#define CLINKAGE
#endif

#include "MDMidiDrv_Export.h"

#ifdef __cplusplus
extern "C"
{
#endif

MDMidiDrv_EXPORT void InitEngine(void);	// from MainEngine.c
MDMidiDrv_EXPORT void StartEngine(void);

#ifdef __cplusplus
}
#endif
