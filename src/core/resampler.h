#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct {
    float taps[3][32], history[32];
    unsigned position, phase, input_step, output_step;
    uint64_t input_count, next_time;
} resampler_t;
void resampler_init(resampler_t *r, unsigned from, unsigned to);
size_t resampler_process(resampler_t *r, const int16_t *in, size_t n, int16_t *out,
                         size_t capacity);
