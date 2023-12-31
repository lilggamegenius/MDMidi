#define STRICT
#ifndef _WIN32_WINNT
#define _WIN32_WINNT _WIN32_WINNT_WINXP
#endif

#include <windows.h>

#include <InitGuid.h>

//#define HAVE_KS_HEADERS

#include <mmsystem.h>
#include <dsound.h>
#include <mmreg.h>
#include <cassert>
#include <cmath>
#include <vector>

#ifdef HAVE_KS_HEADERS

#include <ks.h>
#include <ksmedia.h>

#endif

#include "ds_stream.h"

class critical_section {
private:
	CRITICAL_SECTION sec{};

public:
	void enter() noexcept {
		EnterCriticalSection(&sec);
	}

	void leave() noexcept {
		LeaveCriticalSection(&sec);
	}

	critical_section() {
		InitializeCriticalSection(&sec);
	}

	~critical_section() {
		DeleteCriticalSection(&sec);
	}

private:
	critical_section(const critical_section &) = default;

	critical_section &operator=(const critical_section &) = default;
};

class c_insync {
private:
	critical_section &m_section;

public:
	explicit c_insync(critical_section *p_section) noexcept : m_section(*p_section) {
		m_section.enter();
	}

	explicit c_insync(critical_section &p_section) noexcept : m_section(p_section) {
		m_section.enter();
	}

	~c_insync() noexcept {
		m_section.leave();
	}
};

#define insync(X) c_insync blah____sync(X)

namespace {
	class circular_buffer {
		std::vector<char> buffer;
		unsigned readptr, writeptr, used, size;

	public:
		explicit circular_buffer(unsigned p_size) : buffer(p_size)
												  , readptr(0)
												  , writeptr(0)
												  , size(p_size)
												  , used(0) {}

		[[nodiscard]] unsigned data_available() const {
			return used;
		}

		[[nodiscard]] unsigned free_space() const {
			return size - used;
		}

		bool write(const void *src, unsigned bytes) {
			if(bytes > free_space())
				return false;
			auto srcptr = static_cast<const char*>(src);
			while(bytes) {
				unsigned delta = size - writeptr;
				if(delta > bytes)
					delta = bytes;
				memcpy(&buffer.at(0) + writeptr, srcptr, delta);
				used += delta;
				writeptr = (writeptr + delta) % size;
				srcptr += delta;
				bytes -= delta;
			}
			return true;
		}

		unsigned read(void *dst, unsigned bytes) {
			unsigned done = 0;
			auto dstptr   = static_cast<char*>(dst);
			for(;;) {
				unsigned delta = size - readptr;
				if(delta > used)
					delta = used;
				if(delta > bytes)
					delta = bytes;
				if(delta == 0)
					break;

				memcpy(dstptr, &buffer.at(0) + readptr, delta);
				dstptr += delta;
				done += delta;
				readptr = (readptr + delta) % size;
				bytes -= delta;
				used -= delta;
			}
			return done;
		}

		void reset() {
			readptr = writeptr = used = 0;
		}
	};
}

class ds_stream_i;

class ds_api_i final : public ds_api {
	HANDLE g_thread;
	DWORD g_thread_id;

	GUID device;
	bool b_have_device;
	HWND appwindow;

	static DWORD WINAPI _g_thread_proc(void *param) {
		static_cast<ds_api_i*>(param)->g_thread_proc();
		return 0;
	}

	void g_thread_proc();

	void g_cleanup_thread();

	void g_thread_sleep() const {
		assert(g_is_update_thread());
		SleepEx(10,TRUE);
	}

public:
	std::vector<ds_stream_i*> g_streams;
	IDirectSound *g_p_ds;

	bool g_initialized;

	[[nodiscard]] bool g_is_update_thread() const {
		return g_thread && GetCurrentThreadId() == g_thread_id;
	}

	bool g_shutting_down;

	critical_section g_sync;

	explicit ds_api_i(HWND);

	~ds_api_i() override;

	ds_stream *ds_stream_create(const ds_stream_config *cfg) override;

	void set_device(const GUID *id) override;

