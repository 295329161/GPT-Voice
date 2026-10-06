#include "core/button.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    button_t b;
    button_init(&b, 0);
    assert(!button_pressed(&b, true, 1000)); // Held during startup is ignored.
    assert(!button_pressed(&b, false, 1010));
    assert(!button_pressed(&b, false, 1040));
    // Press bounce never toggles until the level has settled.
    assert(!button_pressed(&b, true, 1050));
    assert(!button_pressed(&b, false, 1060));
    assert(!button_pressed(&b, true, 1070));
    assert(!button_pressed(&b, true, 1099));
    assert(button_pressed(&b, true, 1100));
    assert(!button_pressed(&b, true, 10000)); // No repeat on long hold.
    // A bouncing release must not arm a duplicate press.
    assert(!button_pressed(&b, false, 10010));
    assert(!button_pressed(&b, true, 10020));
    assert(!button_pressed(&b, true, 10050));
    assert(!button_pressed(&b, false, 10060));
    assert(!button_pressed(&b, false, 10090));
    assert(!button_pressed(&b, true, 10100));
    assert(button_pressed(&b, true, 10130));
    // The millisecond counter wrapping does not suppress the next press.
    button_init(&b, UINT32_MAX - 100);
    assert(!button_pressed(&b, false, UINT32_MAX - 90));
    assert(!button_pressed(&b, false, UINT32_MAX - 60));
    assert(!button_pressed(&b, true, UINT32_MAX - 10));
    assert(button_pressed(&b, true, 20));
    puts("BOOT debounce, startup hold, release bounce, no repeat and clock wrap PASS");
}
