/*
	mid2smps MIDI Driver
	by Valley Bell
	based on the BASSMIDI Driver by kode54 and Mudlord
*/

#define _CRT_SECURE_NO_WARNINGS

#define STRICT
#ifndef _WIN32_WINNT
#define _WIN32_WINNT _WIN32_WINNT_WINXP
#endif
#define WINVER 0x0500

#if __DMC__
unsigned long _beginthreadex( void *security, unsigned stack_size,
		unsigned ( __stdcall *start_address )( void * ), void *arglist,
		unsigned initflag, unsigned *thrdaddr );
void _endthreadex( unsigned retval );
#endif

#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES 1
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <windows.h>
#include <process.h>
#include <Shlwapi.h>
#include <mmreg.h>	// for MM_MSFT_WDMAUDIO_MIDIOUT

#include  "MDMidDrv.h"

#if defined(_MSC_VER) && _MSC_VER < 1300
// from WinNT.h
#define KEY_WOW64_32KEY         (0x0200)
#define KEY_WOW64_64KEY         (0x0100)
#define KEY_WOW64_RES           (0x0300)

/*// from BaseTsd.h
typedef unsigned __int32 ULONG_PTR;
// from intsafe.h
typedef ULONG_PTR   DWORD_PTR;*/
typedef unsigned __int32	DWORD_PTR;

// from WinReg.h
//typedef __success(return==ERROR_SUCCESS) LONG LSTATUS;
typedef LONG				LSTATUS;

// from MMSystem.h
#define MOD_WAVETABLE   6  /* hardware wavetable synth */
#define MOD_SWSYNTH     7  /* software synth */

typedef struct tagMIDIOUTCAPS2A {
    WORD wMid;
    WORD wPid;
    MMVERSION vDriverVersion;
    CHAR szPname[MAXPNAMELEN];
    WORD wTechnology;
    WORD wVoices;
    WORD wNotes;
    WORD wChannelMask;
    DWORD dwSupport;
    GUID ManufacturerGuid;
    GUID ProductGuid;
    GUID NameGuid;
} MIDIOUTCAPS2A, *PMIDIOUTCAPS2A, *NPMIDIOUTCAPS2A, *LPMIDIOUTCAPS2A;

typedef struct tagMIDIOUTCAPS2W {
    WORD wMid;
    WORD wPid;
    MMVERSION vDriverVersion;
    WCHAR szPname[MAXPNAMELEN];
    WORD wTechnology;
    WORD wVoices;
    WORD wNotes;
    WORD wChannelMask;
    DWORD dwSupport;
    GUID ManufacturerGuid;
    GUID ProductGuid;
    GUID NameGuid;
} MIDIOUTCAPS2W, *PMIDIOUTCAPS2W, *NPMIDIOUTCAPS2W, *LPMIDIOUTCAPS2W;
#endif

#include <mmddk.h>
#include <tchar.h>

/*#define BASSDEF(f) (WINAPI *f)	// define the BASS/BASSMIDI functions as pointers
#define BASSMIDIDEF(f) (WINAPI *f)
#define LOADBASSFUNCTION(f) *((void**)&f)=GetProcAddress(bass,#f)
#define LOADBASSMIDIFUNCTION(f) *((void**)&f)=GetProcAddress(bassmidi,#f)
#include <bass.h>
#include <bassmidi.h>*/

#include "sound_out.h"

extern "C" {
	#include "SoundEngine/Loader.h"
	#include "SoundEngine/Sound.h"

	void DoShortMidiEvent(UINT8 Command, UINT8 Value1, UINT8 Value2);

	void DoLongMidiEvent(UINT8 Command, UINT8 Value, UINT32 DataLen, UINT8 *Data);
}

#define DEBUG_LOG_MID_DATA
#define DEBUG_LOG_MSGS
//#define LOGFILE_PATH_LOG	"C:\\MidDrv.log"	// replaced by Registry Entry
//#define LOGFILE_PATH_MID	"C:\\MidLog.mid"	// replaced by Registry Entry

#define MAX_DRIVERS 2
#define MAX_CLIENTS 1 // Per driver

#define SAMPLES_PER_FRAME_MAX	1024
//#define SAMPLES_PER_FRAME_DS	6 * SAMPLES_PER_FRAME_XA
//#define SAMPLES_PER_FRAME_XA	88 * 2
#define FRAMES_XAUDIO 15
#define FRAMES_DSOUND 50
#define SAMPLE_RATE_DEFAULT 44100

enum {
	SND_DRV_FAIL   = 0,
	SND_DRV_WINMM  = 1,
	SND_DRV_DSOUND = 2,
	SND_DRV_XAUDIO = 3
};

struct Driver_Client {
	int allocated;
	DWORD_PTR instance;
	DWORD flags;
	DWORD_PTR callback;
};

//Note: drivers[0] is not used (See OnDriverOpen).
struct Driver {
	int open;
	int clientCount;
	HDRVR hdrvr;
	struct Driver_Client clients[MAX_CLIENTS];
} drivers[MAX_DRIVERS + 1];

static int driverCount = 0;

static volatile int OpenCount   = 0;
static volatile int modm_closed = 1;

static CRITICAL_SECTION mim_section;
static volatile int stop_thread    = 0;
static volatile int reset_synth[2] = {0, 0};
static HANDLE hCalcThread          = nullptr;
static DWORD processPriority;
static HANDLE load_sfevent = nullptr;

static bool sound_out_float         = false;
static UINT32 SndAPIDisable         = 0x00;
static float sound_out_volume_float = 1.0;
static int sound_out_volume_int     = 0x100;
static int smpls_per_frame          = 0;             // 0 = default - calculate
static int xaudio2_frames           = FRAMES_XAUDIO; // default
static int dsound_frames            = FRAMES_DSOUND; // default
static int sample_rate              = SAMPLE_RATE_DEFAULT;
static TCHAR GYBPath[MAX_PATH];
static TCHAR PSGPath[MAX_PATH];
static TCHAR MapPath[MAX_PATH];
static TCHAR DACPath[MAX_PATH];

static UINT8 sound_drv_mode;
static sound_out *sound_driver = nullptr;

//static HINSTANCE bass = 0;			// bass handle
//static HINSTANCE bassmidi = 0;			// bassmidi handle
//TODO: Can be done with: HMODULE GetDriverModuleHandle(HDRVR hdrvr);  (once DRV_OPEN has been called)
static HINSTANCE hinst = nullptr; //main DLL handle

#ifdef DEBUG_LOG_MID_DATA
static TCHAR MidLogPath[MAX_PATH];
static FILE *hLogMid      = nullptr;
static UINT32 MidLogDelay = 0;
#endif

#ifndef DEBUG_LOG_MSGS
#define	fprintd
#else
#define	fprintd		fprintf

static TCHAR LogPath[MAX_PATH];
static FILE *hLogFile = nullptr;
#endif
static UINT32 SentBufCount;

MDMidiDrv_EXPORT bool load_settings();

static void DoStopClient();