	void release() override;
};

class ds_stream_i : public ds_stream {
	critical_section &g_sync;

	IDirectSoundBuffer *p_dsb;
	unsigned srate;
	unsigned short nch, bps;
	circular_buffer incoming;
	bool b_error;

	int prebuffer_bytes;
	int write_min_bytes, write_max_bytes, write_max_ms;
	int buffer_size;
	int silence_delta;
	int silence_bytes;
	int last_write;
	bool paused, prebuffering, b_force_play;
	int latency_bytes;

	bool pause_request;

	double current_time;

	unsigned buffer_ms;

	[[nodiscard]] int ms2bytes(int ms) const {
		return MulDiv(ms, srate, 1000) * (bps >> 3) * nch;
	}

	[[nodiscard]] int bytes2ms(int bytes) const {
		return MulDiv(bytes, 1000, srate * (bps >> 3) * nch);
	}

	[[nodiscard]] double bytes2time(unsigned bytes) const {
		return (double) bytes / (double) (srate * (bps >> 3) * nch);
	}

	void memsil(void *ptr, int bytes) const {
		memset(ptr, bps == 8 ? 0x80 : 0, bytes);
	}

	void force_play_internal();

	void flush_internal();

	ds_api_i *api;
	bool b_release_requested;

public:
	[[nodiscard]] bool is_paused() const {
		return paused;
	}

	ds_stream_i(ds_api_i *p_api, const ds_stream_config *cfg);

	~ds_stream_i() override;
	bool write(const void *data, unsigned bytes) override;
	bool is_playing() override;
	double get_latency() override;
	unsigned get_latency_bytes() override;
	unsigned can_write_bytes() override;

	bool force_play() override {
		insync(g_sync);
		b_force_play = true;
		return !b_error;
	}

	//destructor methods
	void release() override;

	bool pause(bool status) override;

	bool set_ratio(double ratio) override;

	bool update();
};

void ds_api_i::g_cleanup_thread() {
	CloseHandle(g_thread);
	g_thread    = nullptr;
	g_thread_id = 0;
	for(auto &g_stream: g_streams)
		delete g_stream;
	g_streams.clear();
	g_p_ds->Release();
	g_p_ds = nullptr;
}

void ds_api_i::g_thread_proc() {
	SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_TIME_CRITICAL); {
		insync(g_sync);
		if(!g_initialized) {
			CoInitialize(nullptr);
			g_initialized = true;
		}
		g_p_ds = nullptr;
		if(FAILED(CoCreateInstance(CLSID_DirectSound, NULL, CLSCTX_INPROC_SERVER, IID_IDirectSound, (void**)&g_p_ds)))
			return;
		if(g_p_ds == nullptr)
			return;
		if(FAILED(g_p_ds->Initialize(b_have_device ? &device : 0)))
			return;
		if(FAILED(g_p_ds->SetCooperativeLevel(appwindow,DSSCL_PRIORITY))) {
			g_p_ds->Release();
			g_p_ds = nullptr;
			return;
		}
	}

	while(!g_shutting_down) {
		bool b_exit = false; {
			insync(g_sync);

			bool b_deleted = false; {
				for(unsigned n = 0; n < g_streams.size();) {
					//ds_stream_i* ptr = g_streams[n];
					if(g_streams[n]->update())
						n++;
					else {
						delete g_streams[n];
						g_streams.erase(g_streams.begin() + n);
						b_deleted = true;
					}
				}
			}

			if(b_deleted && g_streams.empty()) {
				b_exit = true;
				g_cleanup_thread();
			}
		}

		if(b_exit)
			break;
		g_thread_sleep();
	}
}

