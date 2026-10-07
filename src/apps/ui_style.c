#include "ui_style.h"

void ui_style_button(lv_obj_t *o) {
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_color(o, lv_color_hex(UI_RAISED), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(UI_BORDER), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(o, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_radius(o, 10, 0);
    lv_obj_set_style_opa(o, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}
void ui_style_ghost(lv_obj_t *o, uint32_t color) {
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_10, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_radius(o, 12, 0);
}
void ui_style_primary(lv_obj_t *o) {
    lv_obj_set_style_bg_color(o, lv_color_hex(UI_ACCENT), 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(0x62bcad), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(o, lv_color_hex(UI_INK), 0);
}
void ui_style_panel(lv_obj_t *o) {
    lv_obj_set_style_bg_color(o, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_set_style_radius(o, 14, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(UI_TEXT), 0);
}
void ui_style_list(lv_obj_t *o) {
    ui_style_panel(o);
    lv_obj_set_style_pad_all(o, 4, 0);
    lv_obj_set_style_pad_row(o, 2, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(UI_MUTED), LV_PART_SCROLLBAR);
    lv_obj_set_style_width(o, 3, LV_PART_SCROLLBAR);
}
void ui_style_list_item(lv_obj_t *o) {
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(UI_RAISED), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 8, 0);
    lv_obj_set_style_pad_ver(o, 10, 0);
    lv_obj_set_style_pad_hor(o, 10, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(UI_TEXT), 0);
    if (lv_obj_get_child_cnt(o) > 1)
        lv_obj_set_style_text_color(lv_obj_get_child(o, 0), lv_color_hex(UI_ACCENT), 0);
}
void ui_style_slider(lv_obj_t *o) {
    lv_obj_set_height(o, 5);
    lv_obj_set_style_bg_color(o, lv_color_hex(UI_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_color(o, lv_color_hex(UI_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(o, lv_color_hex(UI_TEXT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(o, 5, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(o, 0, LV_PART_KNOB);
    lv_obj_set_ext_click_area(o, 12);
}
