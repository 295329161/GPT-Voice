#include "assets/ui_art.h"
#include "core/arcade.h"
#include "core/terminal.h"
#include "esp_random.h"
#include "shell.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static int kind = -1, steps;
static bool paused, finished;
static float zero_r, zero_p;
static uint32_t last_tick;
static lv_obj_t *board, *hud, *player, *enemy[ENEMIES], *shot[SHOTS], *drop[DROPS],
    *hostile[HOSTILE], *stars[24], *banner;
static lv_obj_t *cards[TILE_COUNT], *card_art[TILE_COUNT], *tray[TRAY_CAP], *cells[144];
static lv_obj_t *effects[EFFECTS], *pause_button, *shuffle_button, *undo_button;
static shooter_t flight;
static tiles_game_t garden;
static uint8_t flood[144];
static const uint32_t colors[] = {0xF15B64, 0xFAC858, 0x56C9A5, 0x55A9FF, 0xA887FF, 0xF599C3};
static const lv_img_dsc_t *tile_art[] = {&art_tile_0, &art_tile_1, &art_tile_2,
                                         &art_tile_3, &art_tile_4, &art_tile_5,
                                         &art_tile_6, &art_tile_7, &art_tile_8};
static lv_obj_t *rect(lv_obj_t *p, int x, int y, int w, int h, uint32_t color, int radius) {
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, 255, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
static lv_obj_t *sprite(lv_obj_t *p, const lv_img_dsc_t *src) {
    lv_obj_t *o = lv_img_create(p);
    lv_img_set_src(o, src);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}
static void visible(lv_obj_t *o, bool show) {
    if (show)
        lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}
static void calibrate(void) {
    state_lock();
    zero_r = state->roll;
    zero_p = state->pitch;
    state_unlock();
}
static void tiles_refresh(void) {
    for (int i = 0; i < TILE_COUNT; i++) {
        visible(cards[i], !garden.tiles[i].removed);
        lv_img_set_src(card_art[i], tile_art[garden.tiles[i].type]);
        bool open = tile_available(&garden, i);
        lv_obj_set_style_bg_color(cards[i], lv_color_hex(open ? 0xfff9e8 : 0xa5bba8), 0);
        lv_obj_set_style_img_opa(card_art[i], open ? 255 : 90, 0);
    }
    for (int i = 0; i < TRAY_CAP; i++) {
        visible(tray[i], i < garden.count);
        if (i < garden.count)
            lv_img_set_src(tray[i], tile_art[garden.tray[i]]);
    }
    if (garden.undo_used || !garden.undo_ready)
        lv_obj_add_state(undo_button, LV_STATE_DISABLED);
    else
        lv_obj_clear_state(undo_button, LV_STATE_DISABLED);
    if (!garden.shuffles)
        lv_obj_add_state(shuffle_button, LV_STATE_DISABLED);
    else
        lv_obj_clear_state(shuffle_button, LV_STATE_DISABLED);
    char count_text[20];
    snprintf(count_text, sizeof(count_text), "洗牌%d", garden.shuffles);
    lv_label_set_text(lv_obj_get_child(shuffle_button, 0), count_text);
    char text[64];
    snprintf(text, sizeof(text), "%d/12", garden.matches);
    lv_label_set_text(hud, text);
    visible(banner, garden.won || garden.lost);
    lv_label_set_text(banner, garden.won ? "全部消除！" : "槽位已满 / 可撤回或重开");
}
static void tile_click(lv_event_t *e) {
    if (tiles_pick(&garden, (intptr_t)lv_event_get_user_data(e)))
        tiles_refresh();
}
static void undo_cb(lv_event_t *e) {
    (void)e;
    if (tiles_undo(&garden))
        tiles_refresh();
}
static void shuffle_cb(lv_event_t *e) {
    if (tiles_shuffle(&garden)) {
        tiles_refresh();
        char s[32];
        snprintf(s, sizeof(s), "洗牌 %d", garden.shuffles);
        lv_label_set_text(lv_obj_get_child(lv_event_get_target(e), 0), s);
    }
}
static void reset(void) {
    paused = finished = false;
    steps = 0;
    last_tick = lv_tick_get();
    calibrate();
    if (kind == 0)
        shooter_init(&flight, esp_random());
    if (kind == 1) {
        tiles_init(&garden, esp_random());
        tiles_refresh();
    }
    if (kind == 2)
        for (int i = 0; i < 144; i++) {
            flood[i] = esp_random() % 6;
            lv_obj_set_style_bg_color(cells[i], lv_color_hex(colors[flood[i]]), 0);
        }
    if (kind != 1) {
        visible(banner, false);
        lv_label_set_text(lv_obj_get_child(pause_button, 0), "暂停");
    }
}
static void reset_cb(lv_event_t *e) {
    (void)e;
    reset();
}
static void pause_cb(lv_event_t *e) {
    paused = !paused;
    calibrate();
    lv_label_set_text(lv_obj_get_child(lv_event_get_target(e), 0), paused ? "继续" : "暂停");
}
static void flood_cb(lv_event_t *e) {
    if (paused || finished)
        return;
    uint8_t color = (uintptr_t)lv_event_get_user_data(e), old = flood[0];
    if (old == color)
        return;
    int queue[144], head = 0, tail = 1;
    queue[0] = 0;
    flood[0] = color;
    while (head < tail) {
        int p = queue[head++], n[4] = {p - 12, p + 12, p - 1, p + 1};
        for (int i = 0; i < 4; i++) {
            int q = n[i];
            if (q < 0 || q >= 144 || (i >= 2 && q / 12 != p / 12) || flood[q] != old)
                continue;
            flood[q] = color;
            queue[tail++] = q;
        }
    }
    steps++;
    bool win = true;
    for (int i = 0; i < 144; i++) {
        lv_obj_set_style_bg_color(cells[i], lv_color_hex(colors[flood[i]]), 0);
        if (flood[i] != color)
            win = false;
    }
    finished = win || steps >= 25;
    char s[64];
    snprintf(s, sizeof(s), win ? "成功！%d 步" : finished ? "步数用完" : "%d / 25 步", steps);
    lv_label_set_text(hud, s);
}
void games_create(lv_obj_t *parent, lv_obj_t *toolbar, int which) {
    kind = which;
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    hud = ui_label(toolbar, "", 62, 10, kind == 1 ? 50 : 106);
    lv_label_set_long_mode(hud, LV_LABEL_LONG_CLIP);
    if (kind == 1) {
        undo_button = ui_text_button(toolbar, "撤回", 116, 3, 50, undo_cb, NULL);
        shuffle_button = ui_text_button(toolbar, "洗牌", 170, 3, 58, shuffle_cb, NULL);
    } else
        pause_button = ui_text_button(toolbar, "暂停", 174, 3, 50, pause_cb, NULL);
    ui_text_button(toolbar, "重开", 232, 3, 50, reset_cb, NULL);
    board = rect(parent, 0, 0, ARENA_WIDTH, ARENA_HEIGHT, kind == 1 ? 0xc5debc : 0x0b172b, 12);
    if (kind == 0) {
        for (int i = 0; i < 24; i++) {
            stars[i] =
                rect(board, (i * 83) % ARENA_WIDTH, (i * 47) % ARENA_HEIGHT, i % 3 == 0 ? 2 : 1,
                     i % 3 == 0 ? 5 : 2, i % 3 == 0 ? 0x466681 : 0x273e58, 1);
        }
        for (int i = 0; i < SHOTS; i++)
            shot[i] = rect(board, 0, 0, 3, 10, 0x8deaff, 2);
        for (int i = 0; i < HOSTILE; i++)
            hostile[i] = rect(board, 0, 0, 5, 7, 0xffa79a, 3);
        for (int i = 0; i < ENEMIES; i++)
            enemy[i] = sprite(board, &art_enemy);
        for (int i = 0; i < DROPS; i++) {
            drop[i] = rect(board, 0, 0, 22, 22, 0x489baf, 6);
            lv_obj_set_style_border_width(drop[i], 1, 0);
            lv_obj_set_style_border_color(drop[i], lv_color_hex(0xc3f6ff), 0);
            ui_label(drop[i], "", 4, 2, 20);
        }
        player = sprite(board, &art_fighter);
        for (int i = 0; i < EFFECTS; i++) {
            effects[i] = rect(board, 0, 0, 20, 20, 0xffd594, 20);
            lv_obj_set_style_bg_opa(effects[i], 80, 0);
            lv_obj_set_style_border_width(effects[i], 2, 0);
            lv_obj_set_style_border_color(effects[i], lv_color_hex(0xffdfb3), 0);
        }
    } else if (kind == 1) {
        for (int i = 0; i < TILE_COUNT; i++) {
            int x, y;
            tile_position(i, &x, &y);
            cards[i] = rect(board, x, y, 42, 45, 0xfff9e8, 7);
            lv_obj_set_style_shadow_width(cards[i], 3, 0);
            lv_obj_set_style_shadow_ofs_y(cards[i], 2, 0);
            lv_obj_set_style_shadow_color(cards[i], lv_color_hex(0x6c8c70), 0);
            lv_obj_add_flag(cards[i], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(cards[i], tile_click, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            card_art[i] = sprite(cards[i], tile_art[0]);
            lv_obj_set_pos(card_art[i], 3, 3);
        }
        rect(board, 4, 153, 304, 46, 0x73987b, 10);
        for (int i = 0; i < TRAY_CAP; i++) {
            rect(board, 7 + i * 43, 158, 40, 37, 0xe2ecd0, 5);
            tray[i] = sprite(board, tile_art[0]);
            lv_obj_set_pos(tray[i], 9 + i * 43, 158);
        }
    } else {
        for (int r = 0; r < 12; r++)
            for (int c = 0; c < 12; c++)
                cells[r * 12 + c] = rect(board, c * 16, r * 16, 15, 15, colors[0], 2);
        for (int i = 0; i < 6; i++) {
            lv_obj_t *b = ui_button(board, "", 218 + (i % 2) * 47, 8 + (i / 2) * 64, 40, flood_cb,
                                    (void *)(uintptr_t)i);
            lv_obj_set_height(b, 52);
            lv_obj_set_style_bg_color(b, lv_color_hex(colors[i]), 0);
        }
    }
    banner = ui_label(board, "", 8, 80, 296);
    lv_obj_set_style_text_align(banner, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(banner, lv_color_hex(0x1c3541), 0);
    lv_obj_set_style_bg_opa(banner, 240, 0);
    lv_obj_set_style_pad_ver(banner, 10, 0);
    lv_obj_set_style_radius(banner, 8, 0);
    reset();
}
void games_destroy(void) {
    kind = -1;
    board = NULL;
}
void games_tick(void) {
    if (kind < 0)
        return;
    uint32_t now = lv_tick_get();
    float dt = (now - last_tick) / 1000.f;
    last_tick = now;
    if (kind == 1)
        return;
    if (kind == 2) {
        if (!finished) {
            char s[32];
            snprintf(s, sizeof(s), paused ? "已暂停" : "%d / 25 步", steps);
            lv_label_set_text(hud, s);
        }
        return;
    }
    state_lock();
    bool valid = state->imu_valid;
    float r = remainderf(state->roll - zero_r, 360.f), p = state->pitch - zero_p;
    int sensitivity = config.sensitivity;
    state_unlock();
    if (!valid) {
        lv_label_set_text(hud, "姿态传感器异常");
        return;
    }
    if (!paused)
        shooter_tick(&flight, r, p, dt, sensitivity);
    for (int i = 0; i < 24; i++)
        lv_obj_set_y(stars[i], (int)(i * 47 + flight.elapsed * (i % 3 + 1) * 12) % ARENA_HEIGHT);
    lv_obj_set_pos(player, flight.x - 16, flight.y - 17);
    visible(player, flight.invulnerable <= 0 || (now / 100) % 2);
    for (int i = 0; i < ENEMIES; i++) {
        visible(enemy[i], flight.enemies[i].active);
        lv_obj_set_pos(enemy[i], flight.enemies[i].x - 16, flight.enemies[i].y - 14);
    }
    for (int i = 0; i < SHOTS; i++) {
        actor_t *a = &flight.shots[i];
        visible(shot[i], a->active);
        lv_obj_set_pos(shot[i], a->x - 2, a->y - 5);
        lv_obj_set_height(shot[i], a->type == 1 ? 18 : 9);
        lv_obj_set_style_bg_color(shot[i],
                                  lv_color_hex(a->type == 1   ? 0xe1baff
                                               : a->type == 2 ? 0xffdf8c
                                                              : 0x8deaff),
                                  0);
    }
    for (int i = 0; i < HOSTILE; i++) {
        visible(hostile[i], flight.hostile[i].active);
        lv_obj_set_pos(hostile[i], flight.hostile[i].x - 2, flight.hostile[i].y - 3);
    }
    for (int i = 0; i < DROPS; i++) {
        actor_t *a = &flight.drops[i];
        visible(drop[i], a->active);
        lv_obj_set_pos(drop[i], a->x - 11, a->y - 11);
        lv_label_set_text(lv_obj_get_child(drop[i], 0),
                          (const char *[]){"S", "L", "W", "+"}[a->type]);
    }
    for (int i = 0; i < EFFECTS; i++) {
        actor_t *a = &flight.effects[i];
        visible(effects[i], a->active);
        int radius = 6 + (int)((.35f - a->vy) * 50);
        lv_obj_set_size(effects[i], radius * 2, radius * 2);
        lv_obj_set_pos(effects[i], a->x - radius, a->y - radius);
    }
    char s[80];
    snprintf(s, sizeof(s), paused ? "已暂停" : "%d %s%d %dHP", flight.score,
             (const char *[]){"散射", "激光", "双翼"}[flight.weapon], flight.power, flight.lives);
    lv_label_set_text(hud, s);
    visible(banner, flight.over);
    if (flight.over) {
        snprintf(s, sizeof(s), "任务结束 / 得分 %d", flight.score);
        lv_label_set_text(banner, s);
    }
}