class message_window {
	HWND m_hWnd;
	ATOM class_atom;

public:
	message_window() {
		static auto class_name = _T("MDMidiDrv message window");
		WNDCLASSEX cls         = {0};
		cls.cbSize             = sizeof(cls);
		cls.lpfnWndProc        = DefWindowProc;
		cls.hInstance          = hinst;
		cls.lpszClassName      = class_name;
		class_atom             = RegisterClassEx(&cls);
		if(class_atom) {
			m_hWnd = CreateWindowEx(0, reinterpret_cast<LPCTSTR>(class_atom), _T("MDMidiDrv"), 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hinst, nullptr);
		} else {
			m_hWnd = nullptr;
		}
	}

	~message_window() {
		if(IsWindow(m_hWnd))
			DestroyWindow(m_hWnd);
		if(class_atom)
			UnregisterClass(reinterpret_cast<LPCTSTR>(class_atom), hinst);
	}

	[[nodiscard]] HWND get_hwnd() const {
		return m_hWnd;
	}
};

message_window *g_msgwnd = nullptr;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
	if(fdwReason == DLL_PROCESS_ATTACH) {
		hinst = hinstDLL;
		DisableThreadLibraryCalls(hinstDLL);

		load_settings();
		#ifdef DEBUG_LOG_MSGS
		hLogFile = _tfopen(LogPath, _T("at"));
		if(hLogFile != nullptr) {
			fprintf(hLogFile, "DllMain: DLL_PROCESS_ATTACH\n");
			fclose(hLogFile);
		}
		#endif
		g_msgwnd = new message_window;
	} else if(fdwReason == DLL_PROCESS_DETACH) {
		DoStopClient();
		if(hCalcThread != nullptr) {
			WaitForSingleObject(hCalcThread, INFINITE);
			CloseHandle(hCalcThread);
			hCalcThread = nullptr;
		}
		delete g_msgwnd;
	}
	return TRUE;
}

#ifdef _UNICODE
#define FMT_TSTR	"%ls"
#else
#define FMT_TSTR	"%s"
#endif
static bool LoadDriverData(FILE *hDebugFile) {
	if(hDebugFile != nullptr)
		fprintf(hDebugFile, "Loading GYB file: " FMT_TSTR "\n", GYBPath), fflush(hDebugFile);
	UINT8 RetVal = LoadGYBFile(GYBPath);
	if(hDebugFile != nullptr)
		fprintf(hDebugFile, "    result: %02X\n", RetVal);
	//if (RetVal)
	//	return false;

	if(hDebugFile != nullptr)
		fprintf(hDebugFile, "Loading PSG Envelopes: " FMT_TSTR "\n", PSGPath), fflush(hDebugFile);
	RetVal = LoadPSGEnvFile(PSGPath);
	if(hDebugFile != nullptr)
		fprintf(hDebugFile, "    result: %02X\n", RetVal);
	//if (RetVal)
	//	return false;

	if(hDebugFile != nullptr)
		fprintf(hDebugFile, "Loading Mappings: " FMT_TSTR "\n", MapPath), fflush(hDebugFile);
	RetVal = LoadMappingFile(MapPath);
	if(hDebugFile != nullptr)
		fprintf(hDebugFile, "    result: %02X\n", RetVal);
	//if (RetVal)
	//	return false;

	if(hDebugFile != nullptr)
		fprintf(hDebugFile, "Loading DAC Data: " FMT_TSTR "\n", DACPath), fflush(hDebugFile);
	RetVal = LoadDACData(DACPath);
	if(hDebugFile != nullptr)
		fprintf(hDebugFile, "    result: %02X\n", RetVal);
	//if (RetVal)
	//	return false;

	if(hDebugFile != nullptr)
		fprintf(hDebugFile, "Done.\n"), fflush(hDebugFile);
	return true;
}

LRESULT DoDriverLoad() {
	//The DRV_LOAD message is always the first message that a device driver receives.
	//Notifies the driver that it has been loaded. The driver should make sure that any hardware and supporting drivers it needs to function properly are present.
	memset(drivers, 0, sizeof(drivers));
	driverCount = 0;

	return DRV_OK;
}

LRESULT DoDriverOpen(HDRVR hdrvr, LPCWSTR driverName, LONG lParam) {
	/**
	Remarks

	If the driver returns a nonzero value, the system uses that value as the driver identifier (the dwDriverId parameter)
	in messages it subsequently sends to the driver instance. The driver can return any type of value as the identifier.
	For example, some drivers return memory addresses that point to instance-specific information. Using this method of
	specifying identifiers for a driver instance gives the drivers ready access to the information while they are processing messages.

	When the driver's DriverProc function receives a
	DRV_OPEN message, it should:
	1. Allocate memory space for a structure instance.
	2. Add the structure instance to the linked list.
	3. Store instance data in the new list entry.
	4. Specify the entry's number or address as the return value for the DriverProc function.
	Subsequent calls to DriverProc will include the list entry's identifier as its dwDriverID
	argument
	*/
	int driverNum;

	if(driverCount == MAX_DRIVERS)
		return 0;

	for(driverNum = 1; driverNum < MAX_DRIVERS; driverNum++) {
		if(!drivers[driverNum].open)
			break;
	}
	if(driverNum == MAX_DRIVERS)
		return 0;

	drivers[driverNum].open        = 1;
	drivers[driverNum].clientCount = 0;
	drivers[driverNum].hdrvr       = hdrvr;
	driverCount++;
	#ifdef DEBUG_LOG_MSGS
	//hLogFile = fopen(LOGFILE_PATH_LOG, "at");
	hLogFile = _tfopen(LogPath, _T("at"));
	if(hLogFile != nullptr) {
		fprintf(hLogFile, "DoDriverOpen %d: OK, %d drivers\n", driverNum, driverCount);
		fclose(hLogFile);
	}
	#endif
	return driverNum;
}

LRESULT DoDriverClose(DWORD_PTR dwDriverId, HDRVR hdrvr, LONG lParam1, LONG lParam2) {
	for(int i = 0; i < MAX_DRIVERS; i++) {
		if(drivers[i].open && drivers[i].hdrvr == hdrvr) {
			drivers[i].open = 0;
			--driverCount;
			#ifdef DEBUG_LOG_MSGS
			//hLogFile = fopen(LOGFILE_PATH_LOG, "at");
			hLogFile = _tfopen(LogPath, _T("at"));
			if(hLogFile != nullptr) {
				fprintf(hLogFile, "DoDriverClose %d: OK, %u drivers\n", i, driverCount);
				fclose(hLogFile);
			}
			#endif
			return DRV_OK;
		}
	}
	return DRV_CANCEL;
}

LRESULT DoDriverConfigure(DWORD_PTR dwDriverId, HDRVR hdrvr, HWND parent, DRVCONFIGINFO *configInfo) {
	return DRV_CANCEL;
}