ds_stream_i::ds_stream_i(ds_api_i *p_api, const ds_stream_config *cfg) : srate(cfg->srate)
																	   , nch(cfg->nch)
																	   , bps(cfg->bps)
																	   , incoming(ms2bytes(250))
																	   , b_error(false)
																	   , api(p_api)
																	   , g_sync(p_api->g_sync)
																	   , buffer_ms(cfg->buffer_ms) {
	b_release_requested = false;
	prebuffer_bytes     = 0;
	write_min_bytes     = 0;
	write_max_bytes     = 0;
	write_max_ms        = 0;
	buffer_size         = 0;
	silence_delta       = 0;
	silence_bytes       = 0;
	last_write          = 0;
	prebuffering        = true;
	paused              = false;
	b_force_play        = false;
	latency_bytes       = 0;
	pause_request       = false;
	current_time        = 0;
	p_dsb               = nullptr;
}

ds_stream_i::~ds_stream_i() {
	if(p_dsb) {
		p_dsb->Stop();
		p_dsb->Release();
		p_dsb = nullptr;
	}
}

#ifdef HAVE_KS_HEADERS
static DWORD get_channel_mask(unsigned nch){
	DWORD rv;
	switch(nch)
	{
	default:
		rv = 0;
		break;
	case 1:
		rv = KSAUDIO_SPEAKER_MONO;
		break;
	case 2:
		rv = KSAUDIO_SPEAKER_STEREO;
		break;
	case 4:
		rv = KSAUDIO_SPEAKER_QUAD;
		break;
	case 6:
		rv = KSAUDIO_SPEAKER_5POINT1;
		break;
	}
	return rv;
}
#endif

