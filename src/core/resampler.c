#include "resampler.h"
#include <math.h>
#include <string.h>
void resampler_init(resampler_t *r, unsigned from, unsigned to) {
    memset(r, 0, sizeof(*r));
    unsigned a = from, b = to;
    while (b) {
        unsigned t = a % b;
        a = b;
        b = t;
    }
    r->input_step = from / a;
    r->output_step = to / a;
    float cutoff = .45f * fminf(1.f, (float)to / from);
    for (unsigned p = 0; p < r->output_step && p < 3; p++) {
        float sum = 0;
        for (int k = 0; k < 32; k++) {
            float x = k - 15.f + (float)p / r->output_step;
            float h = fabsf(x) < 1e-5 ? 2 * cutoff
                                      : sinf(2 * 3.14159265f * cutoff * x) / (3.14159265f * x);
            h *= .5f - .5f * cosf(2 * 3.14159265f * k / 31);
            r->taps[p][k] = h;
            sum += h;
        }
        for (int k = 0; k < 32; k++)
            r->taps[p][k] /= sum;
    }
}
size_t resampler_process(resampler_t *r, const int16_t *in, size_t n, int16_t *out, size_t cap) {
    size_t m = 0;
    for (size_t i = 0; i < n; i++) {
        r->history[r->position] = in[i];
        while (r->next_time / r->output_step == r->input_count) {
            float v = 0;
            unsigned phase = r->next_time % r->output_step;
            for (unsigned k = 0; k < 32; k++)
                v += r->history[(r->position + 32 - k) % 32] * r->taps[phase][k];
            if (m < cap)
                out[m++] = (int16_t)fmaxf(-32768, fminf(32767, v));
            r->next_time += r->input_step;
        }
        r->position = (r->position + 1) % 32;
        r->input_count++;
    }
    return m;
}
