#include "core/tilt.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void) {
    tilt_filter_t f = {0};
    tilt_filter_update(&f, 0, 0, .01f);
    for (int i = 0; i < 4; i++)
        tilt_filter_update(&f, 20, 20, .01f);
    assert(f.roll > 18 && f.pitch > 18); // >90% response in 40 ms.
    f = (tilt_filter_t){179, 0, true};
    tilt_filter_update(&f, -179, 0, .01f);
    assert(fabsf(f.roll) > 178); // No 358-degree jump across the roll seam.
    assert(tilt_speed(.5f, 3) == 0);
    assert(tilt_speed(-.5f, 3) == 0);
    assert(tilt_speed(10, 3) > 60);
    assert(tilt_speed(10, 5) > tilt_speed(10, 1));
    assert(tilt_speed(10000, 5) == 300);
    assert(tilt_speed(-10000, 5) == -300);
    puts("Tilt step response, angle wrapping, dead zone and bounded sensitivity PASS");
}
