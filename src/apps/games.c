#include "core/terminal.h"
#include "esp_random.h"
#include "shell.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static int kind = -1, level, score, steps;
static bool paused, finished, star_collected;
static float zero_r, zero_p, x, y, vx, vy, obstacle_y, obstacle_x;
static lv_obj_t *board, *ball, *obstacle, *star, *hud, *cells[144], *walls[8];
static uint8_t flood[144];
static const uint32_t colors[] = {0xF15B64, 0xFAC858, 0x56C9A5, 0x55A9FF, 0xA887FF, 0xF599C3};
static int maze_walls[5][4][4] = {
    {{60, 0, 10, 90}, {130, 65, 10, 115}, {205, 0, 10, 110}, {245, 140, 55, 10}},
    {{50, 45, 130, 10}, {95, 100, 10, 80}, {180, 0, 10, 105}, {230, 65, 10, 115}},
    {{40, 0, 10, 120}, {90, 65, 10, 115}, {150, 0, 10, 120}, {215, 65, 10, 115}},
    {{45, 35, 200, 10}, {0, 90, 180, 10}, {100, 145, 200, 10}, {255, 35, 10, 70}},
    {{50, 0, 10, 140}, {105, 40, 10, 140}, {165, 0, 10, 140}, {230, 40, 10, 140}}};