/* INFO Installable Driver Reference: http://msdn.microsoft.com/en-us/library/ms709328%28v=vs.85%29.aspx */
/* The original header is LONG DriverProc(DWORD dwDriverId, HDRVR hdrvr, UINT msg, LONG lParam1, LONG lParam2);
but that does not support 64bit. See declaration of DefDriverProc to see where the values come from.
*/
STDAPI_(LRESULT) DriverProc(DWORD_PTR dwDriverId, HDRVR hdrvr, UINT uMsg, LPARAM lParam1, LPARAM lParam2) {
	switch(uMsg) {
		/* Seems this is only for kernel mode drivers
			case DRV_INSTALL:
				return DoDriverInstall(dwDriverId, hdrvr, static_cast<DRVCONFIGINFO*>(lParam2));
			case DRV_REMOVE:
				DoDriverRemove(dwDriverId, hdrvr);
				return DRV_OK;
		*/
		case DRV_QUERYCONFIGURE:
			//TODO: Until it doesn't have a configuration window, it should return 0.
			return DRV_CANCEL;
		case DRV_CONFIGURE: return DoDriverConfigure(dwDriverId, hdrvr, reinterpret_cast<HWND>(lParam1), reinterpret_cast<DRVCONFIGINFO*>(lParam2));

		/* TODO: Study this. It has implications:
				Calling OpenDriver, described in the Win32 SDK. This function calls SendDriverMessage to
				send DRV_LOAD and DRV_ENABLE messages only if the driver has not been previously loaded,
				and then to send DRV_OPEN.
				· Calling CloseDriver, described in the Win32 SDK. This function calls SendDriverMessage to
				send DRV_CLOSE and, if there are no other open instances of the driver, to also send
				DRV_DISABLE and DRV_FREE.
		*/
		case DRV_LOAD: return DoDriverLoad();
		case DRV_FREE:
			//The DRV_FREE message is always the last message that a device driver receives.
			//Notifies the driver that it is being removed from memory. The driver should free any memory and other system resources that it has allocated.
			return DRV_OK;
		case DRV_OPEN: return DoDriverOpen(hdrvr, reinterpret_cast<LPCWSTR>(lParam1), static_cast<LONG>(lParam2));
		case DRV_CLOSE: return DoDriverClose(dwDriverId, hdrvr, static_cast<LONG>(lParam1), static_cast<LONG>(lParam2));
		default: return DefDriverProc(dwDriverId, hdrvr, uMsg, lParam1, lParam2);
	}
}

HRESULT modGetCaps(UINT uDeviceID, MIDIOUTCAPS *capsPtr, DWORD capsSize) {
	#define MOC_MID		0x7FFF	// MM_UNMAPPED
	#define MOC_PID		MM_MSFT_WDMAUDIO_MIDIOUT
	#define MOC_CHNMASK	0x3E3F	// 0..5, 9..13

	MIDIOUTCAPSA *myCapsA;
	MIDIOUTCAPSW *myCapsW;
	MIDIOUTCAPS2A *myCaps2A;
	MIDIOUTCAPS2W *myCaps2W;
	const CHAR synthName[]   = "mid2smps MIDI Driver";
	const WCHAR synthNameW[] = L"mid2smps MIDI Driver";

	ZeroMemory(capsPtr, capsSize);
	#if 0	// for debugging
	{
		char TempStr[0x80];

		sprintf(TempStr, "Requesting capsSize: 0x%02X\n"
						"CapsA: 0x$%2X, CapsW: 0x$%2X, Caps2A: 0x$%2X, Caps2W: 0x$%2X",
						capsSize,
						sizeof(MIDIOUTCAPSA), sizeof(MIDIOUTCAPSW),
						sizeof(MIDIOUTCAPS2A), sizeof(MIDIOUTCAPS2W));
		MessageBox(nullptr, TempStr, "MIDI Drv. Info", MB_OK);
	}
	#endif
	switch(capsSize) {
		case sizeof(MIDIOUTCAPSA): myCapsA = capsPtr;
			myCapsA->wMid = MOC_MID;
			myCapsA->wPid = MOC_PID;
			strncpy(myCapsA->szPname, synthName, MAXPNAMELEN - 1);
			myCapsA->wTechnology    = MOD_SWSYNTH;
			myCapsA->vDriverVersion = 0x0090;
			myCapsA->wVoices        = 10;
			myCapsA->wNotes         = 10;
			myCapsA->wChannelMask   = MOC_CHNMASK;
			myCapsA->dwSupport      = MIDICAPS_VOLUME;
			return MMSYSERR_NOERROR;
		case sizeof(MIDIOUTCAPSW): myCapsW = (MIDIOUTCAPSW*) capsPtr;
			myCapsW->wMid = MOC_MID;
			myCapsW->wPid = MOC_PID;
			wcsncpy(myCapsW->szPname, synthNameW, MAXPNAMELEN - 1);
			myCapsW->wTechnology    = MOD_SWSYNTH;
			myCapsW->vDriverVersion = 0x0090;
			myCapsW->wVoices        = 10;
			myCapsW->wNotes         = 10;
			myCapsW->wChannelMask   = MOC_CHNMASK;
			myCapsW->dwSupport      = MIDICAPS_VOLUME;
			return MMSYSERR_NOERROR;
		case sizeof(MIDIOUTCAPS2A): myCaps2A = (MIDIOUTCAPS2A*) capsPtr;
			myCaps2A->wMid = MOC_MID;
			myCaps2A->wPid = MOC_PID;
			strncpy(myCaps2A->szPname, synthName, MAXPNAMELEN - 1);
			myCaps2A->wTechnology    = MOD_SWSYNTH;
			myCaps2A->vDriverVersion = 0x0090;
			myCaps2A->wVoices        = 10;
			myCaps2A->wNotes         = 10;
			myCaps2A->wChannelMask   = MOC_CHNMASK;
			myCaps2A->dwSupport      = MIDICAPS_VOLUME;
			return MMSYSERR_NOERROR;
		case sizeof(MIDIOUTCAPS2W): myCaps2W = (MIDIOUTCAPS2W*) capsPtr;
			myCaps2W->wMid = MOC_MID;
			myCaps2W->wPid = MOC_PID;
			wcsncpy(myCaps2W->szPname, synthNameW, MAXPNAMELEN - 1);
			myCaps2W->wTechnology    = MOD_SWSYNTH;
			myCaps2W->vDriverVersion = 0x0090;
			myCaps2W->wVoices        = 10;
			myCaps2W->wNotes         = 10;
			myCaps2W->wChannelMask   = MOC_CHNMASK;
			myCaps2W->dwSupport      = MIDICAPS_VOLUME;
			return MMSYSERR_NOERROR;
		default: return MMSYSERR_ERROR;
	}
}

struct evbuf_t {
	UINT uDeviceID;
	UINT uMsg;
	DWORD_PTR dwParam1;
	DWORD_PTR dwParam2;
	int exlen;
	unsigned char *sysexbuffer;
};

#define EVBUFF_SIZE	512

static evbuf_t evbuf[EVBUFF_SIZE];
static UINT evbwpoint         = 0;
static UINT evbrpoint         = 0;
static volatile LONG evbcount = 0;
static UINT evbsysexpoint;

int bmsyn_buf_check() {
	EnterCriticalSection(&mim_section);
	const int retval = evbcount;
	LeaveCriticalSection(&mim_section);

	return retval;
}

