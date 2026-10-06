#pragma once
#include <stdbool.h>
typedef struct {
    float roll, pitch;
    bool ready;
} tilt_filter_t;
void tilt_filter_update(tilt_filter_t *f, float roll, float pitch, float dt);
float tilt_speed(float angle, int sensitivity);
