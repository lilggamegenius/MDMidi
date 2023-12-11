#define STRICT
//#ifndef _WIN32_WINNT
//#define _WIN32_WINNT _WIN32_WINNT_WINXP
//#endif

#include "sound_out.h"

//#define HAVE_KS_HEADERS

#include <cassert>
#include <cstdint>
#include <mmdeviceapi.h>
#include <vector>
#include <windows.h>
#include <XAudio2.h>
#ifdef HAVE_KS_HEADERS
#include <ks.h>
#include <ksmedia.h>
#endif

#pragma comment ( lib, "winmm.lib" )

class sound_out_i_xaudio2;

void xaudio2_device_changed(sound_out_i_xaudio2 *);

class XAudio2_Device_Notifier final : public IMMNotificationClient {
	volatile LONG registered;
	IMMDeviceEnumerator *pEnumerator;

	CRITICAL_SECTION lock;
	std::vector<sound_out_i_xaudio2*> instances;

public:
	XAudio2_Device_Notifier() : registered(0) {
		InitializeCriticalSection(&lock);
	}

	~XAudio2_Device_Notifier() {
		DeleteCriticalSection(&lock);
	}

	ULONG STDMETHODCALLTYPE AddRef() override {
		return 1;
	}

	ULONG STDMETHODCALLTYPE Release() override {
		return 1;
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, VOID **ppvInterface) override {
		if(IID_IUnknown == riid) {
			*ppvInterface = static_cast<IUnknown*>(this);
		} else if(__uuidof(IMMNotificationClient) == riid) {
			*ppvInterface = static_cast<IMMNotificationClient*>(this);
		} else {
			*ppvInterface = nullptr;
			return E_NOINTERFACE;
		}
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDeviceId) override {
		if(flow == eRender) {
			EnterCriticalSection(&lock);
			for(auto it = instances.begin(); it < instances.end(); ++it) {
				xaudio2_device_changed(*it);
			}
			LeaveCriticalSection(&lock);
		}

		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR pwstrDeviceId) override {
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId) override {
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) override {
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) override {
		return S_OK;
	}

	void do_register(sound_out_i_xaudio2 *p_instance) {
		if(InterlockedIncrement(&registered) == 1) {
			pEnumerator      = nullptr;
			const HRESULT hr = CoCreateInstance(
				__uuidof(MMDeviceEnumerator),
				nullptr,
				CLSCTX_INPROC_SERVER,
				__uuidof(IMMDeviceEnumerator),
				reinterpret_cast<void**>(&pEnumerator)
			);
			if(SUCCEEDED(hr)) {
				pEnumerator->RegisterEndpointNotificationCallback(this);
				registered = true;
			}
		}

		EnterCriticalSection(&lock);
		instances.push_back(p_instance);
		LeaveCriticalSection(&lock);
	}

	void do_unregister(const sound_out_i_xaudio2 *p_instance) {
		if(InterlockedDecrement(&registered) == 0) {
			if(pEnumerator) {
				pEnumerator->UnregisterEndpointNotificationCallback(this);
				pEnumerator->Release();
				pEnumerator = nullptr;
			}
			registered = false;
		}

		EnterCriticalSection(&lock);
		for(auto it = instances.begin(); it < instances.end(); ++it) {
			if(*it == p_instance) {
				instances.erase(it);
				break;
			}
		}
		LeaveCriticalSection(&lock);
	}
} g_notifier;

class sound_out_i_xaudio2 : public sound_out {
	class XAudio2_BufferNotify final : public IXAudio2VoiceCallback {
	public:
		HANDLE hBufferEndEvent;

		XAudio2_BufferNotify() {
			hBufferEndEvent = nullptr;
			hBufferEndEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			assert(hBufferEndEvent != nullptr);
		}

		~XAudio2_BufferNotify() {
			CloseHandle(hBufferEndEvent);
			hBufferEndEvent = nullptr;
		}

		STDMETHOD_(void, OnBufferEnd)(void *pBufferContext) {
			assert(hBufferEndEvent != nullptr);
			SetEvent(hBufferEndEvent);
			auto *psnd = static_cast<sound_out_i_xaudio2*>(pBufferContext);
			if(psnd)
				psnd->OnBufferEnd();
		}

		// dummies:
		STDMETHOD_(void, OnVoiceProcessingPassStart)(UINT32 BytesRequired) {}

		STDMETHOD_(void, OnVoiceProcessingPassEnd)() {}

		STDMETHOD_(void, OnStreamEnd)() {}

		STDMETHOD_(void, OnBufferStart)(void *pBufferContext) {}

		STDMETHOD_(void, OnLoopEnd)(void *pBufferContext) {}