bool ds_stream_i::update() {
	if(b_release_requested)
		return false;

	assert(api->g_p_ds);
	if(b_error)
		return true;
	if(p_dsb == nullptr) {
		write_max_ms             = buffer_ms;
		const int buffer_size_ms = write_max_ms > 2000 ? write_max_ms + 2000 : write_max_ms * 2;
		int prebuffer_size_ms    = write_max_ms / 2;
		if(prebuffer_size_ms > 10000)
			prebuffer_size_ms = 10000;
		prebuffer_bytes = ms2bytes(prebuffer_size_ms);

		write_min_bytes = ms2bytes(1);
		write_max_bytes = ms2bytes(write_max_ms);

		WAVEFORMATEX wfx;
		wfx.wFormatTag      = bps == 32 ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
		wfx.nChannels       = nch;
		wfx.nSamplesPerSec  = srate;
		wfx.nAvgBytesPerSec = ms2bytes(1000);
		wfx.nBlockAlign     = (bps >> 3) * nch;
		wfx.wBitsPerSample  = bps;
		wfx.cbSize          = 0;

		DSBUFFERDESC desc;
		desc.dwSize        = sizeof(desc);
		desc.dwFlags       = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_STICKYFOCUS | DSBCAPS_GLOBALFOCUS | DSBCAPS_CTRLFREQUENCY;
		desc.dwBufferBytes = buffer_size = ms2bytes(buffer_size_ms);
		desc.dwReserved    = 0;
		desc.lpwfxFormat   = &wfx;

		silence_delta = buffer_size / 4;
		if(silence_delta > (buffer_size - write_max_bytes) / 2)
			silence_delta = (buffer_size - write_max_bytes) / 2;

		if(FAILED(api->g_p_ds->CreateSoundBuffer(&desc,&p_dsb,nullptr))) {
			#ifdef HAVE_KS_HEADERS
			WAVEFORMATEXTENSIBLE wfxe;
			wfxe.Format=wfx;
			wfxe.Format.wFormatTag=WAVE_FORMAT_EXTENSIBLE;
			wfxe.Format.cbSize=22;
			wfxe.Samples.wReserved=0;

			wfxe.dwChannelMask=get_channel_mask(nch);

			wfxe.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
			desc.lpwfxFormat = &wfxe.Format;
			if (FAILED(api->g_p_ds->CreateSoundBuffer(&desc,&p_dsb,0)))
			#endif
			{
				b_error = true;
			}
		}

		if(!b_error) {
			void *p1 = nullptr, *p2 = nullptr;
			DWORD s1 = 0, s2        = 0;
			if(SUCCEEDED(p_dsb->Lock(0,0,&p1,&s1,&p2,&s2,DSBLOCK_ENTIREBUFFER))) {
				if(p1)
					memsil(p1, s1);
				if(p2)
					memsil(p2, s2);
				p_dsb->Unlock(p1, s1, p2, s2);
			}
			silence_bytes = buffer_size;
		}

		last_write   = 0;
		prebuffering = true;
	}

	if(b_error) {
		return true;
	}
	{
		DWORD foo;
		p_dsb->GetStatus(&foo);
		if(foo & DSBSTATUS_BUFFERLOST)
			if(FAILED(p_dsb->Restore())) {
				b_error = true;
				return true;
			}
		p_dsb->GetStatus(&foo);
		if(!paused && !prebuffering && (foo & (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING)) != (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING))
			p_dsb->Play(0, 0,DSBPLAY_LOOPING);
	}

	if(!paused) {
		if(p_dsb == nullptr) {
			latency_bytes = 0;
		} else if(prebuffering) {
			latency_bytes = last_write;
		} else {
			const int latency_bytes_old = latency_bytes;
			long buffer_position;
			if(FAILED(p_dsb->GetCurrentPosition(reinterpret_cast<DWORD *>(&buffer_position),nullptr)))
				buffer_position = 0;
			int bytes = last_write - buffer_position;
			if(bytes < 0)
				bytes += buffer_size;
			if(bytes > buffer_size - (buffer_size - write_max_bytes) / 2)
				bytes -= buffer_size;
			latency_bytes = bytes;
			current_time += bytes2time(latency_bytes_old - latency_bytes);
		}
	}

	if(b_force_play) {
		force_play_internal();
		b_force_play = false;
	}

	if(pause_request != paused) {
		if(!prebuffering && p_dsb) {
			if(pause_request) {
				p_dsb->Stop();
			} else {
				p_dsb->Play(0, 0,DSBPLAY_LOOPING);
			}
		}
		paused = pause_request;
	}

	if(incoming.data_available() == 0) {
		if(!paused && !prebuffering && latency_bytes <= 0) {
			flush_internal();
			return true;
		}
	}

	if(!paused)

		for(;;) {
			int write_start = last_write;
			int write_max = latency_bytes >= 0 ? write_max_bytes - latency_bytes : write_max_bytes;
			int write_delta = 0;
			int chunk_delta = incoming.data_available();

			if(!prebuffering && latency_bytes < write_min_bytes) {
				const int d = write_min_bytes - latency_bytes;
				write_start += d;
				write_delta += d;
				write_max -= d;
				latency_bytes += d;
			}

			if(chunk_delta > write_max) {
				chunk_delta = write_max;
				if(chunk_delta <= 0)
					break;
			}

			write_delta += chunk_delta; {
				void *p1 = nullptr, *p2 = nullptr;
				DWORD s1 = 0, s2        = 0;
				if(chunk_delta > 0) {
					if(SUCCEEDED(p_dsb->Lock(write_start%buffer_size,chunk_delta,&p1,&s1,&p2,&s2,0))) {
						if(p1) {
							incoming.read(p1, s1);
						}
						if(p2) {
							incoming.read(p2, s2);
						}
						p_dsb->Unlock(p1, s1, p2, s2);
						last_write = (write_start + chunk_delta) % buffer_size;
						silence_bytes -= write_delta;
						latency_bytes += write_delta;
					}
				}

				if(silence_bytes < silence_delta) {
					if(SUCCEEDED(p_dsb->Lock((last_write+silence_bytes)%buffer_size,silence_delta,&p1,&s1,&p2,&s2,0))) {
						if(p1)
							memsil(p1, s1);
						if(p2)
							memsil(p2, s2);
						p_dsb->Unlock(p1, s1, p2, s2);
						silence_bytes += silence_delta;
					}
				}
				if(prebuffering && last_write >= prebuffer_bytes)
					force_play_internal();
			}
			break;
		}
	return true;
}

void ds_stream_i::force_play_internal() {
	if(p_dsb && prebuffering && last_write > 0 && !paused) {
		prebuffering = false;
		p_dsb->Play(0, 0,DSBPLAY_LOOPING);
	}
}

double ds_stream_i::get_latency() {
	return bytes2time(get_latency_bytes());
}

