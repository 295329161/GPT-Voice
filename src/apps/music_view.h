#pragma once
#include "lvgl.h"
#include "core/terminal.h"

void music_view_create(lv_obj_t *parent, lv_event_cb_t back, lv_event_cb_t browse);
void music_view_update(const terminal_state_t *snapshot);
