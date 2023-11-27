#ifdef __cplusplus
extern "C"
{
#endif

void InitMappingData(void);

UINT8 LoadGYBFile(const TCHAR* FileName);
void FreeGYBFile(void);

UINT8 LoadMappingFile(const TCHAR* FileName);
// Mapping Files don't use malloc

UINT8 LoadPSGEnvFile(const TCHAR* FileName);
void FreePSGEnvelopes(void);

UINT8 LoadDACData(const TCHAR* FileName);
void FreeDACData(void);

#ifdef __cplusplus
}
#endif
