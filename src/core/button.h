#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t changed_ms;
    bool stable_pressed, candidate_pressed, armed;
} button_t;

// Require a clean release after boot before accepting the first press.
void button_init(button_t *button, uint32_t now_ms);
bool button_pressed(button_t *button, bool pressed, uint32_t now_ms);
