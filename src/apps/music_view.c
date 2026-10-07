#include "music_view.h"
#include "shell.h"
#include "ui_style.h"
#include "assets/ui_art.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *heading, *title, *subtitle, *play_symbol, *position, *volume, *volume_symbol;
static char current_path[PATH_SIZE];

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int width,
                       uint32_t color, const lv_font_t *font) {
    lv_obj_t *o = ui_label(parent, text, x, y, width);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    if (font) lv_obj_set_style_text_font(o, font, 0);
    return o;
}
static lv_obj_t *shape(lv_obj_t *parent, int x, int y, int size, uint32_t color, int radius) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, size, size);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
static lv_obj_t *button(lv_obj_t *parent, const char *symbol, int x, int y, int size,
                        lv_event_cb_t cb, void *data, bool primary) {
    lv_obj_t *o = lv_btn_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, size, size);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(primary ? UI_ROSE : UI_PAPER_TEXT), 0);
    lv_obj_set_style_bg_opa(o, primary ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(o, primary ? LV_OPA_80 : LV_OPA_10, LV_STATE_PRESSED);
    lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, data);
    lv_obj_t *text = label(o, symbol, 0, 0, size, primary ? 0xffffff : 0x2a2028,
                           primary ? &lv_font_montserrat_24 : &lv_font_montserrat_20);
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(text);
    return text;
}
static void toggle(lv_event_t *e) {
    (void)e;
    terminal_submit(JOB_MUSIC_TOGGLE, NULL, NULL, 0);
}
static void skip(lv_event_t *e) {
    terminal_submit(JOB_MUSIC_NEXT, NULL, NULL, (intptr_t)lv_event_get_user_data(e));
}
static void volume_changed(lv_event_t *e) {
    char value[12];
    snprintf(value, sizeof(value), "%d", (int)lv_slider_get_value(lv_event_get_target(e)));
    terminal_submit(JOB_LEVELS, value, NULL, 1);
}
void music_view_create(lv_obj_t *parent, lv_event_cb_t back, lv_event_cb_t browse) {
    current_path[0] = '\0';
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(parent, lv_color_hex(UI_PAPER), 0);
    lv_obj_set_style_bg_grad_color(parent, lv_color_hex(0xefdee2), 0);
    lv_obj_set_style_bg_grad_dir(parent, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    ui_back_button(parent, back, UI_PAPER_TEXT);
    heading = label(parent, "音乐", 94, 11, 132, 0x33242e, NULL);
    lv_obj_set_style_text_align(heading, LV_TEXT_ALIGN_CENTER, 0);
    button(parent, LV_SYMBOL_LIST, 244, 0, 40, browse, NULL, false);

    lv_obj_t *cover = shape(parent, 18, 49, 120, UI_ROSE, 18);
    lv_obj_set_style_shadow_color(cover, lv_color_hex(0x804358), 0);
    lv_obj_set_style_shadow_width(cover, 12, 0);
    lv_obj_set_style_shadow_opa(cover, 25, 0);
    lv_obj_set_style_shadow_ofs_y(cover, 4, 0);
    lv_obj_set_style_clip_corner(cover, true, 0);
    lv_obj_t *halo = shape(cover, 55, -30, 100, 0xffd9e2, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(halo, 25, 0);
    halo = shape(cover, -30, 80, 90, 0x8c3c5a, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(halo, 25, 0);
    lv_obj_t *note = label(cover, LV_SYMBOL_AUDIO, 0, 0, 100, 0xfff5f7, &lv_font_montserrat_48);
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(note);

    title = label(parent, "选择一首音乐", 155, 59, 148, 0x241c24, NULL);
    lv_label_set_long_mode(title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_anim_speed(title, 20, 0);
    subtitle = label(parent, "本地音乐", 155, 87, 148, 0x92727e, NULL);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_DOT);
    button(parent, LV_SYMBOL_PREV, 151, 123, 43, skip, (void *)-1, false);
    play_symbol = button(parent, LV_SYMBOL_PLAY, 203, 119, 52, toggle, NULL, true);
    button(parent, LV_SYMBOL_NEXT, 264, 123, 43, skip, (void *)1, false);

    position = label(parent, "点击播放，或从右上角选歌", 18, 183, 286, 0x92727e, NULL);
    lv_label_set_long_mode(position, LV_LABEL_LONG_DOT);
    volume_symbol = label(parent, LV_SYMBOL_VOLUME_MID, 20, 216, 28, 0x92727e, &lv_font_montserrat_16);
    label(parent, LV_SYMBOL_VOLUME_MAX, 281, 216, 24, 0x92727e, &lv_font_montserrat_16);
    volume = lv_slider_create(parent);
    lv_obj_set_pos(volume, 57, 222);
    lv_obj_set_size(volume, 208, 4);
    lv_slider_set_range(volume, 0, 90);
    lv_slider_set_value(volume, config.volume, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(volume, lv_color_hex(0xd5c4ca), LV_PART_MAIN);
    lv_obj_set_style_bg_color(volume, lv_color_hex(0xa17c8c), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(volume, lv_color_hex(0xffffff), LV_PART_KNOB);
    lv_obj_set_style_pad_all(volume, 5, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(volume, 0, LV_PART_KNOB);
    lv_obj_set_ext_click_area(volume, 12);
    lv_obj_add_event_cb(volume, volume_changed, LV_EVENT_RELEASED, NULL);
}
void music_view_update(const terminal_state_t *s) {
    char text[PATH_SIZE];
    if (strcmp(current_path, s->music_path)) {
        snprintf(current_path, sizeof(current_path), "%s", s->music_path);
        const char *name = strrchr(current_path, '/');
        snprintf(text, sizeof(text), "%s", name ? name + 1 : current_path);
        char *ext = strrchr(text, '.');
        if (ext) *ext = '\0';
        lv_label_set_text(title, *text ? text : "选择一首音乐");
        const char *suffix = strrchr(current_path, '.');
        lv_label_set_text(subtitle, suffix && !strcasecmp(suffix, ".wav") ? "本地音乐 / WAV" : "本地音乐 / MP3");
    }
    lv_label_set_text(heading, s->music_playing ? "正在播放" : s->music_paused ? "已暂停" : "音乐");
    lv_label_set_text(play_symbol, s->music_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    if (s->music_path[0]) {
        int64_t elapsed = s->music_elapsed_ms;
        if (s->music_playing) elapsed += esp_timer_get_time() / 1000 - s->music_started_ms;
        unsigned seconds = elapsed > 0 ? (unsigned)(elapsed / 1000) : 0;
        snprintf(text, sizeof(text), "%s   %u:%02u", s->music_status, seconds / 60, seconds % 60);
        lv_label_set_text(position, text);
    } else lv_label_set_text(position, s->mounted ? "点击播放，或从右上角选歌" : "插入 SD 卡，开始听音乐");
    if (!lv_obj_has_state(volume, LV_STATE_PRESSED))
        lv_slider_set_value(volume, config.volume, LV_ANIM_OFF);
    lv_label_set_text(volume_symbol, config.volume ? LV_SYMBOL_VOLUME_MID : LV_SYMBOL_MUTE);
}
