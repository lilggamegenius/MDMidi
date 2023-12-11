#pragma once

#ifdef __cplusplus
#define CLINKAGE extern "C"
#else
#define CLINKAGE
#endif

//#include "MDMidiDrv_Export.h"
#define MDMidiDrv_EXPORT
#define MDMidiDrv_CEXPORT MDMidiDrv_EXPORT CLINKAGE

MDMidiDrv_CEXPORT void InitEngine(void); // from MainEngine.c
MDMidiDrv_CEXPORT void StartEngine(void);

