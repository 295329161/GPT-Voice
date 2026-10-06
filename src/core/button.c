#include "button.h"

void button_init(button_t *button, uint32_t now_ms) {
    *button = (button_t){.changed_ms = now_ms, .stable_pressed = true,
                         .candidate_pressed = true};
}
bool button_pressed(button_t *button, bool pressed, uint32_t now_ms) {
    if (pressed != button->candidate_pressed) {
        button->candidate_pressed = pressed;
        button->changed_ms = now_ms;
    }
    if ((uint32_t)(now_ms - button->changed_ms) < 30 ||
        button->stable_pressed == button->candidate_pressed)
        return false;
    button->stable_pressed = button->candidate_pressed;
    if (!button->stable_pressed) {
        button->armed = true;
        return false;
    }
    bool fire = button->armed;
    button->armed = false;
    return fire;
}