#ifdef DEBUG_LOG_MID_DATA
static const UINT8 MidiHeader[0x12] =
{
	0x4D,
	0x54,
	0x68,
	0x64,
	0x00,
	0x00,
	0x00,
	0x06,
	0x00,
	0x00,
	0x00,
	0x01,
	0x01,
	0x00,
	0x4D,
	0x54,
	0x72,
	0x6B
};

static void PutBE16(UINT16 Value, FILE *hFile) {
	fputc(Value >> 8 & 0xFF, hFile);
	fputc(Value >> 0 & 0xFF, hFile);
}

static void PutBE32(UINT32 Value, FILE *hFile) {
	fputc(Value >> 24 & 0xFF, hFile);
	fputc(Value >> 16 & 0xFF, hFile);
	fputc(Value >> 8 & 0xFF, hFile);
	fputc(Value >> 0 & 0xFF, hFile);
}

static void PutMidiDelay(UINT32 Delay, FILE *hFile) {
	UINT8 DataArr[0x05];

	UINT8 CurPos = 0x00;
	do {
		DataArr[CurPos] = Delay & 0x7F;
		Delay >>= 7;
		CurPos++;
	} while(Delay);

	while(--CurPos)
		fputc(0x80 | DataArr[CurPos], hFile);
	fputc(DataArr[CurPos], hFile);
}
#endif

#define GET_BYTE(x, shift)	(UINT8)((x >> shift) & 0xFF)

int bmsyn_play_some_data(FILE *hDebugFile) {
	DWORD dwParam1;

	int played = 0;
	if(!bmsyn_buf_check()) {
		played = ~0;
		return played;
	}

	do {
		EnterCriticalSection(&mim_section);
		const UINT evbpoint = evbrpoint;
		if(++evbrpoint >= EVBUFF_SIZE)
			evbrpoint -= EVBUFF_SIZE;

		const UINT uMsg            = evbuf[evbpoint].uMsg;
		dwParam1                   = evbuf[evbpoint].dwParam1;
		int exlen                  = evbuf[evbpoint].exlen;
		unsigned char *sysexbuffer = evbuf[evbpoint].sysexbuffer;

		LeaveCriticalSection(&mim_section);
		switch(uMsg) {
			default: break;
			case MODM_DATA: if(hDebugFile != nullptr)
					fprintf(
						hDebugFile,
						"Short MIDI Event: %02X %02X %02X\n",
						GET_BYTE(dwParam1, 0),
						GET_BYTE(dwParam1, 8),
						GET_BYTE(dwParam1, 16)
					);

			// C0/D0 has 1 parameter, all others 2 ones
				exlen = (dwParam1 & 0xE0) == 0xC0 ? 2 : 3;
				#ifdef DEBUG_LOG_MID_DATA
				if(hLogMid != nullptr) {
					PutMidiDelay(MidLogDelay, hLogMid);
					MidLogDelay = 0;
					fwrite(&dwParam1, 0x01, exlen, hLogMid);
				}
				#endif
				DoShortMidiEvent(
					GET_BYTE(dwParam1, 0),
					// 0x0000FF
					GET_BYTE(dwParam1, 8),
					// 0x00FF00
					GET_BYTE(dwParam1, 16)
				); // 0xFF0000
				break;
			case MODM_LONGDATA: if(hDebugFile != nullptr)
					fprintf(
						hDebugFile,
						"Long MIDI Event: [size %02X] %02X %02X %02X ...\n",
						exlen,
						sysexbuffer[0],
						sysexbuffer[1],
						sysexbuffer[2]
					);
				#ifdef DEBUG
			FILE* logfile;

			logfile = fopen("D:\\dbglog2.log", "at");
			if(logfile!=nullptr)
			{
				for (int i = 0; i < exlen; i++)
					fprintf(logfile,"%x ", sysexbuffer[i]);
				fprintf(logfile,"\n");
			}
			fclose(logfile);
				#endif

				#ifdef DEBUG_LOG_MID_DATA
				if(hLogMid != nullptr) {
					PutMidiDelay(MidLogDelay, hLogMid);
					MidLogDelay = 0;

					fputc(sysexbuffer[0], hLogMid);                    // write F0
					PutMidiDelay(exlen - 1, hLogMid);                  // write data length
					fwrite(sysexbuffer + 1, 0x01, exlen - 1, hLogMid); // write data
				}
				#endif
				DoLongMidiEvent(sysexbuffer[0], 0x00, exlen - 1, sysexbuffer + 1);
				free(sysexbuffer);
				break;
		}
	} while(InterlockedDecrement(&evbcount));

	return played;
}

LSTATUS GetRegDataInt(HKEY hKeyCU, HKEY hKeyLM, LPCTSTR lpValueName, DWORD *lpData) {
	DWORD dwSize;

	LSTATUS lResult = -1;
	if(hKeyCU != nullptr) {
		// read from HKEY_CURRENT_USER
		dwSize  = sizeof(DWORD);
		lResult = RegQueryValueEx(hKeyCU, lpValueName, nullptr, nullptr, (LPBYTE) lpData, &dwSize);
		if(lResult == ERROR_SUCCESS)
			return ERROR_SUCCESS;
	}

	if(hKeyLM != nullptr) {
		// read from HKEY_LOCAL_MACHINE
		dwSize  = sizeof(DWORD); // just to be sure
		lResult = RegQueryValueEx(hKeyLM, lpValueName, nullptr, nullptr, (LPBYTE) lpData, &dwSize);
		if(lResult == ERROR_SUCCESS)
			return ERROR_SUCCESS;
	}

	*lpData = 0;
	return lResult;
}

LSTATUS GetRegDataStr(HKEY hKeyCU, HKEY hKeyLM, LPCTSTR lpValueName, DWORD StrSize, void *lpData, DWORD *FinalStrSize) {
	DWORD dwSize;

	LSTATUS lResult = -1;
	if(hKeyCU != nullptr) {
		// read from HKEY_CURRENT_USER
		dwSize  = StrSize * sizeof(TCHAR);
		lResult = RegQueryValueEx(hKeyCU, lpValueName, nullptr, nullptr, (LPBYTE) lpData, &dwSize);
		if(lResult == ERROR_SUCCESS) {
			if(FinalStrSize != nullptr)
				*FinalStrSize = dwSize;
			return ERROR_SUCCESS;
		}
	}

	if(hKeyLM != nullptr) {
		// read from HKEY_LOCAL_MACHINE
		dwSize  = StrSize * sizeof(TCHAR); // just to be sure
		lResult = RegQueryValueEx(hKeyLM, lpValueName, nullptr, nullptr, (LPBYTE) lpData, &dwSize);
		if(lResult == ERROR_SUCCESS) {
			if(FinalStrSize != nullptr)
				*FinalStrSize = dwSize;
			return ERROR_SUCCESS;
		}
	}

	_tccpy(static_cast<TCHAR *>(lpData), _T(""));
	return lResult;
}

