#pragma once
#include "lvgl.h"
void shell_init(void);
void shell_open(int app);
lv_obj_t *ui_label(lv_obj_t *parent, const char *text, int x, int y, int width);
lv_obj_t *ui_button(lv_obj_t *parent, const char *text, int x, int y, int width, lv_event_cb_t cb,
                    void *data);
void games_create(lv_obj_t *parent, lv_obj_t *toolbar, int kind);
void games_destroy(void);
void games_tick(void);

/* Register before shell_init, or from the LVGL thread before opening the menu.
 * IDs >= 100 are reserved for independently added applications. */
typedef struct {
    int id;
    const char *name, *icon;
    uint32_t color;
    void (*create)(void);
    void (*enter)(void);
    void (*leave)(void);
    void (*destroy)(void);
} terminal_app_t;
bool shell_register_app(const terminal_app_t *app);
lv_obj_t *shell_content(void);
