#include "usb_transfer_view.h"
#include "shell.h"
#include "ui_style.h"
#include "services/usb_transfer.h"
#include <stdio.h>

static lv_obj_t *title, *description, *activity, *detail, *action, *action_text, *dot;
static lv_obj_t *confirmation;
static lv_obj_t *shape(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
static void line(lv_obj_t *parent, const lv_point_t *points, int count, uint32_t color, int width) {
    lv_obj_t *o = lv_line_create(parent);
    lv_line_set_points(o, points, count);
    lv_obj_set_style_line_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_line_width(o, width, 0);
    lv_obj_set_style_line_rounded(o, true, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
}
static void icon(lv_obj_t *parent, int x, int y) {
    lv_obj_t *root = shape(parent, x, y, 70, 60, 0, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    shape(root, 25, 1, 22, 22, 0xd9edf4, 4);
    shape(root, 30, 4, 4, 10, 0x40637a, 1);
    shape(root, 38, 4, 4, 10, 0x40637a, 1);
    lv_obj_t *body = shape(root, 17, 18, 38, 40, 0x76e4d4, 10);
    lv_obj_set_style_bg_grad_color(body, lv_color_hex(0x2f9fb2), 0);
    lv_obj_set_style_bg_grad_dir(body, LV_GRAD_DIR_VER, 0);
    shape(root, 21, 23, 3, 23, 0xa4f5e5, 2);
    static const lv_point_t stem[] = {{36,49},{36,29},{32,33},{36,29},{40,33}};
    static const lv_point_t branch[] = {{36,43},{29,38},{29,34}};
    static const lv_point_t branch2[] = {{36,39},{43,35},{43,29}};
    line(root, stem, 5, 0xefffff, 2);
    line(root, branch, 3, 0xefffff, 2);
    line(root, branch2, 3, 0xefffff, 2);
    shape(root, 26, 30, 6, 6, 0xefffff, LV_RADIUS_CIRCLE);
    shape(root, 40, 26, 6, 6, 0xefffff, 1);
    static const lv_point_t left[] = {{11,30},{4,37},{11,44}};
    static const lv_point_t right[] = {{60,30},{67,37},{60,44}};
    line(root, left, 3, 0x8baec8, 2);
    line(root, right, 3, 0x8baec8, 2);
}
void usb_transfer_menu_icon(lv_obj_t *parent) { icon(parent, 39, 3); }
static void confirmation_deleted(lv_event_t *event) {
    (void)event;
    confirmation = NULL;
}
static void confirm_choice(lv_event_t *event) {
    bool enable = lv_event_get_user_data(event) != NULL;
    if (confirmation) lv_obj_del(confirmation);
    if (enable) usb_transfer_request(true);
}
static void confirm_open(void) {
    if (confirmation) return;
    // Screen-owned modal is also destroyed if navigation occurs externally.
    confirmation = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(confirmation);
    lv_obj_set_size(confirmation, 320, 240);
    lv_obj_set_style_bg_color(confirmation, lv_color_hex(0x050c15), 0);
    lv_obj_set_style_bg_opa(confirmation, LV_OPA_80, 0);
    lv_obj_clear_flag(confirmation, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(confirmation, confirmation_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_t *panel = shape(confirmation, 8, 9, 304, 222, UI_SURFACE, 18);
    // Labels share the project's CJK font through the current page.
    lv_obj_set_style_text_font(panel, lv_obj_get_style_text_font(shell_content(), 0), 0);
    lv_obj_set_style_text_color(panel, lv_color_hex(0xe6edf7), 0);
    ui_label(panel, "开启 USB 传输？", 16, 13, 272);
    lv_obj_t *body = ui_label(panel,
        "音乐和语音将停止，SD 卡交给电脑。\n"
        "本机暂停播放、看图及文件管理。\n"
        "60 秒未连接电脑，自动返回。\n"
        "结束时请在电脑上安全弹出。", 16, 43, 272);
    lv_obj_set_style_text_color(body, lv_color_hex(0xb4c7da), 0);
    ui_button(panel, "取消", 14, 180, 132, confirm_choice, NULL);
    lv_obj_t *yes = ui_button(panel, "确认开启", 156, 180, 132, confirm_choice, (void *)1);
    ui_style_primary(yes);
}
static void clicked(lv_event_t *event) {
    (void)event;
    if (usb_transfer_busy()) usb_transfer_request(false);
    else confirm_open();
}
void usb_transfer_view_create(lv_obj_t *parent) {
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *card = shape(parent, 0, 0, 294, 108, 0x193747, 17);
    ui_style_panel(card);
    icon(card, 12, 22);
    title = ui_label(card, "连接电脑", 96, 12, 186);
    lv_obj_set_style_text_color(title, lv_color_hex(0xf0f7fc), 0);
    description = ui_label(card, "通过数据线拷贝\n音乐、图片与文件", 96, 37, 186);
    lv_obj_set_style_text_color(description, lv_color_hex(0xadc5d9), 0);
    lv_obj_set_height(description, 38);
    dot = shape(card, 97, 87, 7, 7, 0x6b8da3, LV_RADIUS_CIRCLE);
    activity = ui_label(card, "普通模式", 112, 80, 171);
    lv_obj_set_style_text_color(activity, lv_color_hex(0x88ded7), 0);
    detail = ui_label(parent, "60 秒内未连接将自动退出", 2, 116, 290);
    lv_obj_set_style_text_color(detail, lv_color_hex(0x91a9bf), 0);
    lv_label_set_long_mode(detail, LV_LABEL_LONG_DOT);
    action = ui_button(parent, "开启 USB 传输", 0, 146, 294, clicked, NULL);
    lv_obj_set_height(action, 36);
    lv_obj_set_style_radius(action, 11, 0);
    ui_style_primary(action);
    action_text = lv_obj_get_child(action, 0);
}
void usb_transfer_view_update(const terminal_state_t *s) {
    const char *heading = "连接电脑", *body = "通过数据线拷贝\n音乐、图片与文件";
    const char *status = "普通模式", *button = "开启 USB 传输";
    char info[128], badge[64];
    snprintf(info, sizeof(info), "60 秒内未连接将自动退出");
    bool disabled = false;
    uint32_t color = 0x6b8da3;
    switch (s->usb_phase) {
    case USB_TRANSFER_PREPARING:
        heading = "正在准备"; body = "正在停止播放\n并释放 SD 卡";
        status = "请稍候"; button = "正在准备..."; disabled = true;
        break;
    case USB_TRANSFER_WAITING:
        heading = "等待电脑连接"; body = "请使用支持传输的\nUSB 数据线连接电脑";
        snprintf(badge, sizeof(badge), "剩余 %u 秒", s->usb_wait_seconds);
        status = badge; button = "取消连接"; color = 0xefa956;
        break;
    case USB_TRANSFER_CONNECTED:
        heading = "电脑已连接";
        body = "SD 卡由电脑使用\n拔线前请安全弹出";
        status = s->usb_io_error ? "读写失败，请安全弹出" :
                 s->usb_suspended ? "电脑已暂停 USB" : s->usb_writing ? "正在写入，请勿拔线" :
                 s->usb_reading ? "正在读取" : "当前空闲";
        button = "结束 USB 传输"; color = s->usb_writing ? 0xefa956 : 0x70e3c2;
        if (s->usb_io_error) color = 0xf19986;
        snprintf(info, sizeof(info), "读取 %.1f MB  /  写入 %.1f MB",
                 s->usb_read_bytes / 1048576.0, s->usb_written_bytes / 1048576.0);
        break;
    case USB_TRANSFER_FINISHING:
        heading = "正在恢复"; body = "释放 USB 连接\n恢复 SD 卡本机访问";
        status = "请稍候"; button = "正在恢复..."; disabled = true;
        break;
    case USB_TRANSFER_ERROR:
        heading = "暂时无法连接"; body = "请检查 SD 卡\n然后重试";
        status = "连接未开启"; color = 0xf19986;
        snprintf(info, sizeof(info), "%s", s->usb_status);
        break;
    default:
        if (s->usb_status[0]) snprintf(info, sizeof(info), "%s", s->usb_status);
        break;
    }
    lv_label_set_text(title, heading);
    lv_label_set_text(description, body);
    lv_label_set_text(activity, status);
    lv_label_set_text(detail, info);
    lv_label_set_text(action_text, button);
    lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
    if (disabled) lv_obj_add_state(action, LV_STATE_DISABLED);
    else lv_obj_clear_state(action, LV_STATE_DISABLED);
}
