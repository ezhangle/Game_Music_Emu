#include <stdlib.h>
#include <string.h>
#define _USE_MATH_DEFINES
#include <math.h>

#include "lanczos_resampler.h"

enum { LANCZOS_RESOLUTION = 8192 };
enum { LANCZOS_WIDTH = 8 };
enum { LANCZOS_SAMPLES = LANCZOS_RESOLUTION * LANCZOS_WIDTH };

static double lanczos_lut[LANCZOS_SAMPLES + 1];

enum { lanczos_buffer_wrap_size = LANCZOS_WIDTH * 2 };

static int fEqual(const double b, const double a)
{
	return fabs(a - b) < 1.0e-6;
}

static double sinc(double x)
{
	return fEqual(x, 0.0) ? 1.0 : sin(x * M_PI) / (x * M_PI);
}

static void gme_lanczos_init()
{
	static int initialized = 0;

	if ( !initialized )
	{
		unsigned i;
		double dx = (double)(LANCZOS_WIDTH) / LANCZOS_SAMPLES, x = 0.0;
		for (i = 0; i < LANCZOS_SAMPLES + 1; ++i, x += dx)
			lanczos_lut[i] = abs(x) < LANCZOS_WIDTH ? sinc(x) * sinc(x / LANCZOS_WIDTH) : 0.0;
		initialized = 1;
	}
}

typedef struct lanczos_resampler
{
    int write_pos, write_filled;
    int read_pos, read_filled;
    unsigned short phase;
    unsigned int phase_inc;
	int buffer_size;
	float * buffer_in_l;
	float * buffer_in_r;
	short * buffer_out;
} lanczos_resampler;

void * gme_lanczos_resampler_create(int buffer_size)
{
	lanczos_resampler * r = ( lanczos_resampler * ) malloc( sizeof(lanczos_resampler) );
    if ( !r ) return 0;

	gme_lanczos_init();

	r->write_pos = 0;
    r->write_filled = 0;
    r->read_pos = 0;
    r->read_filled = 0;
    r->phase = 0;
    r->phase_inc = 0;

	r->buffer_size = buffer_size;
	r->buffer_in_l = (float *) malloc( sizeof(*r->buffer_in_l) * (buffer_size + lanczos_buffer_wrap_size) * 2 );
	if ( !r->buffer_in_l )
	{
		free( r );
		return NULL;
	}
	r->buffer_in_r = r->buffer_in_l + buffer_size + lanczos_buffer_wrap_size;
	r->buffer_out = (short *) malloc( sizeof(*r->buffer_out) * buffer_size * 2 );
	if ( !r->buffer_out )
	{
		free( r->buffer_in_l );
		free( r );
		return NULL;
	}

	return r;
}

void gme_lanczos_resampler_delete(void *_r)
{
	lanczos_resampler * r = ( lanczos_resampler * ) _r;
	free( r->buffer_out );
	free( r->buffer_in_l );
	free( r );
}

int gme_lanczos_resampler_resize(void *_r, int buffer_size)
{
	lanczos_resampler * r = ( lanczos_resampler * ) _r;

	if ( buffer_size != r->buffer_size )
	{
		void * buffer = realloc(r->buffer_in_l, sizeof(*r->buffer_in_l) * (buffer_size + lanczos_buffer_wrap_size) * 2 );
		if ( !buffer ) return -1;
		r->buffer_in_l = (float *) buffer;
		r->buffer_in_r = r->buffer_in_l + buffer_size + lanczos_buffer_wrap_size;
		buffer = realloc(r->buffer_out, sizeof(*r->buffer_out) * buffer_size * 2 );
		if ( !buffer ) return -1;
		r->buffer_out = (short *) buffer;

		r->write_pos = 0;
		r->write_filled = 0;
		r->read_pos = 0;
		r->read_filled = 0;

		r->buffer_size = buffer_size;
	}

	return 0;
}

void gme_lanczos_resampler_clear(void *_r)
{
	lanczos_resampler * r = ( lanczos_resampler * ) _r;

	r->write_pos = 0;
	r->write_filled = 0;
	r->read_pos = 0;
	r->read_filled = 0;
	r->phase = 0;
}

void gme_lanczos_resampler_set_rate(void *_r, double new_factor)
{
    lanczos_resampler * r = ( lanczos_resampler * ) _r;
	r->phase_inc = (int)( new_factor * LANCZOS_RESOLUTION );
}

double gme_lanczos_resampler_get_rate(void *_r)
{
	lanczos_resampler * r = ( lanczos_resampler * ) _r;
	return (double)r->phase_inc / LANCZOS_RESOLUTION;
}

int gme_lanczos_resampler_get_samples_written(void *_r)
{
	lanczos_resampler * r = ( lanczos_resampler * ) _r;
	return r->write_filled;
}

int gme_lanczos_resampler_get_write_sample_free(void *_r)
{
	lanczos_resampler * r = ( lanczos_resampler * ) _r;
	return r->buffer_size - r->write_filled;
}

