#include "shell.h"
#include "core/terminal.h"
#include "esp32_s3_szp.h"
#include "esp_heap_caps.h"
#include "weather_view.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
enum {
    HOME,
    MENU,
    SETTINGS,
    MUSIC,
    GAMES,
    PICTURES,
    VOICE,
    FILES,
    HA,
    DISPLAY,
    SOUND,
    WIFI,
    BLE,
    WEATHER,
    WEB,
    ABOUT,
    SHOOTER,
    TILES,
    FLOOD,
    FILE_DETAIL,
    IMAGE_VIEW
};
static void settings_page(void), music_page(void), games_page(void), pictures_page(void),
    voice_page(void), files_page(void), ha_page(void);
static void voice_enter(void) {
    voice_request(true);
}
static void voice_leave(void) {
    voice_request(false);
}
static const terminal_app_t defaults[] = {
    {.id = SETTINGS,
     .name = "设置",
     .icon = LV_SYMBOL_SETTINGS,
     .color = 0x90a4c1,
     .create = settings_page},
    {.id = MUSIC, .name = "音乐", .icon = LV_SYMBOL_AUDIO, .color = 0xe97cac, .create = music_page},
    {.id = GAMES, .name = "游戏", .icon = LV_SYMBOL_PLAY, .color = 0xa58af9, .create = games_page},
    {.id = PICTURES,
     .name = "图片",
     .icon = LV_SYMBOL_IMAGE,
     .color = 0xf4b85b,
     .create = pictures_page},
    {.id = VOICE,
     .name = "GPT Voice",
     .icon = LV_SYMBOL_CALL,
     .color = 0x52d7be,
     .create = voice_page,
     .enter = voice_enter,
     .leave = voice_leave},
    {.id = FILES,
     .name = "文件管理",
     .icon = LV_SYMBOL_DIRECTORY,
     .color = 0x68acee,
     .create = files_page},
    {.id = HA,
     .name = "Home Assistant",
     .icon = LV_SYMBOL_HOME,
     .color = 0x4bc7e8,
     .create = ha_page}};
static const terminal_app_t *registry[24];
static int registry_count;
#define MENU_PAGE_SIZE 4
bool shell_register_app(const terminal_app_t *app) {
    if (!app || !app->create || !app->name || !app->icon || registry_count >= 24)
        return false;
    for (int i = 0; i < registry_count; i++)
        if (registry[i]->id == app->id)
            return false;
    registry[registry_count++] = app;
    return true;
}
static const terminal_app_t *find_app(int id) {
    for (int i = 0; i < registry_count; i++)
        if (registry[i]->id == id)
            return registry[i];
    return NULL;
}
static lv_obj_t *screen, *content, *notice, *heading, *info, *list, *edit_a, *edit_b, *keyboard,
    *picture, *top_wifi, *game_bar;
