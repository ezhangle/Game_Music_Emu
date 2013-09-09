// Game_Music_Emu $vers. http://www.slack.net/~ant/

#include "Dual_Resampler.h"

/* Copyright (C) 2003-2008 Shay Green. This module is free software; you
can redistribute it and/or modify it under the terms of the GNU Lesser
General Public License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version. This
module is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
details. You should have received a copy of the GNU Lesser General Public
License along with this module; if not, write to the Free Software Foundation,
Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA */

#include "blargg_source.h"

// TODO: fix this. hack since resampler holds back some output.
int const resampler_extra = 34;

int const stereo = 2;

blargg_err_t Dual_Resampler::reset( int pairs )
{
	// expand allocations a bit
	RETURN_ERR( sample_buf.resize( (pairs + (pairs >> 2)) * 2 ) );
	resize( pairs );
	unsigned int resampler_size = oversamples_per_frame + (oversamples_per_frame >> 2);
	if ( gme_lanczos_resampler_resize( resampler, resampler_size ) < 0 ) return blargg_err_memory;
	RETURN_ERR( resample_buf.resize( resampler_size * 2 ) );
	return blargg_ok;
}

void Dual_Resampler::resize( int pairs )
{
	int new_sample_buf_size = pairs * 2;
	//new_sample_buf_size = new_sample_buf_size / 4 * 4; // TODO: needed only for 3:2 downsampler
	if ( sample_buf_size != new_sample_buf_size )
	{
		if ( (unsigned) new_sample_buf_size > sample_buf.size() )
		{
			check( false );
			return;
		}
		sample_buf_size = new_sample_buf_size;
		oversamples_per_frame = int (pairs * gme_lanczos_resampler_get_rate(resampler)) * 2 + 2;
		clear();
	}
}

void Dual_Resampler::clear()
{
	buf_pos = buffered = 0;
	gme_lanczos_resampler_clear(resampler);
}

int Dual_Resampler::play_frame_( Stereo_Buffer& stereo_buf, dsample_t out [], Stereo_Buffer** secondary_buf_set, int secondary_buf_set_count )
{
	int pair_count = sample_buf_size >> 1;
	blip_time_t blip_time = stereo_buf.center()->count_clocks( pair_count );
	int sample_count = gme_lanczos_resampler_get_write_sample_free( resampler ) * 2;

	if ( sample_count > 0 )
	{
		if ( sample_count > oversamples_per_frame )
			sample_count = oversamples_per_frame;

		int new_count = set_callback.f( set_callback.data, blip_time, sample_count, &resample_buf[0] );

		for ( int i = 0; i < new_count; i += 2 )
		{
			gme_lanczos_resampler_write_sample(resampler, resample_buf[i], resample_buf[i + 1]);
		}
	}

	stereo_buf.end_frame( blip_time );
	assert( stereo_buf.samples_avail() == pair_count * 2 );
	if ( secondary_buf_set && secondary_buf_set_count )
	{
		for ( int i = 0; i < secondary_buf_set_count; i++ )
		{
			Stereo_Buffer * second_buf = secondary_buf_set[i];
			blip_time_t blip_time_2 = second_buf->center()->count_clocks( pair_count );
			second_buf->end_frame( blip_time_2 );
			assert( second_buf->samples_avail() == pair_count * 2 );
		}
	}

	int count = gme_lanczos_resampler_get_sample_count(resampler) * 2;

	if ( count > sample_buf_size ) count = sample_buf_size;

	for ( int i = 0; i < count; i += 2 )
	{
		gme_lanczos_resampler_get_sample( resampler, &sample_buf[i], &sample_buf[i + 1] );
		gme_lanczos_resampler_remove_sample( resampler );
	}

	mix_samples( stereo_buf, out, count, secondary_buf_set, secondary_buf_set_count );

	pair_count = count >> 1;
	stereo_buf.left()->remove_samples( pair_count );
	stereo_buf.right()->remove_samples( pair_count );
	stereo_buf.center()->remove_samples( pair_count );

	if ( secondary_buf_set && secondary_buf_set_count )
	{
		for ( int i = 0; i < secondary_buf_set_count; i++ )
		{
			Stereo_Buffer * second_buf = secondary_buf_set[i];
			second_buf->left()->remove_samples( pair_count );
			second_buf->right()->remove_samples( pair_count );
			second_buf->center()->remove_samples( pair_count );
		}
	}

	return count;
}