bool load_settings() {
	int config_volume;
	HKEY hKeyCU;
	HKEY hKeyLM;

	// --- Open Registry Keys ---
	LSTATUS lResult = RegOpenKeyEx(HKEY_CURRENT_USER, _T("Software\\MD MIDI Driver"), 0, KEY_READ, &hKeyCU);
	if(lResult != ERROR_SUCCESS)
		hKeyCU = nullptr;
	lResult = RegOpenKeyEx(HKEY_LOCAL_MACHINE, _T("Software\\MD MIDI Driver"), 0, KEY_READ, &hKeyLM);
	if(lResult != ERROR_SUCCESS)
		hKeyLM = nullptr;
	if(hKeyCU == nullptr && hKeyLM == nullptr)
		return false;

	// --- Read Sound-related Values ---
	// Note: If the function fails, 0 is written to the parameter value.
	//       Value 0 equals the "default" value, so I don't need to check lResult.
	GetRegDataInt(hKeyCU, hKeyLM, _T("Volume"), reinterpret_cast<DWORD*>(&config_volume));
	if(!config_volume)
		config_volume = 10000;
	GetRegDataInt(hKeyCU, hKeyLM, _T("BufNum"), reinterpret_cast<DWORD*>(&dsound_frames));
	if(!dsound_frames)
		dsound_frames = FRAMES_DSOUND;
	GetRegDataInt(hKeyCU, hKeyLM, _T("XBufNum"), reinterpret_cast<DWORD*>(&xaudio2_frames));
	if(!xaudio2_frames)
		xaudio2_frames = FRAMES_XAUDIO;
	GetRegDataInt(hKeyCU, hKeyLM, _T("BufSmpls"), reinterpret_cast<DWORD*>(&smpls_per_frame));
	if(!smpls_per_frame)
		smpls_per_frame = 0;
	GetRegDataInt(hKeyCU, hKeyLM, _T("SampleRate"), reinterpret_cast<DWORD*>(&sample_rate));
	if(!sample_rate)
		sample_rate = SAMPLE_RATE_DEFAULT;
	GetRegDataInt(hKeyCU, hKeyLM, _T("SoundAPIDisable"), reinterpret_cast<DWORD*>(&SndAPIDisable));
	if(!SndAPIDisable)
		SndAPIDisable = 0x00;

	// -- Read Emulation-related Values ---
	// Note: If the function fails, "" is copied to the parameter string.
	//       Since this is the default value here, I can ignore lResult here.
	GetRegDataStr(hKeyCU, hKeyLM, _T("GYBPath"), MAX_PATH, GYBPath, nullptr);
	GetRegDataStr(hKeyCU, hKeyLM, _T("PSGPath"), MAX_PATH, PSGPath, nullptr);
	GetRegDataStr(hKeyCU, hKeyLM, _T("MappingPath"), MAX_PATH, MapPath, nullptr);
	GetRegDataStr(hKeyCU, hKeyLM, _T("DACPath"), MAX_PATH, DACPath, nullptr);
	#ifdef DEBUG_LOG_MSGS
	GetRegDataStr(hKeyCU, hKeyLM, _T("LogFile"), MAX_PATH, LogPath, nullptr);
	#endif
	#ifdef DEBUG_LOG_MID_DATA
	GetRegDataStr(hKeyCU, hKeyLM, _T("MidLogFile"), MAX_PATH, MidLogPath, nullptr);
	#endif

	// --- Close Registry Keys ---
	if(hKeyLM != nullptr)
		RegCloseKey(hKeyLM);
	if(hKeyCU != nullptr)
		RegCloseKey(hKeyCU);

	// convert registry volume value into "real" value
	sound_out_volume_float = static_cast<float>(config_volume) / 10000.0f;
	sound_out_volume_int   = static_cast<int>(sound_out_volume_float * static_cast<float>(0x100) + 0.5f);

	return true;
}

#include <VersionHelpers.h>

BOOL IsVistaOrNewer() {
	return IsWindowsVistaOrGreater();
}

static UINT8 LoadSoundDriver(unsigned int *FrameCnt, unsigned int *SmplPFrame, FILE *hDebugFile) {
	const char *err;
	unsigned int FrmSmpls = 0;

	sound_out_float = IsVistaOrNewer() ? true : false;

	if(hDebugFile != nullptr)
		fprintd(hDebugFile, "Sound driver init:\n");

	if(smpls_per_frame > 0)
		FrmSmpls = smpls_per_frame;
	else if(smpls_per_frame == 0)
		FrmSmpls = sample_rate / 250;
	else if(smpls_per_frame < 0)
		FrmSmpls = sample_rate / smpls_per_frame;

	if(FrmSmpls > SAMPLES_PER_FRAME_MAX)
		FrmSmpls = SAMPLES_PER_FRAME_MAX;
	else if(FrmSmpls < 10)
		FrmSmpls = 10;

	#if _MSC_VER >= 1300
	// --- XAudio2 ---
	if (hDebugFile != nullptr)
		fprintd(hDebugFile, "\tXAudio2 ");
	if (!(SndAPIDisable & 0x04)) {
		const unsigned int FrmCnt = xaudio2_frames;

		sound_driver = create_sound_out_xaudio2();
		err = sound_driver->open(g_msgwnd->get_hwnd(), sample_rate, 2, sound_out_float, FrmSmpls, FrmCnt);
		if (err == nullptr) {
			*SmplPFrame = FrmSmpls;
			*FrameCnt = FrmCnt;
			if (hDebugFile != nullptr)
				fprintd(hDebugFile, "Successful\n");
			return SND_DRV_XAUDIO;
		}
		delete sound_driver;
		if (hDebugFile != nullptr)
			fprintd(hDebugFile, "failed\n");
	}
	else {
		if (hDebugFile != nullptr)
			fprintd(hDebugFile, "disabled\n");
	}
	// --- XAudio2 End ---
	#endif	// _MSC_VER >= 13000

	// --- DSound ---
	if(hDebugFile != nullptr)
		fprintd(hDebugFile, "\tDSound ");
	if(!(SndAPIDisable & 0x02)) {
		const unsigned int FrmCnt = dsound_frames;

		sound_driver = create_sound_out_ds();
		err          = sound_driver->open(g_msgwnd->get_hwnd(), sample_rate, 2, sound_out_float, FrmSmpls, FrmCnt);
		if(err == nullptr) {
			*SmplPFrame = FrmSmpls;
			*FrameCnt   = FrmCnt;
			if(hDebugFile != nullptr)
				fprintd(hDebugFile, "Successful\n");
			return SND_DRV_DSOUND;
		}
		delete sound_driver;
		if(hDebugFile != nullptr)
			fprintd(hDebugFile, "failed\n");
	} else {
		if(hDebugFile != nullptr)
			fprintd(hDebugFile, "disabled\n");
	}
	// --- DSound End ---

	// --- WinMM ---
	if(hDebugFile != nullptr)
		fprintd(hDebugFile, "\tWinMM ");
	if(!(SndAPIDisable & 0x01)) {
		const unsigned int FrmCnt = dsound_frames;

		sound_driver = create_sound_out_winmm();
		err          = sound_driver->open(g_msgwnd->get_hwnd(), sample_rate, 2, sound_out_float, FrmSmpls, FrmCnt);
		if(err == nullptr) {
			*SmplPFrame = FrmSmpls;
			*FrameCnt   = FrmCnt;
			if(hDebugFile != nullptr)
				fprintd(hDebugFile, "Successful\n");
			return SND_DRV_WINMM;
		}
		delete sound_driver;
		if(hDebugFile != nullptr)
			fprintd(hDebugFile, "failed\n");
	} else {
		if(hDebugFile != nullptr)
			fprintd(hDebugFile, "disabled\n");
	}
	// --- WinMM End ---

	sound_driver = nullptr;
	return SND_DRV_FAIL;
}