LV_FONT_DECLARE(terminal_cjk);
static lv_font_t font;
static int active = HOME, pending = -1, menu_page;
static int history[12], history_count;
static bool going_back;
static unsigned file_generation, wifi_generation, ble_generation;
static char selected[PATH_SIZE], image_source[PATH_SIZE + 3];
static bool selected_dir, file_held;
static uint64_t selected_size;
static terminal_state_t *snapshot;
static int confirm_action;
static bool dialog;
static bool voice_follow_latest, voice_dragging;
lv_obj_t *shell_content(void) {
    return content;
}
static char target_dir[PATH_SIZE] = "/sdcard";
static void navigate(lv_event_t *e) {
    shell_open((intptr_t)lv_event_get_user_data(e));
}
lv_obj_t *ui_label(lv_obj_t *p, const char *s, int x, int y, int width) {
    lv_obj_t *o = lv_label_create(p);
    lv_label_set_text(o, s);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_width(o, width);
    lv_label_set_long_mode(o, LV_LABEL_LONG_WRAP);
    return o;
}
lv_obj_t *ui_button(lv_obj_t *p, const char *s, int x, int y, int w, lv_event_cb_t cb, void *data) {
    lv_obj_t *b = lv_btn_create(p);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, 30);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x24364d), 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, s);
    lv_obj_center(l);
    if (cb)
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, data);
    return b;
}
static void back(lv_event_t *e) {
    going_back = true;
    pending = history_count ? history[--history_count] : HOME;
}
void shell_open(int app) {
    pending = app;
    going_back = false;
}
static void action(lv_event_t *e) {
    terminal_submit((intptr_t)lv_event_get_user_data(e), NULL, NULL, 0);
}
static void gesture(lv_event_t *e) {
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if ((active == HOME) && (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT))
        shell_open(MENU);
    else if (active == MENU && (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT)) {
        int pages = (registry_count + MENU_PAGE_SIZE - 1) / MENU_PAGE_SIZE;
        menu_page = (menu_page + (dir == LV_DIR_LEFT ? 1 : pages - 1)) % pages;
        shell_open(MENU);
    }
    if ((active == HOME || active == MENU) && (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT))
        lv_indev_wait_release(lv_indev_get_act());
}
static void keyboard_done(lv_event_t *e) {
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}
static void focus(lv_event_t *e) {
    lv_keyboard_set_textarea(keyboard, lv_event_get_target(e));
    lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(keyboard);
}
static lv_obj_t *input(const char *hint, int y, int max, bool secret) {
    lv_obj_t *o = lv_textarea_create(content);
    lv_obj_set_pos(o, 0, y);
    lv_obj_set_size(o, 294, 37);
    lv_textarea_set_one_line(o, true);
    lv_textarea_set_max_length(o, max);
    lv_textarea_set_placeholder_text(o, hint);
    lv_textarea_set_password_mode(o, secret);
    lv_obj_add_event_cb(o, focus, LV_EVENT_FOCUSED, NULL);
    return o;
}
static lv_obj_t *decoration(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color,
                            int radius) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, 255, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
