/*#define STRICT
#ifndef _WIN32_WINNT
#define _WIN32_WINNT _WIN32_WINNT_WINXP
#endif*/

#include "sound_out.h"

#include <windows.h>
#include <mmreg.h>
#include <malloc.h>
#include <cassert>

#pragma comment (lib, "winmm.lib")

class sound_out_i_winmm final : public sound_out{
	HWAVEOUT hWaveOut;

	WAVEHDR* WaveHdrs;
	//unsigned char** Buffers;

	bool paused;

	unsigned int sample_rate;
	unsigned int max_samples_per_frame;
	unsigned int num_frames;
	unsigned int bytes_per_sample;
	unsigned int buffer_size_bytes;
	unsigned short nch;
	unsigned int NextBuf;

public:
	sound_out_i_winmm(){
		this->hWaveOut = nullptr;
		this->WaveHdrs = nullptr;
		//this->Buffers = nullptr;
		this->paused = false;
	}

	~sound_out_i_winmm() override{
		close();
	}

	const char* open(void* hwnd,
					 unsigned sample_rate,
					 unsigned short nch,
					 bool floating_point,
					 unsigned max_samples_per_frame,
					 unsigned num_frames) override{
		this->sample_rate = sample_rate;
		this->nch = nch;
		this->max_samples_per_frame = max_samples_per_frame;
		this->num_frames = num_frames;
		this->bytes_per_sample = floating_point ? 4 : 2;

		WAVEFORMATEX WaveFmt;
		WaveFmt.wFormatTag = floating_point ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
		WaveFmt.nChannels = nch;
		WaveFmt.nSamplesPerSec = sample_rate;
		WaveFmt.wBitsPerSample = bytes_per_sample * 8;
		WaveFmt.nBlockAlign = bytes_per_sample * WaveFmt.nChannels;
		WaveFmt.nAvgBytesPerSec = WaveFmt.nSamplesPerSec * WaveFmt.nBlockAlign;
		WaveFmt.cbSize = 0;

		MMRESULT RetVal = waveOutOpen(&this->hWaveOut, WAVE_MAPPER, &WaveFmt, 0x00, 0x00, CALLBACK_NULL);
		if (RetVal != MMSYSERR_NOERROR)
			return "Opening Wave Device";

		this->buffer_size_bytes = max_samples_per_frame * this->bytes_per_sample;
		this->WaveHdrs = new WAVEHDR[num_frames];
		//this->Buffers = new unsigned char*[num_frames];
		WAVEHDR* TempHdr;
		for (unsigned int CurBuf = 0; CurBuf < num_frames; CurBuf++) {
			TempHdr = &this->WaveHdrs[CurBuf];
			//this->Buffers[CurBuf] = new unsigned char[this->buffer_size_bytes];
			//TempHdr->lpData = (LPSTR)this->Buffers[CurBuf];
			TempHdr->lpData = reinterpret_cast<LPSTR>(new unsigned char[this->buffer_size_bytes]);
			TempHdr->dwBufferLength = this->buffer_size_bytes;
			TempHdr->dwBytesRecorded = 0x00;
			TempHdr->dwUser = 0x00;
			TempHdr->dwFlags = 0x00;
			TempHdr->dwLoops = 0x00;
			TempHdr->lpNext = nullptr;
			TempHdr->reserved = 0x00;
			RetVal = waveOutPrepareHeader(this->hWaveOut, TempHdr, sizeof(WAVEHDR));
			TempHdr->dwFlags |= WHDR_DONE;
		}

#if 1
		for (unsigned int CurBuf = 0; CurBuf < num_frames; CurBuf++) {
			TempHdr = &this->WaveHdrs[CurBuf];

			TempHdr->dwFlags &= ~WHDR_DONE;
			TempHdr->dwBufferLength = TempHdr->dwBufferLength;
			memset(TempHdr->lpData, 0x00, TempHdr->dwBufferLength);
			waveOutWrite(this->hWaveOut, TempHdr, sizeof(WAVEHDR));
		}
#endif
		NextBuf = 0;

		return nullptr;
	}

	void close(){
		if (this->hWaveOut == nullptr)
			return;

		waveOutReset(this->hWaveOut);
		for (unsigned int CurBuf = 0x00; CurBuf < this->num_frames; CurBuf++) {
			WAVEHDR* TempHdr = &this->WaveHdrs[CurBuf];

			waveOutUnprepareHeader(this->hWaveOut, TempHdr, sizeof(WAVEHDR));
			//delete [] this->Buffers[CurBuf];
			delete [] TempHdr->lpData;
		}
		delete [] this->WaveHdrs;
		this->WaveHdrs = nullptr;

		//delete [] this->Buffers;
		//this->Buffers = nullptr;

		MMRESULT RetVal = waveOutClose(this->hWaveOut);
		if (RetVal != MMSYSERR_NOERROR)
			return;
		this->hWaveOut = nullptr;
	}

	const char* write_frame(void* buffer, unsigned num_samples, bool wait) override{
		if (this->paused) {
			if (wait)
				Sleep(MulDiv(num_samples / nch, 1000, sample_rate));
			return nullptr;
		}

		const unsigned int BufWrtBytes = num_samples * this->bytes_per_sample;

		assert(BufWrtBytes <= this->buffer_size_bytes);
		WAVEHDR* TempHdr = &this->WaveHdrs[this->NextBuf];
		if (wait) {
			while (!(TempHdr->dwFlags & WHDR_DONE))
				Sleep(1);
		}
		else {
			if (!(TempHdr->dwFlags & WHDR_DONE))
				return nullptr;
		}

		TempHdr->dwFlags &= ~WHDR_DONE;
		TempHdr->dwBufferLength = BufWrtBytes;
		memcpy(TempHdr->lpData, buffer, BufWrtBytes);
		waveOutWrite(this->hWaveOut, TempHdr, sizeof(WAVEHDR));

		this->NextBuf++;
		this->NextBuf %= this->num_frames;

		return nullptr;
	}

	bool can_write(unsigned num_samples) override{
		//unsigned int buffer_size_write = num_samples * bytes_per_sample;

		return this->WaveHdrs[this->NextBuf].dwFlags & WHDR_DONE;
	}

	const char* set_ratio(double ratio) override{
		//if ( !p_stream->set_ratio( ratio ) ) return "setting ratio";
		return "Not supported";
	}

	const char* pause(bool pausing) override{
		MMRESULT RetVal;

		if (pausing)
			RetVal = waveOutPause(this->hWaveOut);
		else
			RetVal = waveOutRestart(this->hWaveOut);
		if (RetVal != MMSYSERR_NOERROR)
			return "Failed.";

		this->paused = pausing;

		return nullptr;
	}

	double buffered() override{
		const unsigned int BufBytMax = this->max_samples_per_frame * this->buffer_size_bytes;

		unsigned int BufBytes = 0;
		for (unsigned int CurBuf = 0x00; CurBuf < this->num_frames; CurBuf++) {
			if (!(this->WaveHdrs[CurBuf].dwFlags & WHDR_DONE))
				BufBytes += this->WaveHdrs[CurBuf].dwBufferLength;
		}

		return static_cast<double>(BufBytes) / static_cast<double>(BufBytMax);
	}
};

sound_out* create_sound_out_winmm(){
	return new sound_out_i_winmm;
}
