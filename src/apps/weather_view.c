#include "weather_view.h"
#include "assets/ui_art.h"
#include "shell.h"
#include <math.h>
#include <stdio.h>
#include <time.h>
static lv_obj_t *root, *clock_text, *date_text, *city, *temperature, *condition, *range, *wifi,
    *sun, *clouds[2], *drops[14], *stars[16], *hill;
static bool night, rain, snow, cloudy;
static lv_obj_t *shape(lv_obj_t *p, int x, int y, int w, int h, uint32_t c, int radius) {
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(c), 0);
    lv_obj_set_style_bg_opa(o, 255, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
static void show(lv_obj_t *o, bool yes) {
    if (yes)
        lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}
void weather_view_create(lv_obj_t *parent) {
    root = parent;
    lv_obj_set_style_bg_opa(root, 255, 0);
    lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_VER, 0);
    for (int i = 0; i < 16; i++) {
        stars[i] = shape(root, 155 + (i * 43) % 150, 20 + (i * 29) % 143, i % 3 ? 1 : 2,
                         i % 3 ? 1 : 2, 0xd3e6fa, 2);
    }
    sun = lv_img_create(root);
    lv_img_set_src(sun, &art_sun);
    lv_obj_set_pos(sun, 221, 40);
    for (int i = 0; i < 2; i++) {
        clouds[i] = lv_img_create(root);
        lv_img_set_src(clouds[i], &art_cloud);
        lv_obj_set_pos(clouds[i], 177 + i * 49, 94 + i * 33);
        lv_obj_set_style_img_opa(clouds[i], i ? 125 : 230, 0);
    }
    hill = shape(root, 165, 174, 230, 180, 0x568fb2, 90);
    lv_obj_set_style_bg_opa(hill, 45, 0);
    for (int i = 0; i < 14; i++)
        drops[i] = shape(root, 160 + (i * 47) % 153, 120 + (i * 23) % 100, 2, 8, 0xc4e5f7, 2);
    city = ui_label(root, "中山", 20, 13, 230);
    wifi = ui_label(root, LV_SYMBOL_WIFI, 280, 13, 28);
    clock_text = ui_label(root, "--:--", 18, 43, 210);
    lv_obj_set_style_text_font(clock_text, &lv_font_montserrat_48, 0);
    date_text = ui_label(root, "", 22, 99, 205);
    lv_obj_set_style_text_color(date_text, lv_color_hex(0xd0e1ee), 0);
    temperature = ui_label(root, "--°", 19, 133, 120);
    lv_obj_set_style_text_font(temperature, &lv_font_montserrat_48, 0);
    condition = ui_label(root, "", 130, 147, 148);
    lv_obj_t *card = shape(root, 16, 196, 288, 30, 0xffffff, 12);
    lv_obj_set_style_bg_opa(card, 22, 0);
    range = ui_label(root, "", 26, 202, 275);
    shape(root, 144, 234, 32, 3, 0xc2d7e6, 2);
}
static const char *weather_name(int c) {
    if (c == 0)
        return "晴";
    if (c <= 3)
        return "多云";
    if (c <= 48)
        return "雾";
    if (c <= 67)
        return "雨";
    if (c <= 77)
        return "雪";
    if (c <= 82)
        return "阵雨";
    if (c <= 86)
        return "阵雪";
    return "雷雨";
}
void weather_view_update(const terminal_state_t *s) {
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    char text[128];
    night = s->time_valid && (t.tm_hour < 6 || t.tm_hour >= 18);
    rain = s->weather_valid && s->weather_code >= 51 &&
           !(s->weather_code >= 71 && s->weather_code <= 77) &&
           !(s->weather_code >= 85 && s->weather_code <= 86);
    snow = s->weather_valid && ((s->weather_code >= 71 && s->weather_code <= 77) ||
                                (s->weather_code >= 85 && s->weather_code <= 86));
    cloudy = s->weather_valid && s->weather_code > 0;
    lv_obj_set_style_bg_color(root, lv_color_hex(night ? 0x142440 : rain ? 0x38546c : 0x286eab), 0);
    lv_obj_set_style_bg_grad_color(root,
                                   lv_color_hex(night  ? 0x34465f
                                                : rain ? 0x728e9f
                                                       : 0x79aec8),
                                   0);
    lv_label_set_text(city, s->weather_city[0] ? s->weather_city : "中山");
    lv_obj_set_style_text_opa(wifi, s->online ? 255 : 55, 0);
    if (s->time_valid) {
        strftime(text, sizeof(text), "%H:%M", &t);
        lv_label_set_text(clock_text, text);
        snprintf(text, sizeof(text), "%d月%d日  星期%s", t.tm_mon + 1, t.tm_mday,
                 (const char *[]){"日", "一", "二", "三", "四", "五", "六"}[t.tm_wday]);
    } else
        snprintf(text, sizeof(text), "--月--日");
    lv_label_set_text(date_text, text);
    if (s->weather_valid) {
        snprintf(text, sizeof(text), "%.0f°", s->temperature);
        lv_label_set_text(temperature, text);
        snprintf(text, sizeof(text), "%s%s", weather_name(s->weather_code),
                 (s->weather_error[0] || !s->online) ? " · 缓存" : "");
        lv_label_set_text(condition, text);
        snprintf(text, sizeof(text), "体感 %.0f°    最低 %.0f° / 最高 %.0f°", s->feels, s->low,
                 s->high);
    } else {
        lv_label_set_text(temperature, "--°");
        lv_label_set_text(condition, "天气更新中");
        snprintf(text, sizeof(text), "中山 · 实时天气");
    }
    lv_label_set_text(range, text);
    lv_img_set_src(sun, night ? &art_moon : &art_sun);
    show(sun, !rain && !snow);
    for (int i = 0; i < 2; i++)
        show(clouds[i], cloudy || i == 0);
    for (int i = 0; i < 16; i++)
        show(stars[i], night && !rain && !snow);
    for (int i = 0; i < 14; i++) {
        show(drops[i], rain || snow);
        lv_obj_set_size(drops[i], snow ? 3 : 2, snow ? 3 : 9);
    }
}
void weather_view_animate(void) {
    float t = lv_tick_get() / 1000.f;
    for (int i = 0; i < 2; i++)
        lv_obj_set_x(clouds[i], 177 + i * 49 + sinf(t * .25f + i) * 9);
    for (int i = 0; i < 14; i++)
        if (rain || snow)
            lv_obj_set_y(drops[i], 105 + (int)(i * 23 + t * (snow ? 15 : 65)) % 85);
}