static void home_page(void) {
    weather_view_create(content);
}
// Fixed pages must not enter LVGL's scrolling state, which suppresses gestures.
static void gesture_tree(lv_obj_t *o) {
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_GESTURE_BUBBLE);
    for (unsigned i = 0; i < lv_obj_get_child_cnt(o); i++)
        gesture_tree(lv_obj_get_child(o, i));
}
static void menu_more(lv_event_t *e) {
    menu_page = (menu_page + 1) % ((registry_count + MENU_PAGE_SIZE - 1) / MENU_PAGE_SIZE);
    shell_open(MENU);
}
static void menu_create(void) {
    int start = menu_page * MENU_PAGE_SIZE;
    int count = registry_count - start;
    if (count > MENU_PAGE_SIZE)
        count = MENU_PAGE_SIZE;
    for (int k = 0; k < count; k++) {
        int i = start + k;
        int x = (k % 2) * 156;
        if (count == 3 && k == 2)
            x = 78;
        lv_obj_t *b = ui_button(content, "", x, (k / 2) * 96, 148, navigate,
                                (void *)(intptr_t)registry[i]->id);
        lv_obj_set_height(b, 90);
        lv_obj_set_style_pad_all(b, 0, 0);
        lv_obj_set_style_radius(b, 16, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x1b2b40), 0);
        lv_obj_set_style_bg_grad_color(b, lv_color_hex(0x243d53), 0);
        lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_VER, 0);
        lv_obj_t *icon = ui_label(b, registry[i]->icon, 0, 5, 148);
        lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(icon, lv_color_hex(registry[i]->color), 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);
        lv_obj_t *name = ui_label(b, registry[i]->name, 0, 65, 148);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        if (registry[i]->id == HA)
            lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
    }
    int pages = (registry_count + MENU_PAGE_SIZE - 1) / MENU_PAGE_SIZE;
    lv_obj_t *pager = ui_button(content, "", 100, 186, 104, menu_more, NULL);
    lv_obj_set_height(pager, 14);
    lv_obj_set_style_bg_opa(pager, 0, 0);
    lv_obj_set_style_pad_all(pager, 0, 0);
    for (int i = 0; i < pages; i++)
        decoration(pager, 32 + i * 24, 4, i == menu_page ? 16 : 6, 5,
                   i == menu_page ? 0xc7e7ff : 0x506780, 3);
}
static void settings_page(void) {
    const char *names[] = {"显示亮度", "声音音量",       "Wi-Fi 连接", "蓝牙扫描与配对",
                           "天气城市", "网页与语音配置", "设备信息"};
    for (int i = 0; i < 7; i++)
        ui_button(content, names[i], 0, i * 39, 294, navigate, (void *)(intptr_t)(DISPLAY + i));
}
static void slider_changed(lv_event_t *e) {
    int target = (intptr_t)lv_event_get_user_data(e);
    int n = lv_slider_get_value(lv_event_get_target(e));
    char b[16];
    snprintf(b, sizeof(b), "%d", n);
    terminal_submit(JOB_LEVELS, b, NULL, target);
}
static void level_page(bool sound) {
    ui_label(content, sound ? "扬声器音量" : "屏幕亮度", 4, 12, 290);
    lv_obj_t *s = lv_slider_create(content);
    lv_obj_set_pos(s, 15, 70);
    lv_obj_set_size(s, 265, 18);
    lv_slider_set_range(s, sound ? 0 : 5, sound ? 90 : 100);
    lv_slider_set_value(s, sound ? config.volume : config.brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(s, slider_changed, LV_EVENT_RELEASED, (void *)(intptr_t)sound);
    info = ui_label(content, "", 4, 110, 290);
}
static void wifi_connect(lv_event_t *e) {
    terminal_submit(JOB_WIFI_CONNECT, lv_textarea_get_text(edit_a), lv_textarea_get_text(edit_b),
                    0);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}
static void wifi_select(lv_event_t *e) {
    int i = (intptr_t)lv_event_get_user_data(e);
    if (i < snapshot->wifi_count)
        lv_textarea_set_text(edit_a, snapshot->wifi_names[i]);
}
static void wifi_page(void) {
    ui_button(content, "扫描", 0, 0, 80, action, (void *)JOB_WIFI_SCAN);
    ui_button(content, "连接", 88, 0, 80, wifi_connect, NULL);
    info = ui_label(content, "", 177, 4, 122);
    edit_a = input("SSID", 38, 32, false);
    edit_b = input("Wi-Fi 密码", 81, 63, true);
    lv_textarea_set_text(edit_a, config.ssid);
    list = lv_list_create(content);
    lv_obj_set_pos(list, 0, 124);
    lv_obj_set_size(list, 294, 160);
    wifi_generation = ~0u;
    terminal_submit(JOB_WIFI_SCAN, NULL, NULL, 0);
}
static void ble_select(lv_event_t *e) {
    terminal_submit(JOB_BLE_PAIR, NULL, NULL, (intptr_t)lv_event_get_user_data(e));
}
static void ble_page(void) {
    ui_button(content, "扫描 BLE", 0, 0, 118, action, (void *)JOB_BLE_SCAN);
    info = ui_label(content, "", 0, 38, 294);
    list = lv_list_create(content);
    lv_obj_set_pos(list, 0, 85);
    lv_obj_set_size(list, 294, 190);
    ble_generation = ~0u;
}
static void city_save(lv_event_t *e) {
    terminal_submit(JOB_CITY, lv_textarea_get_text(edit_a), NULL, 0);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}
static void weather_page(void) {
    ui_label(content, "输入城市中文名或拼音", 0, 0, 294);
    edit_a = input("例如 Shanghai", 36, 79, false);
    lv_textarea_set_text(edit_a, config.city);
    ui_button(content, "查找并保存", 0, 85, 150, city_save, NULL);
    ui_label(
        content,
        "数据来自 Open-Meteo\n每 15 分钟更新，断网保留缓存。\n同名城市请在网页中使用更明确的名称。",
        0, 129, 294);
}
static void web_on(lv_event_t *e) {
    terminal_submit(JOB_WEB, NULL, NULL, !state->web_enabled);
}
static void web_page(void) {
    ui_button(content, "开启 / 关闭配置网页", 0, 0, 294, web_on, NULL);
    info = ui_label(content, "", 0, 45, 294);
    ui_label(
        content,
        "网页填写 StepFun 密钥、声音与城市。\n网页搜索使用可选 Tavily Key。\n密钥留空保留原配置。",
        0, 104, 294);
}
static void music_toggle(lv_event_t *e) {
    terminal_submit(JOB_MUSIC_TOGGLE, NULL, NULL, 0);
}
static void music_next(lv_event_t *e) {
    terminal_submit(JOB_MUSIC_NEXT, NULL, NULL, (intptr_t)lv_event_get_user_data(e));
}
static void browse_music(lv_event_t *e) {
    strcpy(target_dir, "/sdcard/Music");
    shell_open(FILES);
}
static void music_page(void) {
    info = ui_label(content, "", 0, 0, 294);
    ui_button(content, LV_SYMBOL_PREV, 0, 97, 70, music_next, (void *)-1);
    ui_button(content, "播放/暂停", 77, 97, 136, music_toggle, NULL);
    ui_button(content, LV_SYMBOL_NEXT, 220, 97, 70, music_next, (void *)1);
    ui_button(content, "音乐文件", 0, 143, 142, browse_music, NULL);
    ui_button(content, "音量", 150, 143, 142, navigate, (void *)SOUND);
}
static void games_page(void) {
    ui_button(content, "雷电突击", 0, 4, 294, navigate, (void *)SHOOTER);
    ui_button(content, "羊了个羊", 0, 47, 294, navigate, (void *)TILES);
    ui_button(content, "Color Flood", 0, 90, 294, navigate, (void *)FLOOD);
    ui_label(content, "体感灵敏度", 0, 141, 115);
    lv_obj_t *s = lv_slider_create(content);
    lv_obj_set_pos(s, 134, 151);
    lv_obj_set_size(s, 145, 10);
    lv_slider_set_range(s, 1, 5);
    lv_slider_set_value(s, config.sensitivity, LV_ANIM_OFF);
    lv_obj_add_event_cb(s, slider_changed, LV_EVENT_RELEASED, (void *)2);
}
static void voice_scroll(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        voice_dragging = true;
        voice_follow_latest = false;
    } else {
        if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
            voice_dragging = false;
        if (!voice_dragging)
            voice_follow_latest = lv_obj_get_scroll_bottom(lv_event_get_target(e)) <= 12;
    }
}
static void voice_latest(lv_event_t *e) {
    (void)e;
    voice_follow_latest = true;
    voice_dragging = false;
    lv_obj_update_layout(list);
    lv_obj_scroll_to_y(list, LV_COORD_MAX, LV_ANIM_OFF);
}
static void voice_page(void) {
    voice_follow_latest = true;
    voice_dragging = false;
    info = ui_label(content, "正在连接…", 0, 0, 232);
    ui_button(content, "最新", 240, 0, 54, voice_latest, NULL);
    list = lv_obj_create(content);
    lv_obj_set_pos(list, 0, 38);
    lv_obj_set_size(list, 294, 147);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_add_event_cb(list, voice_scroll, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(list, voice_scroll, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(list, voice_scroll, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(list, voice_scroll, LV_EVENT_PRESS_LOST, NULL);
    ui_label(list, "", 0, 0, 258);
}
static void ha_page(void) {
    ui_label(content, LV_SYMBOL_HOME, 119, 28, 60);
    ui_label(content, "Home Assistant\n开发中", 55, 83, 230);
}
static void file_select(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED && file_held) {
        file_held = false;
        return;
    }
    if (lv_event_get_code(e) != LV_EVENT_CLICKED && lv_event_get_code(e) != LV_EVENT_LONG_PRESSED)
        return;
    int i = (intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= snapshot->file_count)
        return;
    file_entry_t *f = &snapshot->files[i];
    if (snprintf(selected, sizeof(selected), "%s/%s", snapshot->directory, f->name) >=
        sizeof(selected)) {
        terminal_notice("路径过长");
        return;
    }
    selected_dir = f->directory;
    selected_size = f->size;
    if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
        file_held = true;
        shell_open(FILE_DETAIL);
        return;
    }
    if (f->directory) {
        strcpy(target_dir, selected);
        terminal_submit(JOB_LIST, target_dir, NULL, 0);
    } else
        shell_open(FILE_DETAIL);
}
static void up(lv_event_t *e) {
    char path[PATH_SIZE];
    strcpy(path, snapshot->directory);
    if (strcmp(path, "/sdcard")) {
        char *s = strrchr(path, '/');
        if (s)
            *s = 0;
        strcpy(target_dir, path);
        terminal_submit(JOB_LIST, path, NULL, 0);
    }
}
static void refresh_files(lv_event_t *e) {
    terminal_submit(JOB_LIST, target_dir, NULL, 0);
}
static void edit_done(lv_event_t *e) {
    const char *name = lv_textarea_get_text(edit_a);
    if (!storage_name_valid(name)) {
        terminal_notice("文件名无效");
        return;
    }
    if (confirm_action == JOB_MKDIR) {
        char path[PATH_SIZE];
        if (snprintf(path, sizeof(path), "%s/%s", target_dir, name) < sizeof(path))
            terminal_submit(JOB_MKDIR, path, NULL, 0);
    } else
        terminal_submit(JOB_RENAME, selected, name, 0);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    shell_open(FILES);
}
static void name_dialog(lv_event_t *e) {
    confirm_action = (intptr_t)lv_event_get_user_data(e);
    dialog = true;
    lv_obj_clean(content);
    ui_label(content, confirm_action == JOB_MKDIR ? "新文件夹名称" : "新的文件名", 0, 0, 294);
    edit_a = input("名称", 36, 255, false);
    ui_button(content, "确定", 0, 85, 140, edit_done, NULL);
}
static void delete_confirm(lv_event_t *e) {
    terminal_submit(JOB_DELETE, selected, NULL, 0);
    shell_open(FILES);
}
static void delete_ask(lv_event_t *e) {
    dialog = true;
    lv_obj_clean(content);
    ui_label(content, "确认删除？操作不能撤销。", 0, 0, 294);
    ui_label(content, selected + 8, 0, 40, 294);
    ui_button(content, "确认删除", 0, 122, 140, delete_confirm, NULL);
    ui_button(content, "取消", 154, 122, 140, navigate, (void *)FILES);
}
static void files_page(void) {
    ui_button(content, "挂载", 0, 0, 66, action, (void *)JOB_MOUNT);
    ui_button(content, "卸载", 73, 0, 66, action, (void *)JOB_EJECT);
    ui_button(content, "上级", 146, 0, 66, up, NULL);
    ui_button(content, "刷新", 219, 0, 73, refresh_files, NULL);
    info = ui_label(content, "", 0, 37, 294);
    list = lv_list_create(content);
    lv_obj_set_pos(list, 0, 61);
    lv_obj_set_size(list, 294, 160);
    ui_button(content, "新建文件夹", 0, 228, 142, name_dialog, (void *)JOB_MKDIR);
    file_generation = ~0u;
    if (state->mounted)
        terminal_submit(JOB_LIST, target_dir, NULL, 0);
}
static void pictures_page(void) {
    strcpy(target_dir, "/sdcard/Pictures");
    files_page();
}
static void open_file(lv_event_t *e) {
    if (selected_dir) {
        strcpy(target_dir, selected);
        shell_open(FILES);
        return;
    }
    const char *x = strrchr(selected, '.');
    if (!x)
        return;
    if (!strcasecmp(x, ".mp3") || !strcasecmp(x, ".wav")) {
        terminal_submit(JOB_MUSIC_PLAY, selected, NULL, 0);
        shell_open(MUSIC);
    } else if (!strcasecmp(x, ".jpg") || !strcasecmp(x, ".jpeg") || !strcasecmp(x, ".png"))
        shell_open(IMAGE_VIEW);
    else
        terminal_notice("此文件类型暂不支持打开");
}
static void file_detail(void) {
    ui_label(content, selected + 8, 0, 0, 294);
    char detail[64];
    snprintf(detail, sizeof(detail), selected_dir ? "文件夹" : "大小：%llu 字节",
             (unsigned long long)selected_size);
    ui_label(content, detail, 0, 40, 294);
    ui_button(content, "打开", 0, 66, 294, open_file, NULL);
    ui_button(content, "重命名", 0, 109, 141, name_dialog, (void *)JOB_RENAME);
    ui_button(content, "删除…", 151, 109, 141, delete_ask, NULL);
}
static void image_next(lv_event_t *e) {
    int direction = (intptr_t)lv_event_get_user_data(e), found = -1;
    for (int i = 0; i < snapshot->file_count; i++) {
        const char *name = strrchr(selected, '/');
        if (name && !strcmp(name + 1, snapshot->files[i].name)) {
            found = i;
            break;
        }
    }
    for (int k = 1; k <= snapshot->file_count; k++) {
        int i = (found + direction * k + snapshot->file_count * 2) % snapshot->file_count;
        const char *name = snapshot->files[i].name, *ext = strrchr(name, '.');
        if (ext &&
            (!strcasecmp(ext, ".jpg") || !strcasecmp(ext, ".jpeg") || !strcasecmp(ext, ".png"))) {
            if (snprintf(selected, sizeof(selected), "%s/%s", snapshot->directory, name) >=
                sizeof(selected))
                return;
            shell_open(IMAGE_VIEW);
            return;
        }
    }
}
static void image_page(void) {
    struct stat st;
    if (stat(selected, &st) || st.st_size > 1024 * 1024) {
        ui_label(content, "图片不可用或超过 1 MB", 0, 25, 294);
        return;
    }
    snprintf(image_source, sizeof(image_source), "S:%s", selected + 7);
    lv_img_header_t h;
    if (lv_img_decoder_get_info(image_source, &h) != LV_RES_OK || !h.w || !h.h ||
        (uint64_t)h.w * h.h > 1024 * 1024) {
        ui_label(content, "图片格式或尺寸不支持", 0, 25, 294);
        return;
    }
    picture = lv_img_create(content);
    lv_img_set_src(picture, image_source);
    uint32_t zoom = 256;
    if (h.w > 294 || h.h > 145) {
        uint32_t a = 294 * 256 / h.w, b = 145 * 256 / h.h;
        zoom = a < b ? a : b;
    }
    lv_img_set_zoom(picture, zoom);
    lv_obj_set_pos(picture, (294 - (int)h.w) / 2, (145 - (int)h.h) / 2);
    ui_button(content, "上一张", 0, 151, 140, image_next, (void *)-1);
    ui_button(content, "下一张", 154, 151, 140, image_next, (void *)1);
}
static void render(int page) {
    if (page != active && !going_back) {
        if (page == HOME)
            history_count = 0;
        else if (page == MENU) {
            history_count = 1;
            history[0] = HOME;
        } else if (history_count < 12)
            history[history_count++] = active;
    }
    going_back = false;
    const terminal_app_t *previous = find_app(active);
    if (previous) {
        if (previous->leave)
            previous->leave();
        if (previous->destroy)
            previous->destroy();
    }
    games_destroy();
    dialog = false;
    active = page;
    lv_obj_clean(screen);
    info = list = picture = NULL;
    bool game = page == SHOOTER || page == TILES || page == FLOOD;
    heading = ui_label(screen, "", 46, 9, 185);
    game_bar = screen;
    if (page != HOME) {
        ui_button(screen, LV_SYMBOL_LEFT, 4, 3, 32, back, (void *)MENU);
        if (!game)
            ui_button(screen, LV_SYMBOL_HOME, 244, 3, 34, navigate, (void *)HOME);
    }
    content = lv_obj_create(screen);
    lv_obj_remove_style_all(content);
    lv_obj_set_pos(content, 9, 40);
    lv_obj_set_size(content, 302, 190);
    if (page == MENU) {
        lv_obj_set_pos(content, 8, 36);
        lv_obj_set_size(content, 304, 200);
    }
    if (game) {
        lv_obj_set_pos(content, 4, 36);
        lv_obj_set_size(content, 312, 200);
        lv_obj_add_flag(heading, LV_OBJ_FLAG_HIDDEN);
    }
    if (page == HOME) {
        lv_obj_set_pos(content, 0, 0);
        lv_obj_set_size(content, 320, 240);
    }
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_style_text_font(content, &font, 0);
    lv_obj_set_style_text_color(content, lv_color_hex(0xe6edf7), 0);
    notice = ui_label(screen, "", 5, 222, 310);
    lv_obj_set_style_bg_color(notice, lv_color_hex(0x101827), 0);
    lv_obj_set_style_bg_opa(notice, 255, 0);
    lv_obj_set_style_text_color(notice, lv_color_hex(0xa7bed8), 0);
    lv_label_set_long_mode(notice, LV_LABEL_LONG_DOT);
    lv_obj_set_height(notice, 18);
    if (page == HOME || page == MENU || page == SHOOTER || page == TILES || page == FLOOD)
        lv_obj_add_flag(notice, LV_OBJ_FLAG_HIDDEN);
    keyboard = lv_keyboard_create(screen);
    lv_obj_set_size(keyboard, 320, 125);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(keyboard, keyboard_done, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(keyboard, keyboard_done, LV_EVENT_CANCEL, NULL);
    const char *title = "桌面终端";
    for (int i = 0; i < registry_count; i++)
        if (registry[i]->id == page) {
            title = registry[i]->name;
            registry[i]->create();
            if (registry[i]->enter)
                registry[i]->enter();
            goto ready;
        }
    switch (page) {
    case HOME:
        title = "天气与时间";
        home_page();
        break;
    case MENU:
        title = "应用菜单";
        menu_create();
        break;
    case DISPLAY:
        title = "亮度";
        level_page(false);
        break;
    case SOUND:
        title = "音量";
        level_page(true);
        break;
    case WIFI:
        title = "Wi-Fi";
        wifi_page();
        break;
    case BLE:
        title = "蓝牙";
        ble_page();
        break;
    case WEATHER:
        title = "天气城市";
        weather_page();
        break;
    case WEB:
        title = "网页配置";
        web_page();
        break;
    case ABOUT:
        title = "设备信息";
        info = ui_label(content, "", 0, 8, 294);
        break;
    case SHOOTER:
        title = "雷电突击";
        games_create(content, game_bar, 0);
        break;
    case TILES:
        title = "羊了个羊";
        games_create(content, game_bar, 1);
        break;
    case FLOOD:
        title = "Color Flood";
        games_create(content, game_bar, 2);
        break;
    case FILE_DETAIL:
        title = "文件操作";
        file_detail();
        break;
    case IMAGE_VIEW:
        title = "图片";
        image_page();
        break;
    }
ready:
    top_wifi = ui_label(screen, LV_SYMBOL_WIFI, 290, 10, 26);
    lv_obj_set_style_text_font(top_wifi, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_opa(top_wifi, snapshot->online ? 255 : 55, 0);
    lv_label_set_text(heading, title);
    if (page == HOME) {
        lv_obj_add_flag(heading, LV_OBJ_FLAG_HIDDEN);
        weather_view_update(snapshot);
    }
    if (page == HOME || page == MENU)
        gesture_tree(content);
}
static void tick(lv_timer_t *timer) {
    state_lock();
    *snapshot = *state;
    state_unlock();
    if (pending >= 0) {
        int p = pending;
        pending = -1;
        render(p);
    }
    lv_obj_set_style_text_opa(top_wifi, snapshot->online ? 255 : 55, 0);
    char text[768];
    lv_label_set_text(notice, active == HOME ? "天气数据 Open-Meteo" : snapshot->notice);
    if (dialog)
        return;
    if (active == HOME)
        weather_view_update(snapshot);
    if (active == DISPLAY || active == SOUND) {
        snprintf(text, sizeof(text), "当前：%d",
                 active == DISPLAY ? config.brightness : config.volume);
        lv_label_set_text(info, text);
    }
    if (active == FILES || active == PICTURES) {
        if (snapshot->mounted) {
            snprintf(text, sizeof(text), "%s  可用 %llu MB", snapshot->directory,
                     (unsigned long long)(snapshot->sd_free / 1048576));
            lv_label_set_text(info, text);
        } else
            lv_label_set_text(info, "SD 卡未挂载");
        if (file_generation != snapshot->files_generation) {
            file_generation = snapshot->files_generation;
            lv_obj_clean(list);
            for (int i = 0; i < snapshot->file_count; i++) {
                file_entry_t *f = &snapshot->files[i];
                lv_obj_t *b = lv_list_add_btn(
                    list, f->directory ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE, f->name);
                lv_obj_add_event_cb(b, file_select, LV_EVENT_ALL, (void *)(intptr_t)i);
            }
        }
    }
    if (active == WIFI) {
        lv_label_set_text(info, snapshot->online ? snapshot->ip : "未连接");
        if (wifi_generation != snapshot->wifi_generation) {
            wifi_generation = snapshot->wifi_generation;
            lv_obj_clean(list);
            for (int i = 0; i < snapshot->wifi_count; i++) {
                lv_obj_t *b = lv_list_add_btn(list, LV_SYMBOL_WIFI, snapshot->wifi_names[i]);
                lv_obj_add_event_cb(b, wifi_select, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            }
        }
    }
    if (active == BLE) {
        lv_label_set_text(info, snapshot->ble_status);
        if (ble_generation != snapshot->ble_generation) {
            ble_generation = snapshot->ble_generation;
            lv_obj_clean(list);
            for (int i = 0; i < snapshot->ble_count; i++) {
                snprintf(text, sizeof(text), "%s  %d dBm", snapshot->devices[i].name,
                         snapshot->devices[i].rssi);
                lv_obj_t *b = lv_list_add_btn(list, LV_SYMBOL_BLUETOOTH, text);
                lv_obj_add_event_cb(b, ble_select, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            }
        }
    }
    if (active == WEB) {
        if (snapshot->web_enabled)
            snprintf(text, sizeof(text), "http://%s\n配对码 %s（10 分钟）", snapshot->ip,
                     snapshot->web_code);
        else
            snprintf(text, sizeof(text), "配置网页已关闭\n连接 Wi-Fi 后点击开启");
        lv_label_set_text(info, text);
    }
    if (active == MUSIC) {
        const char *name = strrchr(snapshot->music_path, '/');
        snprintf(text, sizeof(text), "%s\n%s", name ? name + 1 : "SD 卡 /Music",
                 snapshot->music_status);
        lv_label_set_text(info, text);
    }
    if (active == VOICE) {
        lv_label_set_text(info, snapshot->voice_status);
        lv_obj_t *l = lv_obj_get_child(list, 0);
        if (strcmp(lv_label_get_text(l), snapshot->transcript)) {
            bool follow = voice_follow_latest;
            lv_label_set_text(l, snapshot->transcript);
            lv_obj_update_layout(list);
            if (follow)
                lv_obj_scroll_to_y(list, LV_COORD_MAX, LV_ANIM_OFF);
        }
    }
    if (active == ABOUT) {
        snprintf(text, sizeof(text),
                 "桌面终端 v0.1\nESP32-S3 · ESP-IDF 5.5\nIP: %s\n可用内存：%u KB\nPSRAM：%u KB",
                 snapshot->ip, (unsigned)esp_get_free_heap_size() / 1024,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
        lv_label_set_text(info, text);
    }
}
static void game_timer(lv_timer_t *t) {
    games_tick();
    if (active == HOME)
        weather_view_animate();
}
void shell_init(void) {
    for (unsigned i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++)
        assert(shell_register_app(&defaults[i]));
    font = lv_font_montserrat_16;
    font.fallback = &terminal_cjk;
    snapshot = heap_caps_calloc(1, sizeof(*snapshot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(snapshot);
    lv_theme_t *theme =
        lv_theme_default_init(lv_disp_get_default(), lv_palette_main(LV_PALETTE_BLUE),
                              lv_palette_main(LV_PALETTE_TEAL), true, &font);
    lv_disp_set_theme(lv_disp_get_default(), theme);
    screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101827), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0xe6edf7), 0);
    lv_obj_set_style_text_font(screen, &font, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(screen, gesture, LV_EVENT_GESTURE, NULL);
    render(HOME);
    lv_timer_create(tick, 150, NULL);
    lv_timer_create(game_timer, 20, NULL);
}