unsigned __stdcall threadfunc(LPVOID lpV) {
	unsigned int FrameCnt;
	UINT8 RetVal;
	int opend = 0;
	INT32 sound_buffer_extra[SAMPLES_PER_FRAME_MAX];
	const auto sound_buffer_f = reinterpret_cast<float*>(sound_buffer_extra);
	const auto sound_buffer_s = reinterpret_cast<INT16*>(sound_buffer_extra);
	unsigned SleepTime;
	unsigned int SmplPFrame;
	FILE *hFile = nullptr;

	stop_thread = 0;
	if(!load_settings())
		goto ThreadExit;

	#ifdef DEBUG_LOG_MSGS
	//hFile = fopen(LOGFILE_PATH_LOG, "at");
	hFile = _tfopen(LogPath, _T("at"));
	if(hFile != nullptr)
		fprintf(hFile, "Thread opened\n");
	/*{
		TCHAR msgStr[MAX_PATH];
		_stprintf_s(msgStr, _T("Logfile Handle: %p (%s)"), hFile, LogPath);
		MessageBox(nullptr, msgStr, _T("MD Midi Driver"), MB_ICONINFORMATION);
	}*/
	#else
	hFile = nullptr;
	#endif

	if(sound_driver == nullptr) {
		sound_drv_mode = LoadSoundDriver(&FrameCnt, &SmplPFrame, hFile);
		if(sound_drv_mode == SND_DRV_FAIL)
			goto ThreadExit;
		SmplPFrame &= ~0x01; // must be divisible by 2 to work correctly
	}

	if(hFile != nullptr)
		fprintf(hFile, "Initializing emulation (sample rate %u) ...\n", sample_rate);
	RetVal = InitChips(0x01, sample_rate);
	if(RetVal) {
		DeinitChips();
		goto ThreadExit;
	}

	if(hFile != nullptr)
		fprintf(hFile, "Initializing sound engine ...\n");
	InitEngine();
	if(hFile != nullptr)
		fprintf(hFile, "Loading sound engine files ...\n");
	if(!LoadDriverData(hFile)) {
		if(hFile != nullptr)
			fprintd(hFile, "Failed to load Driver Data.\n");
		goto ThreadExit;
	}

	if(hFile != nullptr)
		fprintd(hFile, "Doing initial buffer fill ...\n"), fflush(hFile);
	if(sound_out_float) {
		for(unsigned i        = 0; i < SmplPFrame; i++)
			sound_buffer_f[i] = 0.0f;
		for(unsigned i = 0; i < FrameCnt; i++)
			sound_driver->write_frame(sound_buffer_f, SmplPFrame, true);
	} else {
		for(unsigned i        = 0; i < SmplPFrame; i++)
			sound_buffer_s[i] = 0;
		for(unsigned i = 0; i < FrameCnt; i++)
			sound_driver->write_frame(sound_buffer_s, SmplPFrame, true);
	}

	SetEvent(load_sfevent);
	opend          = 1;
	reset_synth[0] = 1;
	//reset_synth[1] = 1;
	if(hFile != nullptr)
		fprintd(hFile, "Driver Running.\n"), fflush(hFile);

	#ifdef DEBUG_LOG_MID_DATA
	if(hLogMid != nullptr) {
		fseek(hLogMid, 0x0C, SEEK_SET);
		const unsigned i = (sample_rate + SmplPFrame / 2) / SmplPFrame;
		PutBE16(i, hLogMid);
		fseek(hLogMid, 0x00, SEEK_END);
	}
	#endif
	SentBufCount = 0;

	while(!stop_thread) {
		INT32 sound_buffer[SAMPLES_PER_FRAME_MAX];
		if(reset_synth[0] != 0) {
			#ifdef DEBUG_LOG_MSGS
			if(hFile != nullptr)
				fprintd(hFile, "Thread reset\n");
			#endif
			SentBufCount = 0;

			reset_synth[0] = 0;
			//load_settings();

			ResetChips();
			StartEngine();
		}
		/*if (reset_synth[1] != 0){
			reset_synth[1] = 0;
			load_settings();
		}*/

		bmsyn_play_some_data(hFile);
		#ifdef DEBUG_LOG_MID_DATA
		MidLogDelay++;
		#endif
		FillBuffer32((WAVE_32BS*) sound_buffer, SmplPFrame / 2);
		SleepTime = 0;
		while(!stop_thread && !sound_driver->can_write(SmplPFrame)) {
			Sleep(1);
			SleepTime++;
		}
		if(stop_thread)
			break;

		if(sound_out_float) {
			for(unsigned i = 0; i < SmplPFrame; i++) {
				float sample = sound_buffer[i] / 4194304.0f;
				sample *= sound_out_volume_float;
				sound_buffer_f[i] = sample;
			}

			if(SentBufCount < FrameCnt * 1 && hFile != nullptr) {
				fprintd(
					hFile,
					"Time %u (slept %u): Write %u Samples (f) ...",
					GetTickCount(),
					SleepTime,
					SmplPFrame
				);
				sound_driver->write_frame(sound_buffer_f, SmplPFrame, false);
				fprintd(hFile, "Done\n");
			} else {
				sound_driver->write_frame(sound_buffer_f, SmplPFrame, false);
			}
		} else {
			for(unsigned i = 0; i < SmplPFrame; i++) {
				int sample = sound_buffer[i];
				sample     = (sample >> 3) * sound_out_volume_int >> 12;
				//sample = ((i/(SmplPFrame/8))%2) * 0x0800;		// write test value
				if(sample + 0x8000 & 0xFFFF0000)
					sample = 0x7FFF ^ sample >> 31;
				sound_buffer_s[i] = sample;
			}

			if(SentBufCount < FrameCnt * 1 && hFile != nullptr) {
				fprintd(
					hFile,
					"Time %u (slept %u): Write %u Samples (i) ...",
					GetTickCount(),
					SleepTime,
					SmplPFrame
				);
				sound_driver->write_frame(sound_buffer_s, SmplPFrame, false);
				fprintd(hFile, "Done\n");
			} else {
				sound_driver->write_frame(sound_buffer_s, SmplPFrame, false);
			}
		}
		SentBufCount++;
	}
	if(hFile != nullptr)
		fprintd(hFile, "Thread stop requested, sent buffer count: %u\n", SentBufCount);

	FreeGYBFile();
	FreePSGEnvelopes();
	FreeDACData();

	DeinitChips();
	if(hFile != nullptr)
		fprintd(hFile, "Chip Deinit finished.\n");

ThreadExit:
	stop_thread = 1;
	if(load_sfevent != nullptr) {
		//fprintd(hFile, "Thread exit - event: %p\n", load_sfevent);
		SetEvent(load_sfevent);
		if(hFile != nullptr)
			fprintd(hFile, "Event passed\n");
	}
	if(hFile != nullptr)
		fflush(hFile);

	if(sound_driver != nullptr) {
		delete sound_driver;
		sound_driver = nullptr;
	}
	if(hFile != nullptr)
		fprintd(hFile, "Sound driver deleted\n");
	#ifdef DEBUG_LOG_MSGS
	if(hFile != nullptr) {
		fprintd(hFile, "Thread closed\n");
		fclose(hFile);
	}
	#endif
	_endthreadex(0);

	return 0;
}