static lv_obj_t *rect(lv_obj_t *p, int px, int py, int w, int h, uint32_t color) {
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, px, py);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
static void calibrate(void) {
    state_lock();
    zero_r = state->roll;
    zero_p = state->pitch;
    state_unlock();
}
static void reset(void) {
    paused = finished = star_collected = false;
    score = steps = 0;
    x = 18;
    y = 18;
    vx = vy = 0;
    obstacle_y = -20;
    obstacle_x = 30 + esp_random() % 230;
    calibrate();
    if (kind == 2) {
        for (int i = 0; i < 144; i++) {
            flood[i] = esp_random() % 6;
            lv_obj_set_style_bg_color(cells[i], lv_color_hex(colors[flood[i]]), 0);
        }
    }
}
static void pause_cb(lv_event_t *e) {
    paused = !paused;
    calibrate();
}
static void reset_cb(lv_event_t *e) {
    reset();
}
static void level_cb(lv_event_t *e) {
    level = (level + 1) % 5;
    for (int i = 0; i < 4; i++) {
        int *w = maze_walls[level][i];
        lv_obj_set_pos(walls[i], w[0], w[1] * .8f);
        lv_obj_set_size(walls[i], w[2], w[3] * .8f);
    }
    reset();
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
    char s[80];
    snprintf(s, sizeof(s),
             win        ? "成功！%d 步"
             : finished ? "步数已用完 · 重开再试"
                        : "%d / 25 步",
             steps);
    lv_label_set_text(hud, s);
}
void games_create(lv_obj_t *parent, int which) {
    kind = which;
    level = 0;
    hud = ui_label(parent, "", 2, 0, 165);
    ui_button(parent, "暂停", 174, 0, 58, pause_cb, NULL);
    ui_button(parent, "重开", 238, 0, 60, reset_cb, NULL);
    board = lv_obj_create(parent);
    lv_obj_remove_style_all(board);
    lv_obj_set_pos(board, 0, 35);
    lv_obj_set_size(board, 300, 155);
    lv_obj_set_style_bg_color(board, lv_color_hex(0x0b1322), 0);
    lv_obj_set_style_bg_opa(board, 255, 0);
    lv_obj_clear_flag(board, LV_OBJ_FLAG_SCROLLABLE);
    if (kind == 2) {
        lv_obj_set_size(board, 192, 144);
        for (int r = 0; r < 12; r++)
            for (int c = 0; c < 12; c++)
                cells[r * 12 + c] = rect(board, c * 16, r * 12, 15, 11, colors[0]);
        for (int i = 0; i < 6; i++) {
            lv_obj_t *b = ui_button(parent, "", 208 + (i % 2) * 46, 38 + (i / 2) * 48, 40, flood_cb,
                                    (void *)(uintptr_t)i);
            lv_obj_set_style_bg_color(b, lv_color_hex(colors[i]), 0);
        }
    } else {
        ball = rect(board, 12, 12, 12, 12, 0x56e3c0);
        lv_obj_set_style_radius(ball, 6, 0);
        if (kind == 0) {
            for (int i = 0; i < 4; i++) {
                int *w = maze_walls[0][i];
                walls[i] = rect(board, w[0], w[1] * .8, w[2], w[3] * .8, 0x567399);
            }
            star = rect(board, 276, 127, 20, 20, 0xffcf5c);
            ui_button(parent, "关卡", 110, 0, 58, level_cb, NULL);
        } else {
            obstacle = rect(board, 100, 0, 55, 12, 0xf15b64);
            star = rect(board, 100, 60, 12, 12, 0xffcf5c);
        }
    }
    reset();
}
void games_destroy(void) {
    kind = -1;
    board = NULL;
}
static bool collision(float px, float py, int *w) {
    return px + 6 > w[0] && px - 6 < w[0] + w[2] && py + 6 > w[1] * .8f &&
           py - 6 < (w[1] + w[3]) * .8f;
}
void games_tick(void) {
    if (kind < 0)
        return;
    if (kind == 2) {
        if (!finished) {
            char s[50];
            snprintf(s, sizeof(s), paused ? "已暂停" : "%d / 25 步", steps);
            lv_label_set_text(hud, s);
        }
        return;
    }
    state_lock();
    bool valid = state->imu_valid;
    float r = state->roll - zero_r, p = state->pitch - zero_p;
    int sensitivity = config.sensitivity;
    state_unlock();
    if (!valid) {
        paused = true;
        lv_label_set_text(hud, "姿态传感器异常");
        return;
    }
    if (paused) {
        lv_label_set_text(hud, "已暂停");
        return;
    }
    if (finished)
        return;
    if (kind == 0) {
        vx = .78f * vx + r * .012f * sensitivity;
        vy = .78f * vy + p * .012f * sensitivity;
        float nx = fmaxf(6, fminf(294, x + vx)), ny = fmaxf(6, fminf(149, y + vy));
        for (int i = 0; i < 4; i++) {
            int *w = maze_walls[level][i];
            if (collision(nx, y, w)) {
                nx = x;
                vx = 0;
            }
            if (collision(nx, ny, w)) {
                ny = y;
                vy = 0;
            }
        }
        x = nx;
        y = ny;
        lv_obj_set_pos(ball, x - 6, y - 6);
        score++;
        char s[64];
        snprintf(s, sizeof(s), "关%d %.1fs", level + 1, score * .033f);
        lv_label_set_text(hud, s);
        if (x > 273 && y > 125) {
            finished = true;
            lv_label_set_text(hud, "通关！点击关卡继续");
        }
    } else {
        x = fmaxf(8, fminf(292, x + r * .035f * sensitivity));
        y = 137;
        obstacle_y += 1.5f + score * .0015f;
        if (obstacle_y > 160) {
            obstacle_y = -12;
            star_collected = false;
            obstacle_x = esp_random() % 245;
            score += 10;
        }
        lv_obj_set_pos(ball, x - 6, y - 6);
        lv_obj_set_pos(obstacle, obstacle_x, obstacle_y);
        lv_obj_set_pos(star, fmodf(obstacle_x + 130, 280), obstacle_y - 60);
        if (!star_collected && fabsf(x - (fmodf(obstacle_x + 130, 280) + 6)) < 14 &&
            fabsf(y - (obstacle_y - 54)) < 10) {
            score += 5;
            star_collected = true;
            lv_obj_add_flag(star, LV_OBJ_FLAG_HIDDEN);
        }
        if (obstacle_y < 0)
            lv_obj_clear_flag(star, LV_OBJ_FLAG_HIDDEN);
        char s[64];
        snprintf(s, sizeof(s), "得分 %d", score);
        lv_label_set_text(hud, s);
        if (x + 6 > obstacle_x && x - 6 < obstacle_x + 55 && y + 6 > obstacle_y &&
            y - 6 < obstacle_y + 12) {
            finished = true;
            lv_label_set_text(hud, "碰撞！点击重开");
            terminal_config_t c;
            config_snapshot(&c);
            if (score > c.best_dodge) {
                c.best_dodge = score;
                terminal_submit(JOB_SCORE, NULL, NULL, score);
            }
        }
    }
}
