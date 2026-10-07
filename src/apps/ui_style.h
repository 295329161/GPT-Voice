#pragma once
#include "lvgl.h"

/* Shared surfaces and accents. Navigation keeps its own invisible hit target. */
#define UI_BG       0x101820
#define UI_SURFACE  0x1a2530
#define UI_RAISED   0x23313e
#define UI_BORDER   0x2c3b48
#define UI_TEXT     0xeaf0f5
#define UI_MUTED    0x98a9b7
#define UI_ACCENT   0x7ad6c5
#define UI_INK      0x173c39
#define UI_PAPER    0xf8f3f1
#define UI_PAPER_TEXT 0x382e37
#define UI_ROSE     0xcf5c7e

void ui_style_button(lv_obj_t *obj);
void ui_style_ghost(lv_obj_t *obj, uint32_t color);
void ui_style_primary(lv_obj_t *obj);
void ui_style_panel(lv_obj_t *obj);
void ui_style_list(lv_obj_t *obj);
void ui_style_list_item(lv_obj_t *obj);
void ui_style_slider(lv_obj_t *obj);