unsigned ds_stream_i::get_latency_bytes() {
	insync(g_sync);
	int rv = (p_dsb ? latency_bytes : 0) + incoming.data_available();
	if(rv < 0)
		rv = 0;
	return static_cast<unsigned>(rv);
}

bool ds_stream_i::is_playing() {
	insync(g_sync);
	return p_dsb && !prebuffering;
}

bool ds_stream_i::write(const void *data, unsigned bytes) {
	bool rv; {
		insync(g_sync);
		rv = incoming.write(data, bytes);
	}
	return rv;
}

unsigned ds_stream_i::can_write_bytes() {
	insync(g_sync);
	if(b_error) {
		return 0;
	}
	unsigned rv = write_max_bytes - get_latency_bytes();
	if(static_cast<signed>(rv) < 0)
		rv = 0;
	const unsigned max = incoming.free_space();
	if(rv > max)
		rv = max;
	return rv;
}

void ds_stream_i::flush_internal() {
	if(p_dsb) {
		p_dsb->Stop();
		p_dsb->SetCurrentPosition(0); {
			void *p1 = nullptr, *p2 = nullptr;
			DWORD s1 = 0, s2        = 0;
			if(SUCCEEDED(p_dsb->Lock(0,0,&p1,&s1,&p2,&s2,DSBLOCK_ENTIREBUFFER))) {
				if(p1)
					memsil(p1, s1);
				if(p2)
					memsil(p2, s2);
				p_dsb->Unlock(p1, s1, p2, s2);
			}
			silence_bytes = buffer_size;
		}
		prebuffering = true;
	}
	last_write    = 0;
	latency_bytes = 0;
	current_time  = 0;
	incoming.reset();
}

bool ds_stream_i::pause(const bool status) {
	insync(g_sync);
	if(b_error) {
		return false;
	}
	pause_request = status;
	return true;
}

bool ds_stream_i::set_ratio(const double ratio) {
	insync(g_sync);
	if(b_error) {
		return false;
	}
	if(FAILED(p_dsb->SetFrequency( static_cast<DWORD>(srate * ratio) ))) {
		return false;
	}
	return true;
}

ds_api *ds_api_create(HWND appwindow) {
	return appwindow ? static_cast<ds_api*>(new ds_api_i(appwindow)) : nullptr;
}

void ds_api_i::set_device(const GUID *id) {
	insync(g_sync);
	if(id) {
		b_have_device = true;
		device        = *id;
	} else
		b_have_device = false;
}

ds_stream *ds_api_i::ds_stream_create(const ds_stream_config *cfg) {
	auto *ptr = new(std::nothrow) ds_stream_i(this, cfg);
	if(ptr) {
		insync(g_sync);
		g_streams.push_back(ptr);
		if(g_thread == nullptr) {
			g_thread = CreateThread(nullptr, 0, _g_thread_proc, this, 0, &g_thread_id);
		}
	}
	return ptr;
}

ds_api_i::ds_api_i(HWND p_appwindow) : device()
									 , appwindow(p_appwindow) {
	b_have_device   = false;
	g_p_ds          = nullptr;
	g_thread        = nullptr;
	g_thread_id     = 0;
	g_shutting_down = false;
	g_initialized   = false;
}

ds_api_i::~ds_api_i() {
	HANDLE blah = nullptr; {
		insync(g_sync);
		g_shutting_down = true;
		if(g_thread) {
			HANDLE h_process = GetCurrentProcess();
			DuplicateHandle(h_process, g_thread, h_process, &blah, 0,FALSE,DUPLICATE_SAME_ACCESS);
		}
	}
	if(blah) {
		WaitForSingleObject(blah,INFINITE);
		CloseHandle(blah);
	}
	insync(g_sync);
	for(auto &g_stream: g_streams) {
		delete g_stream;
	}
	g_streams.clear();

	if(g_initialized) {
		CoUninitialize();
	}
}

void ds_api_i::release() {
	delete this;
}

void ds_stream_i::release() {
	insync(g_sync);
	b_release_requested = true;
}
