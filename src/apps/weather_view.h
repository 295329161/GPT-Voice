#pragma once
#include "core/terminal.h"
#include "lvgl.h"
void weather_view_create(lv_obj_t *parent);
void weather_view_update(const terminal_state_t *s);
void weather_view_animate(void);
void weather_view_preview(const terminal_state_t *s, int code, bool night_preview);
