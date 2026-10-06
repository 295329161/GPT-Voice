#pragma once
#include "lvgl.h"
#include "core/terminal.h"
void usb_transfer_menu_icon(lv_obj_t *parent);
void usb_transfer_view_create(lv_obj_t *parent);
void usb_transfer_view_update(const terminal_state_t *value);