void Dual_Resampler::dual_play( int count, dsample_t out [], Stereo_Buffer& stereo_buf, Stereo_Buffer** secondary_buf_set, int secondary_buf_set_count )
{
	// empty extra buffer
	int remain = buffered - buf_pos;
	if ( remain )
	{
		if ( remain > count )
			remain = count;
		count -= remain;
		memcpy( out, &sample_buf [buf_pos], remain * sizeof *out );
		out += remain;
		buf_pos += remain;
	}
	
	// entire frames
	while ( count >= sample_buf_size )
	{
        buf_pos = buffered = play_frame_( stereo_buf, out, secondary_buf_set, secondary_buf_set_count );
		out += buffered;
		count -= buffered;
	}

	while (count > 0)
	{
        buffered = play_frame_( stereo_buf, sample_buf.begin(), secondary_buf_set, secondary_buf_set_count );
		if ( buffered >= count )
		{
			buf_pos = count;
			memcpy( out, sample_buf.begin(), count * sizeof *out );
			out += count;
			count = 0;
		}
		else
		{
			memcpy( out, sample_buf.begin(), buffered * sizeof *out );
			out += buffered;
			count -= buffered;
		}
	}
}

void Dual_Resampler::mix_samples( Stereo_Buffer& stereo_buf, dsample_t out_ [], int count, Stereo_Buffer** secondary_buf_set, int secondary_buf_set_count )
{
	// lol hax
	if ( ((Tracked_Blip_Buffer*)stereo_buf.left())->non_silent() | ((Tracked_Blip_Buffer*)stereo_buf.right())->non_silent() )
		mix_stereo( stereo_buf, out_, count );
	else
		mix_mono( stereo_buf, out_, count );

    if ( secondary_buf_set && secondary_buf_set_count )
    {
        for ( int i = 0; i < secondary_buf_set_count; i++ )
        {
            Stereo_Buffer * second_buf = secondary_buf_set[i];
            if ( ((Tracked_Blip_Buffer*)second_buf->left())->non_silent() | ((Tracked_Blip_Buffer*)second_buf->right())->non_silent() )
                mix_extra_stereo( *second_buf, out_, count );
            else
                mix_extra_mono( *second_buf, out_, count );
        }
	}
}

void Dual_Resampler::mix_mono( Stereo_Buffer& stereo_buf, dsample_t out_ [], int count )
{
	int const bass = BLIP_READER_BASS( *stereo_buf.center() );
	BLIP_READER_BEGIN( sn, *stereo_buf.center() );
	
	count >>= 1;
	BLIP_READER_ADJ_( sn, count );
	
	typedef dsample_t stereo_dsample_t [2];
	stereo_dsample_t* BLARGG_RESTRICT out = (stereo_dsample_t*) out_ + count;
	stereo_dsample_t const* BLARGG_RESTRICT in =
			(stereo_dsample_t const*) sample_buf.begin() + count;
	int offset = -count;
	int const gain = gain_;
	do
	{
		int s = BLIP_READER_READ_RAW( sn ) >> (blip_sample_bits - 16);
		BLIP_READER_NEXT_IDX_( sn, bass, offset );
		
		int l = (in [offset] [0] * gain >> gain_bits) + s;
		int r = (in [offset] [1] * gain >> gain_bits) + s;
		
		BLIP_CLAMP( l, l );
		out [offset] [0] = (blip_sample_t) l;
		
		BLIP_CLAMP( r, r );
		out [offset] [1] = (blip_sample_t) r;
	}
	while ( ++offset );
	
	BLIP_READER_END( sn, *stereo_buf.center() );
}

