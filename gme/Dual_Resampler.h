// Combination of Fir_Resampler and Stereo_Buffer mixing. Used by Sega FM emulators.

// Game_Music_Emu $vers
#ifndef DUAL_RESAMPLER_H
#define DUAL_RESAMPLER_H

#include "Multi_Buffer.h"

#include "lanczos_resampler.h"

class Dual_Resampler {
public:
	typedef short dsample_t;

	Dual_Resampler() { resampler = gme_lanczos_resampler_create(64); }
	~Dual_Resampler() { gme_lanczos_resampler_delete(resampler); }
	
	void setup( double oversample, double gain );
	blargg_err_t reset( int max_pairs );
	void resize( int pairs_per_frame );
	double rate() const { return gme_lanczos_resampler_get_rate(resampler); }
	void clear();
	
    void dual_play( int count, dsample_t out [], Stereo_Buffer&, Stereo_Buffer** secondary_buf_set = NULL, int secondary_buf_set_count = 0 );
	
	blargg_callback<int (*)( void*, blip_time_t, int, dsample_t* )> set_callback;

private:
	enum { gain_bits = 14 };
	blargg_vector<dsample_t> resample_buf;
	blargg_vector<dsample_t> sample_buf;
	int sample_buf_size;
	int oversamples_per_frame;
	int buf_pos;
	int buffered;
	int gain_;
	
	void * resampler;
    void mix_samples( Stereo_Buffer&, dsample_t [], int, Stereo_Buffer**, int );
	void mix_mono( Stereo_Buffer&, dsample_t [], int );
	void mix_stereo( Stereo_Buffer&, dsample_t [], int );
	void mix_extra_mono( Stereo_Buffer&, dsample_t [], int );
    void mix_extra_stereo( Stereo_Buffer&, dsample_t [], int );
	int play_frame_( Stereo_Buffer&, dsample_t [], Stereo_Buffer**, int );
};

inline void Dual_Resampler::setup( double oversample, double gain )
{
	gain_ = (int) ((1 << gain_bits) * gain);
	gme_lanczos_resampler_set_rate(resampler, oversample);
}

#endif
