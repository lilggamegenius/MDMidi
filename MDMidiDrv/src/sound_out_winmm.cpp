/*#define STRICT
#ifndef _WIN32_WINNT
#define _WIN32_WINNT _WIN32_WINNT_WINXP
#endif*/

#include "sound_out.h"

#include <windows.h>
#include <mmreg.h>
#include <malloc.h>
#include <assert.h>


#pragma comment (lib, "winmm.lib")

class sound_out_i_winmm : public sound_out
{
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
	sound_out_i_winmm()
	{
		this->hWaveOut = NULL;
		this->WaveHdrs = NULL;
		//this->Buffers = NULL;
		this->paused = false;
		
		return;
	}
	
	virtual ~sound_out_i_winmm()
	{
		close();
		
		return;
	}
	
	virtual const char* open(void * hwnd, unsigned sample_rate, unsigned short nch, bool floating_point,
							unsigned max_samples_per_frame, unsigned num_frames)
	{
		WAVEFORMATEX WaveFmt;
		MMRESULT RetVal;
		unsigned int CurBuf;
		WAVEHDR* TempHdr;
		
		this->sample_rate = sample_rate;
		this->nch = nch;
		this->max_samples_per_frame = max_samples_per_frame;
		this->num_frames = num_frames;
		this->bytes_per_sample = floating_point ? 4 : 2;
		
		WaveFmt.wFormatTag = floating_point ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
		WaveFmt.nChannels = nch;
		WaveFmt.nSamplesPerSec = sample_rate;
		WaveFmt.wBitsPerSample = bytes_per_sample * 8;
		WaveFmt.nBlockAlign = bytes_per_sample * WaveFmt.nChannels;
		WaveFmt.nAvgBytesPerSec = WaveFmt.nSamplesPerSec * WaveFmt.nBlockAlign;
		WaveFmt.cbSize = 0;
		
		RetVal = waveOutOpen(&this->hWaveOut, WAVE_MAPPER, &WaveFmt, 0x00, 0x00, CALLBACK_NULL);
		if (RetVal != MMSYSERR_NOERROR)
			return "Opening Wave Device";
		
		this->buffer_size_bytes = max_samples_per_frame * this->bytes_per_sample;
		this->WaveHdrs = new WAVEHDR[num_frames];
		//this->Buffers = new unsigned char*[num_frames];
		for (CurBuf = 0; CurBuf < num_frames; CurBuf ++)
		{
			TempHdr = &this->WaveHdrs[CurBuf];
			//this->Buffers[CurBuf] = new unsigned char[this->buffer_size_bytes];
			//TempHdr->lpData = (LPSTR)this->Buffers[CurBuf];
			TempHdr->lpData = (LPSTR)new unsigned char[this->buffer_size_bytes];
			TempHdr->dwBufferLength = this->buffer_size_bytes;
			TempHdr->dwBytesRecorded = 0x00;
			TempHdr->dwUser = 0x00;
			TempHdr->dwFlags = 0x00;
			TempHdr->dwLoops = 0x00;
			TempHdr->lpNext = NULL;
			TempHdr->reserved = 0x00;
			RetVal = waveOutPrepareHeader(this->hWaveOut, TempHdr, sizeof(WAVEHDR));
			TempHdr->dwFlags |= WHDR_DONE;
		}
		
#if 1
		for (CurBuf = 0; CurBuf < num_frames; CurBuf ++)
		{
			TempHdr = &this->WaveHdrs[CurBuf];
			
			TempHdr->dwFlags &= ~WHDR_DONE;
			TempHdr->dwBufferLength = TempHdr->dwBufferLength;
			memset(TempHdr->lpData, 0x00, TempHdr->dwBufferLength);
			waveOutWrite(this->hWaveOut, TempHdr, sizeof(WAVEHDR));
		}
#endif
		NextBuf = 0;
		
		return NULL;
	}
	
	void close(void)
	{
		if (this->hWaveOut == NULL)
			return;
		
		MMRESULT RetVal;
		unsigned int CurBuf;
		WAVEHDR* TempHdr;
		
		RetVal = waveOutReset(this->hWaveOut);
		for (CurBuf = 0x00; CurBuf < this->num_frames; CurBuf ++)
		{
			TempHdr = &this->WaveHdrs[CurBuf];
			
			RetVal = waveOutUnprepareHeader(this->hWaveOut, TempHdr, sizeof(WAVEHDR));
			//delete [] this->Buffers[CurBuf];
			delete [] TempHdr->lpData;
		}
		delete [] this->WaveHdrs;	this->WaveHdrs = NULL;
		//delete [] this->Buffers;	this->Buffers = NULL;
		
		RetVal = waveOutClose(this->hWaveOut);
		if (RetVal != MMSYSERR_NOERROR)
			return;
		this->hWaveOut = NULL;
		
		return;
	}

	virtual const char* write_frame(void* buffer, unsigned num_samples, bool wait)
	{
		if (this->paused)
		{
			if (wait)
				Sleep(MulDiv(num_samples / nch, 1000, sample_rate));
			return NULL;
		}
		
		unsigned int BufWrtBytes = num_samples * this->bytes_per_sample;
		WAVEHDR* TempHdr;
		
		assert(BufWrtBytes <= this->buffer_size_bytes);
		TempHdr = &this->WaveHdrs[this->NextBuf];
		if (wait)
		{
			while(! (TempHdr->dwFlags & WHDR_DONE))
				Sleep(1);
		}
		else
		{
			if (! (TempHdr->dwFlags & WHDR_DONE))
				return NULL;
		}
		
		TempHdr->dwFlags &= ~WHDR_DONE;
		TempHdr->dwBufferLength = BufWrtBytes;
		memcpy(TempHdr->lpData, buffer, BufWrtBytes);
		waveOutWrite(this->hWaveOut, TempHdr, sizeof(WAVEHDR));
		
		this->NextBuf ++;
		this->NextBuf %= this->num_frames;
		
		return NULL;
	}
	
	virtual bool can_write(unsigned num_samples)
	{
		//unsigned int buffer_size_write = num_samples * bytes_per_sample;
		
		return (this->WaveHdrs[this->NextBuf].dwFlags & WHDR_DONE);
	}
	
	virtual const char* set_ratio(double ratio)
	{
		//if ( !p_stream->set_ratio( ratio ) ) return "setting ratio";
		return "Not supported";
	}
	
	virtual const char* pause(bool pausing)
	{
		MMRESULT RetVal;
		
		if (pausing)
			RetVal = waveOutPause(this->hWaveOut);
		else
			RetVal = waveOutRestart(this->hWaveOut);
		if (RetVal != MMSYSERR_NOERROR)
			return "Failed.";
		
		this->paused = pausing;
		
		return NULL;
	}
	
	virtual double buffered(void)
	{
		unsigned int CurBuf;
		unsigned int BufBytes;
		unsigned int BufBytMax = this->max_samples_per_frame * this->buffer_size_bytes;
		
		BufBytes = 0;
		for (CurBuf = 0x00; CurBuf < this->num_frames; CurBuf ++)
		{
			if (! (this->WaveHdrs[CurBuf].dwFlags & WHDR_DONE))
				BufBytes += this->WaveHdrs[CurBuf].dwBufferLength;
		}
		
		return double(BufBytes) / double(BufBytMax);
	}
};

sound_out* create_sound_out_winmm(void)
{
	return new sound_out_i_winmm;
}