		STDMETHOD_(void, OnVoiceError)(void *pBufferContext, HRESULT Error) {}
	};

	void OnBufferEnd() {
		InterlockedDecrement(&buffered_count);
		const LONG buffer_read_cursor = this->buffer_read_cursor;
		samples_played += samples_in_buffer[buffer_read_cursor];
		this->buffer_read_cursor = (buffer_read_cursor + 1) % num_frames;
	}

	bool com_initialized;
	void *hwnd;
	bool paused;
	volatile bool device_changed;
	unsigned reopen_count;
	unsigned sample_rate, bytes_per_sample, max_samples_per_frame, num_frames;
	unsigned short nch;
	volatile LONG buffered_count;
	volatile LONG buffer_read_cursor;
	LONG buffer_write_cursor;

	volatile UINT64 samples_played;

	uint8_t *sample_buffer;
	UINT64 *samples_in_buffer;

	IXAudio2 *xaud;
	IXAudio2MasteringVoice *mVoice; // listener
	IXAudio2SourceVoice *sVoice;    // sound source
	XAUDIO2_VOICE_STATE vState;
	XAudio2_BufferNotify notify; // buffer end notification
public:
	sound_out_i_xaudio2() {
		com_initialized = false;
		paused          = false;
		reopen_count    = 0;
		buffered_count  = 0;
		device_changed  = false;

		xaud          = nullptr;
		mVoice        = nullptr;
		sVoice        = nullptr;
		sample_buffer = nullptr;
		ZeroMemory(&vState, sizeof( vState ));

		if(!com_initialized) {
			const HRESULT hRes = CoInitialize(nullptr);
			com_initialized    = SUCCEEDED(hRes);
		}
		g_notifier.do_register(this);
	}

	~sound_out_i_xaudio2() override {
		g_notifier.do_unregister(this);
		if(com_initialized) {
			CoUninitialize();
			com_initialized = false;
		}

		close();
	}

	void OnDeviceChanged() {
		device_changed = true;
	}