void DoCallback(int driverNum, int clientNum, DWORD msg, DWORD_PTR param1, DWORD_PTR param2) {
	const Driver_Client *client = &drivers[driverNum].clients[clientNum];

	DriverCallback(
		client->callback,
		client->flags,
		drivers[driverNum].hdrvr,
		msg,
		client->instance,
		param1,
		param2
	);
}

void DoStartClient() {
	if(modm_closed) {
		DWORD result;
		unsigned int thrdaddr;

		#ifdef DEBUG_LOG_MSGS
		//hLogFile = fopen(LOGFILE_PATH_LOG, "at");
		hLogFile = _tfopen(LogPath, _T("at"));
		if(hLogFile != nullptr) {
			fprintf(hLogFile, "DoStartClient Start\n");
			fclose(hLogFile);
		}
		#endif
		#ifdef DEBUG_LOG_MID_DATA
		//hLogMid = fopen(LOGFILE_PATH_MID, "wb");
		hLogMid = _tfopen(MidLogPath, _T("wb"));
		if(hLogMid != nullptr) {
			fwrite(MidiHeader, 0x01, sizeof(MidiHeader), hLogMid);
			PutBE32(-1, hLogMid);
		}
		#endif

		InitializeCriticalSection(&mim_section);
		processPriority = GetPriorityClass(GetCurrentProcess());
		SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);

		//load_sfevent = CreateEvent(nullptr, TRUE, FALSE, TEXT("MDMidLoadedEvent"));
		load_sfevent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		hCalcThread  = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, threadfunc, nullptr, 0, &thrdaddr));

		SetPriorityClass(hCalcThread, REALTIME_PRIORITY_CLASS);
		SetThreadPriority(hCalcThread, THREAD_PRIORITY_TIME_CRITICAL);

		result = WaitForSingleObject(load_sfevent, INFINITE);
		if(result == WAIT_OBJECT_0) {
			CloseHandle(load_sfevent);
			load_sfevent = nullptr;
		}
		modm_closed = 0;

		#ifdef DEBUG_LOG_MSGS
		//hLogFile = fopen(LOGFILE_PATH_LOG, "at");
		hLogFile = _tfopen(LogPath, _T("at"));
		if(hLogFile != nullptr) {
			fprintf(hLogFile, "DoStartClient OK\n");
			fclose(hLogFile);
		}
		#endif
	}
}

void DoStopClient() {
	if(!modm_closed) {
		DWORD wait_result;

		#ifdef DEBUG_LOG_MSGS
		//hLogFile = fopen(LOGFILE_PATH_LOG, "at");
		hLogFile = _tfopen(LogPath, _T("at"));
		if(hLogFile != nullptr) {
			fprintf(hLogFile, "DoStopClient Start\n");
			fclose(hLogFile);
		}
		#endif
		stop_thread = 1;

		if(sound_drv_mode != SND_DRV_WINMM) {
			wait_result = WaitForSingleObject(hCalcThread, INFINITE);
			if(wait_result == WAIT_OBJECT_0) {
				BOOL close_result = CloseHandle(hCalcThread);
				hCalcThread       = nullptr;
			}
		} else {
			// WinMM can deadlock, so I'll use a less safe approach here.
			// (Under Windows 2000, waveOutClose waits until the modMessage callback is done.)
			load_sfevent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			wait_result  = WaitForSingleObject(load_sfevent, INFINITE);
			if(wait_result == WAIT_OBJECT_0) {
				CloseHandle(load_sfevent);
				load_sfevent = nullptr;
			}
		}
		modm_closed = 1;
		SetPriorityClass(GetCurrentProcess(), processPriority);

		#ifdef DEBUG_LOG_MID_DATA
		if(hLogMid != nullptr) {
			PutMidiDelay(MidLogDelay, hLogMid);
			MidLogDelay = 0;
			fputc(0xFF, hLogMid);
			fputc(0x2F, hLogMid);
			fputc(0x00, hLogMid);

			wait_result = ftell(hLogMid) - (sizeof(MidiHeader) + 0x04);
			fseek(hLogMid, sizeof(MidiHeader), SEEK_SET);
			PutBE32(wait_result, hLogMid);

			fclose(hLogMid);
			hLogMid = nullptr;
		}
		#endif
		#ifdef DEBUG_LOG_MSGS
		//hLogFile = fopen(LOGFILE_PATH_LOG, "at");
		hLogFile = _tfopen(LogPath, _T("at"));
		if(hLogFile != nullptr) {
			fprintf(hLogFile, "DoStopClient OK\n");
			fclose(hLogFile);
		}
		#endif
	}
	DeleteCriticalSection(&mim_section);
}

void DoResetClient(UINT uDeviceID) {
	/*
	TODO : If the driver's output queue contains any output buffers (see MODM_LONGDATA) whose contents
have not been sent to the kernel-mode driver, the driver should set the MHDR_DONE flag and
clear the MHDR_INQUEUE flag in each buffer's MIDIHDR structure, and then send the client a
MOM_DONE callback message for each buffer.
	*/
	//reset_synth[!!uDeviceID] = 1;
	reset_synth[0] = 1;
}

LONG DoOpenClient(Driver *driver, UINT uDeviceID, LONG *dwUser, MIDIOPENDESC *desc, DWORD flags) {
	/*	For the MODM_OPEN message, dwUser is an output parameter.
	The driver creates the instance identifier and returns it in the address specified as
	the argument. The argument is the instance identifier.
	CALLBACK_EVENT Indicates dwCallback member of MIDIOPENDESC is an event handle.
	CALLBACK_FUNCTION Indicates dwCallback member of MIDIOPENDESC is the address of a callback function.
	CALLBACK_TASK Indicates dwCallback member of MIDIOPENDESC is a task handle.
	CALLBACK_WINDOW Indicates dwCallback member of MIDIOPENDESC is a window handle.
	*/
	int clientNum;
	if(driver->clientCount == 0) {
		//TODO: Part of this might be done in DoDriverOpen instead.
		DoStartClient();
		//DoResetClient(uDeviceID);
		clientNum = 0;
		if(stop_thread)
			return MMSYSERR_INVALPARAM;
	} else if(driver->clientCount == MAX_CLIENTS) {
		return MMSYSERR_ALLOCATED;
	} else {
		int i;
		for(i = 0; i < MAX_CLIENTS; i++) {
			if(!driver->clients[i].allocated)
				break;
		}
		if(i == MAX_CLIENTS)
			return MMSYSERR_ALLOCATED;
		clientNum = i;
	}
	driver->clients[clientNum].allocated = 1;
	driver->clients[clientNum].flags     = HIWORD(flags);
	driver->clients[clientNum].callback  = desc->dwCallback;
	driver->clients[clientNum].instance  = desc->dwInstance;
	*dwUser                              = clientNum;
	driver->clientCount++;
	SetPriorityClass(GetCurrentProcess(), processPriority);
	//TODO: desc and flags

	DoCallback(uDeviceID, clientNum, MOM_OPEN, 0, 0);
	return MMSYSERR_NOERROR;
}