void gme_lanczos_resampler_write_sample(void *_r, short left, short right)
{
	lanczos_resampler * r = ( lanczos_resampler * ) _r;

	int buffer_size = r->buffer_size;

	if ( r->write_filled < buffer_size )
	{
		unsigned int write_pos = r->write_pos;

		float s32 = left;

		r->buffer_in_l[ write_pos ] = s32;
		if ( write_pos < lanczos_buffer_wrap_size )
			r->buffer_in_l[ write_pos + buffer_size ] = s32;

		s32 = right;

		r->buffer_in_r[ write_pos ] = s32;
		if ( write_pos < lanczos_buffer_wrap_size )
			r->buffer_in_r[ write_pos + buffer_size ] = s32;

        ++r->write_filled;

		r->write_pos = ( write_pos + 1 ) % buffer_size;
	}
}

int gme_lanczos_resampler_skip_input(void *_r, int pairs_to_skip)
{
	lanczos_resampler * r = ( lanczos_resampler * ) _r;

	if ( pairs_to_skip > r->write_filled )
		pairs_to_skip = r->write_filled;

	r->write_pos = ( r->write_pos + pairs_to_skip ) % r->buffer_size;

	return pairs_to_skip;
}

static int gme_lanczos_resampler_run(lanczos_resampler *r, short ** out_, short * out_end)
{
	int buffer_size = r->buffer_size;
    int in_size = r->write_filled;
	int in_offset = ( buffer_size + r->write_pos - r->write_filled ) % buffer_size;
	float const* in_l_ = r->buffer_in_l + in_offset;
	float const* in_r_ = r->buffer_in_r + in_offset;
	float const* in_wrap = r->buffer_in_l + buffer_size;
	int used = 0;
	in_size -= LANCZOS_WIDTH * 2;
	if ( in_size > 0 )
	{
		short* out = *out_;
		float const* in_l = in_l_;
		float const* in_r = in_r_;
        int phase = r->phase;
        int phase_inc = r->phase_inc;

		int step = phase_inc > LANCZOS_RESOLUTION ? LANCZOS_RESOLUTION * LANCZOS_RESOLUTION / phase_inc : LANCZOS_RESOLUTION;
		
		do
		{
			// accumulate in extended precision
            double kernel[LANCZOS_WIDTH * 2], kernel_sum = 0.0;
			int i = LANCZOS_WIDTH;
			int phase_adj = phase * step / LANCZOS_RESOLUTION;
			double sample_l, sample_r;
			int in_inc;
			int sample_temp;

			if ( out >= out_end )
				break;

			for (; i >= -LANCZOS_WIDTH + 1; --i)
			{
				int pos = i * step;
				kernel_sum += kernel[i + LANCZOS_WIDTH - 1] = lanczos_lut[abs(phase_adj - pos)];
			}
			for (sample_l = 0, sample_r = 0, i = 0; i < LANCZOS_WIDTH * 2; ++i)
			{
				sample_l += in_l[i] * kernel[i];
				sample_r += in_r[i] * kernel[i];
			}
            kernel_sum = 1 / kernel_sum;
			sample_temp = sample_l * kernel_sum;
			if ((unsigned)(sample_temp + 0x8000) & 0xFFFF0000) sample_temp = (sample_temp >> 31) ^ 0x7FFF;
			*out++ = (short)sample_temp;
			sample_temp = sample_r * kernel_sum;
			if ((unsigned)(sample_temp + 0x8000) & 0xFFFF0000) sample_temp = (sample_temp >> 31) ^ 0x7FFF;
			*out++ = (short)sample_temp;

            phase += phase_inc;

			in_inc = phase >> 13;
			in_l += in_inc;
			in_r += in_inc;
			used += in_inc;

			if (in_l >= in_wrap)
			{
				in_l -= buffer_size;
				in_r -= buffer_size;
			}

			phase &= 8191;
		}
		while ( used < in_size );
		
        r->phase = phase;
		*out_ = out;

        r->write_filled -= used;
	}
	
	return used;
}

static void gme_lanczos_resampler_fill(lanczos_resampler *r)
{
	int buffer_size = r->buffer_size;
	while ( r->write_filled > (LANCZOS_WIDTH * 2) &&
			r->read_filled < buffer_size )
	{
		int write_pos = ( r->read_pos + r->read_filled ) % buffer_size;
		int write_size = buffer_size - write_pos;
		short * out = r->buffer_out + write_pos * 2;
		if ( write_size > ( buffer_size - r->read_filled ) )
			write_size = buffer_size - r->read_filled;
		gme_lanczos_resampler_run( r, &out, out + write_size * 2 );
		r->read_filled += ( ( out - r->buffer_out ) >> 1 ) - write_pos;
	}
}

int gme_lanczos_resampler_get_sample_count(void *_r)
{
    lanczos_resampler * r = ( lanczos_resampler * ) _r;
	gme_lanczos_resampler_fill( r );
    return r->read_filled;
}

void gme_lanczos_resampler_get_sample(void *_r, short * left, short * right)
{
    lanczos_resampler * r = ( lanczos_resampler * ) _r;
    if ( r->read_filled < 1 )
		gme_lanczos_resampler_fill( r );
    if ( r->read_filled < 1 )
	{
		*left = 0;
		*right = 0;
	}
	else
	{
		*left = r->buffer_out[ r->read_pos * 2 ];
		*right = r->buffer_out[ r->read_pos * 2 + 1 ];
	}
}

void gme_lanczos_resampler_remove_sample(void *_r)
{
    lanczos_resampler * r = ( lanczos_resampler * ) _r;
    if ( r->read_filled > 0 )
    {
        --r->read_filled;
		r->read_pos = ( r->read_pos + 1 ) % r->buffer_size;
    }
}
