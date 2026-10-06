#include "tilt.h"
#include <math.h>
void tilt_filter_update(tilt_filter_t *f, float roll, float pitch, float dt) {
    if (!f->ready) {
        f->roll = roll;
        f->pitch = pitch;
        f->ready = true;
        return;
    }
    // A time-based 12 ms filter removes small noise without the old ~70 ms lag.
    float alpha = dt / (.012f + dt);
    f->roll = remainderf(f->roll + alpha * remainderf(roll - f->roll, 360.f), 360.f);
    f->pitch += alpha * (pitch - f->pitch);
}
float tilt_speed(float angle, int sensitivity) {
    float magnitude = fabsf(angle);
    if (magnitude <= .6f)
        return 0;
    float speed = (magnitude - .6f) * (3.f + sensitivity * 1.3f);
    return copysignf(fminf(speed, 300.f), angle);
}
