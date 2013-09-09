#ifndef _LANCZOS_RESAMPLER_H_
#define _LANCZOS_RESAMPLER_H_

#ifdef __cplusplus
extern "C" {
#endif

void gme_lanczos_init();

void * gme_lanczos_resampler_create(int buffer_size);
void gme_lanczos_resampler_delete(void *);

int gme_lanczos_resampler_resize(void *, int buffer_size);

void gme_lanczos_resampler_clear(void *);

int gme_lanczos_resampler_get_samples_written(void *);
int gme_lanczos_resampler_get_write_sample_free(void *);
void gme_lanczos_resampler_write_sample(void *, short l, short r);
int gme_lanczos_resampler_skip_input(void *, int pairs_to_skip);
void gme_lanczos_resampler_set_rate( void *, double new_factor );
double gme_lanczos_resampler_get_rate( void * );
int gme_lanczos_resampler_get_sample_count(void *);
void gme_lanczos_resampler_get_sample(void *, short * l, short * r);
void gme_lanczos_resampler_remove_sample(void *);

#ifdef __cplusplus
}
#endif

#endif