LONG DoCloseClient(Driver *driver, UINT uDeviceID, LONG dwUser) {
	/*
	If the client has passed data buffers to the user-mode driver by means of MODM_LONGDATA
	messages, and if the user-mode driver hasn't finished sending the data to the kernel-mode driver,
	the user-mode driver should return MIDIERR_STILLPLAYING in response to MODM_CLOSE.
	After the driver closes the device instance it should send a MOM_CLOSE callback message to
	the client.
	*/

	if(!driver->clients[dwUser].allocated)
		return MMSYSERR_INVALPARAM;

	driver->clients[dwUser].allocated = 0;
	driver->clientCount--;
	if(driver->clientCount <= 0) {
		//DoResetClient(uDeviceID);
		DoStopClient();
		driver->clientCount = 0;
	}
	DoCallback(uDeviceID, dwUser, MOM_CLOSE, 0, 0);
	return MMSYSERR_NOERROR;
}

/* Audio Device Messages for MIDI http://msdn.microsoft.com/en-us/library/ff536194%28v=vs.85%29 */
STDAPI_(DWORD) modMessage(UINT uDeviceID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
	MIDIHDR *IIMidiHdr;
	UINT evbpoint;
	struct Driver *driver      = &drivers[uDeviceID];
	int exlen                  = 0;
	unsigned char *sysexbuffer = nullptr;
	DWORD result               = 0;

	switch(uMsg) {
		case MODM_OPEN: return DoOpenClient(driver, uDeviceID, reinterpret_cast<LONG*>(dwUser), reinterpret_cast<MIDIOPENDESC*>(dwParam1), dwParam2);
		case MODM_PREPARE:
		/*If the driver returns MMSYSERR_NOTSUPPORTED, winmm.dll prepares the buffer for use. For
most drivers, this behavior is sufficient.*/
		case MODM_UNPREPARE: return MMSYSERR_NOTSUPPORTED;
		case MODM_GETNUMDEVS: return 0x01;
		case MODM_GETDEVCAPS: return modGetCaps(uDeviceID, reinterpret_cast<MIDIOUTCAPS*>(dwParam1), dwParam2);
		case MODM_LONGDATA: IIMidiHdr = (MIDIHDR*) dwParam1;
			if(!(IIMidiHdr->dwFlags & MHDR_PREPARED))
				return MIDIERR_UNPREPARED;
			IIMidiHdr->dwFlags &= ~MHDR_DONE;
			IIMidiHdr->dwFlags |= MHDR_INQUEUE;
			exlen       = static_cast<int>(IIMidiHdr->dwBufferLength);
			sysexbuffer = static_cast<unsigned char*>(malloc(exlen * sizeof(char)));
			if(sysexbuffer == nullptr)
				return MMSYSERR_NOMEM;

			memcpy(sysexbuffer, IIMidiHdr->lpData, exlen);
			#ifdef DEBUG
		FILE * logfile;
		logfile = fopen("d:\\dbglog.log","at");
		if(logfile!=nullptr) {
			fprintf(logfile,"sysex %d byete\n", exlen);
			for(int i = 0 ; i < exlen ; i++)
				fprintf(logfile,"%x ", sysexbuffer[i]);
			fprintf(logfile,"\n");
		}
		fclose(logfile);
			#endif
		/*
		TODO: 	When the buffer contents have been sent, the driver should set the MHDR_DONE flag, clear the
			MHDR_INQUEUE flag, and send the client a MOM_DONE callback message.


			In other words, these three lines should be done when the evbuf[evbpoint] is sent.
		*/
			IIMidiHdr->dwFlags &= ~MHDR_INQUEUE;
			IIMidiHdr->dwFlags |= MHDR_DONE;
			DoCallback(uDeviceID, static_cast<LONG>(dwUser), MOM_DONE, dwParam1, 0);
		// fallthrough
		case MODM_DATA: EnterCriticalSection(&mim_section);
			evbpoint = evbwpoint;
			if(++evbwpoint >= EVBUFF_SIZE)
				evbwpoint -= EVBUFF_SIZE;
			evbuf[evbpoint].uDeviceID   = !!uDeviceID;
			evbuf[evbpoint].uMsg        = uMsg;
			evbuf[evbpoint].dwParam1    = dwParam1;
			evbuf[evbpoint].dwParam2    = dwParam2;
			evbuf[evbpoint].exlen       = exlen;
			evbuf[evbpoint].sysexbuffer = sysexbuffer;
			LeaveCriticalSection(&mim_section);
			if(InterlockedIncrement((LONG*) &evbcount) >= EVBUFF_SIZE) {
				// I don't want to hang the program in case the thread hangs for whatever reason.
				/*do
				{
					Sleep(1);
				} while (evbcount >= EVBUFF_SIZE);*/
				evbcount -= EVBUFF_SIZE;
			}
			return MMSYSERR_NOERROR;
		case MODM_GETVOLUME: *reinterpret_cast<LONG*>(dwParam1) = static_cast<LONG>(sound_out_volume_float * 0xFFFF);
			return MMSYSERR_NOERROR;
		case MODM_SETVOLUME: sound_out_volume_float = LOWORD(dwParam1) / static_cast<float>(0xFFFF);
			sound_out_volume_int = static_cast<int>(sound_out_volume_float * static_cast<float>(0x100));
			return MMSYSERR_NOERROR;
		case MODM_RESET: DoResetClient(uDeviceID);
			return MMSYSERR_NOERROR;
		/*
			MODM_GETPOS
			MODM_PAUSE
			//The driver must halt MIDI playback in the current position. The driver must then turn off all notes that are currently on.
			MODM_RESTART
			//The MIDI output device driver must restart MIDI playback at the current position.
		   // playback will start on the first MODM_RESTART message that is received regardless of the number of MODM_PAUSE that messages were received.
		   //Likewise, MODM_RESTART messages that are received while the driver is already in play mode must be ignored. MMSYSERR_NOERROR must be returned in either case
			MODM_STOP
			//Like reset, without resetting.
			MODM_PROPERTIES
			MODM_STRMDATA
		*/

		case MODM_CLOSE: return DoCloseClient(driver, uDeviceID, static_cast<LONG>(dwUser));

		/*
			MODM_CACHEDRUMPATCHES
			MODM_CACHEPATCHES
		*/

		default: return MMSYSERR_NOERROR;
	}
}