void Dual_Resampler::mix_stereo( Stereo_Buffer& stereo_buf, dsample_t out_ [], int count )
{
	int const bass = BLIP_READER_BASS( *stereo_buf.center() );
	BLIP_READER_BEGIN( snc, *stereo_buf.center() );
	BLIP_READER_BEGIN( snl, *stereo_buf.left() );
	BLIP_READER_BEGIN( snr, *stereo_buf.right() );
	
	count >>= 1;
	BLIP_READER_ADJ_( snc, count );
	BLIP_READER_ADJ_( snl, count );
	BLIP_READER_ADJ_( snr, count );
	
	typedef dsample_t stereo_dsample_t [2];
	stereo_dsample_t* BLARGG_RESTRICT out = (stereo_dsample_t*) out_ + count;
	stereo_dsample_t const* BLARGG_RESTRICT in =
			(stereo_dsample_t const*) sample_buf.begin() + count;
	int offset = -count;
	int const gain = gain_;
	do
	{
		int sc = BLIP_READER_READ_RAW( snc ) >> (blip_sample_bits - 16);
		int sl = BLIP_READER_READ_RAW( snl ) >> (blip_sample_bits - 16);
		int sr = BLIP_READER_READ_RAW( snr ) >> (blip_sample_bits - 16);
		BLIP_READER_NEXT_IDX_( snc, bass, offset );
		BLIP_READER_NEXT_IDX_( snl, bass, offset );
		BLIP_READER_NEXT_IDX_( snr, bass, offset );
		
		int l = (in [offset] [0] * gain >> gain_bits) + sl + sc;
		int r = (in [offset] [1] * gain >> gain_bits) + sr + sc;
		
		BLIP_CLAMP( l, l );
		out [offset] [0] = (blip_sample_t) l;
		
		BLIP_CLAMP( r, r );
		out [offset] [1] = (blip_sample_t) r;
	}
	while ( ++offset );
	
	BLIP_READER_END( snc, *stereo_buf.center() );
	BLIP_READER_END( snl, *stereo_buf.left() );
	BLIP_READER_END( snr, *stereo_buf.right() );
}

void Dual_Resampler::mix_extra_mono( Stereo_Buffer& stereo_buf, dsample_t out_ [], int count )
{
	int const bass = BLIP_READER_BASS( *stereo_buf.center() );
	BLIP_READER_BEGIN( sn, *stereo_buf.center() );

	count >>= 1;
	BLIP_READER_ADJ_( sn, count );

	typedef dsample_t stereo_dsample_t [2];
	stereo_dsample_t* BLARGG_RESTRICT out = (stereo_dsample_t*) out_ + count;
	int offset = -count;
	int const gain = gain_;
	do
	{
		int s = BLIP_READER_READ_RAW( sn ) >> (blip_sample_bits - 16);
		BLIP_READER_NEXT_IDX_( sn, bass, offset );

		int l = out [offset] [0] + s;
		int r = out [offset] [1] + s;

		BLIP_CLAMP( l, l );
		out [offset] [0] = (blip_sample_t) l;

		BLIP_CLAMP( r, r );
		out [offset] [1] = (blip_sample_t) r;
	}
	while ( ++offset );

	BLIP_READER_END( sn, *stereo_buf.center() );
}

void Dual_Resampler::mix_extra_stereo( Stereo_Buffer& stereo_buf, dsample_t out_ [], int count )
{
	int const bass = BLIP_READER_BASS( *stereo_buf.center() );
	BLIP_READER_BEGIN( snc, *stereo_buf.center() );
	BLIP_READER_BEGIN( snl, *stereo_buf.left() );
	BLIP_READER_BEGIN( snr, *stereo_buf.right() );

	count >>= 1;
	BLIP_READER_ADJ_( snc, count );
	BLIP_READER_ADJ_( snl, count );
	BLIP_READER_ADJ_( snr, count );

	typedef dsample_t stereo_dsample_t [2];
	stereo_dsample_t* BLARGG_RESTRICT out = (stereo_dsample_t*) out_ + count;
	int offset = -count;
	int const gain = gain_;
	do
	{
		int sc = BLIP_READER_READ_RAW( snc ) >> (blip_sample_bits - 16);
		int sl = BLIP_READER_READ_RAW( snl ) >> (blip_sample_bits - 16);
		int sr = BLIP_READER_READ_RAW( snr ) >> (blip_sample_bits - 16);
		BLIP_READER_NEXT_IDX_( snc, bass, offset );
		BLIP_READER_NEXT_IDX_( snl, bass, offset );
		BLIP_READER_NEXT_IDX_( snr, bass, offset );

		int l = out [offset] [0] + sl + sc;
		int r = out [offset] [1] + sr + sc;

		BLIP_CLAMP( l, l );
		out [offset] [0] = (blip_sample_t) l;

		BLIP_CLAMP( r, r );
		out [offset] [1] = (blip_sample_t) r;
	}
	while ( ++offset );

	BLIP_READER_END( snc, *stereo_buf.center() );
	BLIP_READER_END( snl, *stereo_buf.left() );
	BLIP_READER_END( snr, *stereo_buf.right() );
}
