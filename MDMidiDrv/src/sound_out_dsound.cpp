#define STRICT
#ifndef _WIN32_WINNT
#define _WIN32_WINNT _WIN32_WINNT_WINXP
#endif

#include <windows.h>

#include "sound_out.h"

#include "ds_stream.h"

class sound_out_i_dsound final : public sound_out
{
	ds_api    * p_api;
	ds_stream * p_stream;

	bool        paused;

	unsigned    sample_rate, max_samples_per_frame, num_frames, bytes_per_sample, buffer_size_bytes, last_write;
	unsigned short nch;

public:
	sound_out_i_dsound()
	{
		p_api = nullptr;
		p_stream = nullptr;
		paused = false;
	}

	~sound_out_i_dsound() override
	{
		close();
	}

	const char* open( void * hwnd, unsigned sample_rate, unsigned short nch, bool floating_point, unsigned max_samples_per_frame, unsigned num_frames ) override
	{
		p_api = ds_api_create( static_cast<HWND>(hwnd) );
		if ( !p_api )
		{
			return "Initializing DirectSound";
		}

		this->sample_rate = sample_rate;
		this->nch = nch;
		this->max_samples_per_frame = max_samples_per_frame;
		this->num_frames = num_frames;
		bytes_per_sample = floating_point ? 4 : 2;

		ds_stream_config cfg;
		cfg.srate = sample_rate;
		cfg.nch = nch;
		cfg.bps = floating_point ? 32 : 16;
		cfg.buffer_ms = MulDiv( max_samples_per_frame / nch * num_frames, 1000, sample_rate );

		p_stream = p_api->ds_stream_create( &cfg );
		if ( !p_stream )
		{
			return "Creating DirectSound stream object";
		}

		return nullptr;
	}

	void close()
	{
		if ( p_stream )
		{
			p_stream->release();
			p_stream = nullptr;
		}

		if ( p_api )
		{
			p_api->release();
			p_api = nullptr;
		}
	}

	const char* write_frame( void * buffer, unsigned num_samples, bool wait ) override
	{
		if ( paused )
		{
			if ( wait ) Sleep( MulDiv( num_samples / nch, 1000, sample_rate ) );
			return nullptr;
		}

		const unsigned int buffer_size_write = num_samples * bytes_per_sample;

		if ( wait )
		{
			while ( p_stream->can_write_bytes() < buffer_size_write ) Sleep( 1 );
		}

		p_stream->write( buffer, buffer_size_write );

		return nullptr;
	}

	bool can_write(unsigned num_samples) override
	{
		const unsigned int buffer_size_write = num_samples * bytes_per_sample;

		return p_stream->can_write_bytes() >= buffer_size_write;
	}

	const char* set_ratio( double ratio ) override
	{
		if ( !p_stream->set_ratio( ratio ) ) return "setting ratio";
		return nullptr;
	}

	const char* pause( bool pausing ) override
	{
		p_stream->pause( paused = pausing );

		return nullptr;
	}

	double buffered() override
	{
		const unsigned bytes = p_stream->get_latency_bytes();
		const unsigned write_max_bytes = max_samples_per_frame * 2;
		return static_cast<double>(bytes) / static_cast<double>(write_max_bytes);
	}
};

sound_out * create_sound_out_ds()
{
	return new sound_out_i_dsound;
}