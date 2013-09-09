// Fir_Resampler chip emulator container that mixes into the output buffer

// Game_Music_Emu $vers
#ifndef CHIP_RESAMPLER_H
#define CHIP_RESAMPLER_H

#include "blargg_source.h"

#include "lanczos_resampler.h"

int const resampler_extra = 0; //34;

template<class Emu>
class Chip_Resampler_Emu : public Emu {
	int last_time;
	short* out;
	typedef short dsample_t;
	enum { disabled_time = -1 };
	enum { gain_bits = 14 };
	dsample_t sample_buf[64];
	int gain_;

	void * resampler;

	void mix_samples( short * buf, int count )
	{
		for ( int i = 0; i < count; i++ )
		{
			short sample_l, sample_r;
			gme_lanczos_resampler_get_sample(resampler, &sample_l, &sample_r);
			gme_lanczos_resampler_remove_sample(resampler);
			int sample = sample_l;
			sample += buf[i * 2 + 0];
			if ((unsigned)(sample + 0x8000) & 0xFFFF0000) sample = 0x7FFF ^ (sample >> 31);
			buf[i * 2 + 0] = sample;
			sample = sample_r;
			sample += buf[i * 2 + 1];
			if ((unsigned)(sample + 0x8000) & 0xFFFF0000) sample = 0x7FFF ^ (sample >> 31);
			buf[i * 2 + 1] = sample;
		}
	}

public:
	Chip_Resampler_Emu()
	{
		last_time = disabled_time; out = NULL;
		resampler = gme_lanczos_resampler_create(32);
	}
	~Chip_Resampler_Emu()
	{
		gme_lanczos_resampler_delete(resampler);
	}

	void setup( double oversample, double gain )
	{
		gain_ = (int) ((1 << gain_bits) * gain);
		gme_lanczos_resampler_set_rate( resampler, oversample );
		gme_lanczos_resampler_clear( resampler );
	}

	void reset()
	{
		Emu::reset();
		gme_lanczos_resampler_clear( resampler );
	}

	void clear()
	{
		gme_lanczos_resampler_clear(resampler);
	}

	void enable( bool b = true )    { last_time = b ? 0 : disabled_time; }
	bool enabled() const            { return last_time != disabled_time; }
	void begin_frame( short* buf )  { out = buf; last_time = 0; }

	int run_until( int time )
	{
		int count = time - last_time;
		while ( count > 0 )
		{
			if ( last_time < 0 )
				return false;
			last_time = time;

			short* p = out;
			int sample_count = gme_lanczos_resampler_get_sample_count(resampler);
			if ( sample_count > 0 )
			{
				if ( sample_count > count )
				{
					out += count * Emu::out_chan_count;
					mix_samples( p, count );
					return true;
				}
				out += sample_count * Emu::out_chan_count;
				mix_samples( p, sample_count );
				count -= sample_count;
			}

			sample_count = gme_lanczos_resampler_get_write_sample_free(resampler) * 2;
			while (sample_count > 0)
			{
				if (sample_count > 64) sample_count = 64;
				memset( sample_buf, 0, sizeof(*sample_buf) * sample_count );
				Emu::run( sample_count >> 1, sample_buf );
				for ( int i = 0; i < sample_count; i++ )
				{
					dsample_t * ptr = sample_buf + i;
					*ptr = ( *ptr * gain_ ) >> gain_bits;
				}
				for ( int i = 0; i < sample_count; i += 2 )
				{
					dsample_t * ptr = sample_buf + i;
					gme_lanczos_resampler_write_sample(resampler, ptr[0], ptr[1]);
				}

				sample_count = gme_lanczos_resampler_get_write_sample_free(resampler) * 2;
			}
		}
		return true;
	}
};



#endif