	const char *open(void *hwnd, unsigned sample_rate, unsigned short nch, bool floating_point, unsigned max_samples_per_frame, unsigned num_frames) override {
		this->hwnd                  = hwnd;
		this->sample_rate           = sample_rate;
		this->nch                   = nch;
		this->max_samples_per_frame = max_samples_per_frame;
		this->num_frames            = num_frames;
		bytes_per_sample            = floating_point ? 4 : 2;

		#ifdef HAVE_KS_HEADERS
		WAVEFORMATEXTENSIBLE wfx;
		wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
		wfx.Format.nChannels = nch; //1;
		wfx.Format.nSamplesPerSec = sample_rate;
		wfx.Format.nBlockAlign = bytes_per_sample * nch; //2;
		wfx.Format.nAvgBytesPerSec = wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign;
		wfx.Format.wBitsPerSample = floating_point ? 32 : 16;
		wfx.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE)-sizeof(WAVEFORMATEX);
		wfx.Samples.wValidBitsPerSample = wfx.Format.wBitsPerSample;
		wfx.SubFormat = floating_point ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : KSDATAFORMAT_SUBTYPE_PCM;
		wfx.dwChannelMask = nch == 2 ? KSAUDIO_SPEAKER_STEREO : KSAUDIO_SPEAKER_MONO;
		#else
		WAVEFORMATEX wfx;
		wfx.wFormatTag      = floating_point ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
		wfx.nChannels       = nch; //1;
		wfx.nSamplesPerSec  = sample_rate;
		wfx.nBlockAlign     = bytes_per_sample * nch; //2;
		wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
		wfx.wBitsPerSample  = floating_point ? 32 : 16;
		wfx.cbSize          = 0;
		#endif
		HRESULT hr = XAudio2Create(&xaud, 0, XAUDIO2_DEFAULT_PROCESSOR);
		if(FAILED(hr))
			return "Creating XAudio2 interface";
		hr = xaud->CreateMasteringVoice(
			&mVoice,
			nch,
			sample_rate,
			0,
			nullptr,
			nullptr
		);
		if(FAILED(hr)) {
			return "Creating XAudio2 mastering voice";
		}
		hr = xaud->CreateSourceVoice(&sVoice, &wfx, 0, 4.0f, &notify);
		if(FAILED(hr)) {
			return "Creating XAudio2 source voice";
		}
		hr = sVoice->Start(0);
		if(FAILED(hr)) {
			return "Starting XAudio2 voice";
		}
		hr = sVoice->SetFrequencyRatio(1.0f);
		if(FAILED(hr)) {
			return "Setting XAudio2 voice frequency ratio";
		}
		device_changed      = false;
		buffered_count      = 0;
		buffer_read_cursor  = 0;
		buffer_write_cursor = 0;
		samples_played      = 0;
		sample_buffer       = new uint8_t[max_samples_per_frame * num_frames * bytes_per_sample];
		samples_in_buffer   = new UINT64[num_frames];
		memset(samples_in_buffer, 0, sizeof(UINT64) * num_frames);
		return nullptr;
	}

	void close() {
		if(sVoice) {
			if(!paused) {
				sVoice->Stop(0);
			}
			sVoice->DestroyVoice();
			sVoice = nullptr;
		}

		if(mVoice) {
			mVoice->DestroyVoice();
			mVoice = nullptr;
		}

		if(xaud) {
			xaud->Release();
			xaud = nullptr;
		}

		delete [] sample_buffer;
		sample_buffer = nullptr;
		delete [] samples_in_buffer;
		samples_in_buffer = nullptr;
	}

	const char *write_frame(void *buffer, unsigned num_samples, bool wait) override {
		if(device_changed) {
			close();
			reopen_count   = 5;
			device_changed = false;
			return nullptr;
		}

		if(paused) {
			if(wait)
				Sleep(MulDiv(num_samples / nch, 1000, sample_rate));
			return nullptr;
		}

		if(reopen_count) {
			if(!--reopen_count) {
				const char *err = open(hwnd, sample_rate, nch, bytes_per_sample == 4, max_samples_per_frame, num_frames);
				if(err) {
					reopen_count = 60 * 5;
					return err;
				}
			} else {
				if(wait)
					Sleep(MulDiv(num_samples / nch, 1000, sample_rate));
				return nullptr;
			}
		}

		for(;;) {
			sVoice->GetState(&vState);
			assert(vState.BuffersQueued <= num_frames);
			if(vState.BuffersQueued < num_frames) {
				if(vState.BuffersQueued == 0) {
					// buffers ran dry
				}
				// there is at least one free buffer
				break;
			}
			// wait for one buffer to finish playing
			const DWORD timeout_ms = max_samples_per_frame / nch * num_frames * 1000 / sample_rate;
			if(WaitForSingleObject(notify.hBufferEndEvent, timeout_ms) == WAIT_TIMEOUT) {
				// buffer has stalled, likely by the whole XAudio2 system failing, so we should tear it down and attempt to reopen it
				close();
				reopen_count = 5;

				return nullptr;
			}
		}
		samples_in_buffer[buffer_write_cursor] = num_samples / nch;
		XAUDIO2_BUFFER buf                     = {0};
		const unsigned num_bytes               = num_samples * bytes_per_sample;
		buf.AudioBytes                         = num_bytes;
		buf.pAudioData                         = sample_buffer + max_samples_per_frame * buffer_write_cursor * bytes_per_sample;
		buf.pContext                           = this;
		buffer_write_cursor                    = (buffer_write_cursor + 1) % num_frames;
		memcpy((void*) buf.pAudioData, buffer, num_bytes);
		if(sVoice->SubmitSourceBuffer(&buf) == S_OK) {
			InterlockedIncrement(&buffered_count);
			return nullptr;
		}

		close();
		reopen_count = 60 * 5;

		return nullptr;
	}

	const char *pause(bool pausing) override {
		if(pausing) {
			if(!paused) {
				paused = true;
				if(!reopen_count) {
					if(const HRESULT hr = sVoice->Stop(0); FAILED(hr)) {
						close();
						reopen_count = 60 * 5;
					}
				}
			}
		} else {
			if(paused) {
				paused = false;
				if(!reopen_count) {
					if(const HRESULT hr = sVoice->Start(0); FAILED(hr)) {
						close();
						reopen_count = 60 * 5;
					}
				}
			}
		}

		return nullptr;
	}

	bool can_write(unsigned num_samples) override {
		return true;
	}

	const char *set_ratio(double ratio) override {
		if(!reopen_count && FAILED(sVoice->SetFrequencyRatio( static_cast<float>(ratio) )))
			return "setting ratio";
		return nullptr;
	}

	double buffered() override {
		if(reopen_count)
			return 0.0;
		sVoice->GetState(&vState);
		double buffered_count      = vState.BuffersQueued;
		const INT64 samples_played = vState.SamplesPlayed - this->samples_played;
		buffered_count -= static_cast<double>(samples_played) / static_cast<double>(max_samples_per_frame / nch);
		return buffered_count;
	}
};

void xaudio2_device_changed(sound_out_i_xaudio2 *p_instance) {
	p_instance->OnDeviceChanged();
}

sound_out *create_sound_out_xaudio2() {
	return new sound_out_i_xaudio2;
}
