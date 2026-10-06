#include "apps/shell.h"
#include "core/terminal.h"
#include "services/usb_transfer.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp32_s3_szp.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <stdlib.h>
static lv_indev_t *test_pointer;
static lv_indev_drv_t pointer_driver;
static lv_indev_data_t pointer_data;
static void pointer_read(lv_indev_drv_t *driver, lv_indev_data_t *data) {
    (void)driver;
    data->point = pointer_data.point;
    data->state = pointer_data.state;
}
static void pointer_set(int x, int y, bool down) {
    lvgl_port_lock(0);
    if (!test_pointer) {
        lv_indev_drv_init(&pointer_driver);
        pointer_driver.type = LV_INDEV_TYPE_POINTER;
        pointer_driver.read_cb = pointer_read;
        test_pointer = lv_indev_drv_register(&pointer_driver);
    }
    pointer_data.point.x = x;
    pointer_data.point.y = y;
    pointer_data.state = down ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    lvgl_port_unlock();
}
static void task(void *arg) {
    uart_config_t cfg = {.baud_rate = 115200,
                         .data_bits = UART_DATA_8_BITS,
                         .parity = UART_PARITY_DISABLE,
                         .stop_bits = UART_STOP_BITS_1,
                         .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                         .source_clk = UART_SCLK_DEFAULT};
    uart_param_config(UART_NUM_0, &cfg);
    uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0);
    char line[256];
    size_t n = 0;
    for (;;) {
        uint8_t c;
        if (uart_read_bytes(UART_NUM_0, &c, 1, pdMS_TO_TICKS(100)) != 1)
            continue;
        if (c == '\r')
            continue;
        if (c != '\n') {
            if (n < sizeof(line) - 1)
                line[n++] = c;
            continue;
        }
        line[n] = 0;
        n = 0;
        if (!strcmp(line, "status")) {
            state_lock();
            printf("STATUS online=%d time=%d sd=%d imu=%d audio=%d voice=%d heap=%u internal=%u "
                   "tasks=%u\n",
                   state->online, state->time_valid, state->mounted, state->imu_valid,
                   state->audio_ready, state->voice_active, (unsigned)esp_get_free_heap_size(),
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                   (unsigned)uxTaskGetNumberOfTasks());
            printf("MUSIC playing=%d paused=%d elapsed_ms=%lld status=%s\n",
                   state->music_playing, state->music_paused,
                   (long long)state->music_elapsed_ms, state->music_status);
            printf("BACKLIGHT on=%d brightness=%d duty=%u boot_level=%d\n", state->backlight_on,
                   config.brightness, (unsigned)ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0),
                   gpio_get_level(GPIO_NUM_0));
            state_unlock();
            voice_diagnostics();
            usb_transfer_diagnostics();

        } else if (!strcmp(line, "usb-on")) {
            usb_transfer_request(true);
        } else if (!strcmp(line, "usb-off")) {
            usb_transfer_request(false);
        } else if (!strcmp(line, "usb-test-nohost")) {
            usb_transfer_test_nohost();
        } else if (!strncmp(line, "fixtures ", 9)) {
            terminal_submit(JOB_FIXTURES, NULL, NULL, atoi(line + 9));
        } else if (!strcmp(line, "ui")) {
            lvgl_port_lock(0);
            shell_diagnostics();
            lvgl_port_unlock();
        } else if (!strncmp(line, "text ", 5)) {
            lvgl_port_lock(0);
            shell_input_text(line + 5);
            lvgl_port_unlock();
        } else if (!strncmp(line, "tap ", 4)) {
            int x, y, duration = 100;
            if (sscanf(line + 4, "%d %d %d", &x, &y, &duration) >= 2 &&
                x >= 0 && x < 320 && y >= 0 && y < 240 && duration >= 60 && duration <= 2000) {
                pointer_set(x, y, true);
                vTaskDelay(pdMS_TO_TICKS(duration));
                pointer_set(x, y, false);
            }
        } else if (!strncmp(line, "swipe ", 6)) {
            int x, y, xx, yy;
            if (sscanf(line + 6, "%d %d %d %d", &x, &y, &xx, &yy) == 4 &&
                x >= 0 && x < 320 && xx >= 0 && xx < 320 &&
                y >= 0 && y < 240 && yy >= 0 && yy < 240) {
                for (int i=0; i<=20; i++) {
                    pointer_set(x+(xx-x)*i/20, y+(yy-y)*i/20, true);
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
                pointer_set(xx, yy, false);
            }
        } else if (!strncmp(line, "voice-test ", 11)) {
            terminal_submit(JOB_VOICE_TEST, line + 11, NULL, 0);
        } else if (!strncmp(line, "voice-sources ", 14)) {
            lvgl_port_lock(0);
            shell_voice_sources(atoi(line + 14) != 0);
            lvgl_port_unlock();
        } else if (!strncmp(line, "wifi ", 5)) {
            char *separator = strchr(line + 5, '\t');
            if (separator) {
                *separator = 0;
                terminal_submit(JOB_WIFI_CONNECT, line + 5, separator + 1, 0);
                memset(line, 0, sizeof(line));
                printf("WIFI_CONFIG_QUEUED\n");
            } else
                printf("WIFI_CONFIG_INVALID\n");
        } else if (!strncmp(line, "city ", 5)) {
            terminal_submit(JOB_CITY, line + 5, NULL, 0);
            printf("CITY_QUEUED\n");
        } else if (!strcmp(line, "web")) {
            terminal_submit(JOB_WEB, NULL, NULL, 1);
        } else if (!strcmp(line, "ls")) {
            state_lock();
            for (int i = 0; i < state->file_count; i++)
                printf("FILE %s %llu %s\n", state->files[i].directory ? "DIR" : "FILE",
                       (unsigned long long)state->files[i].size, state->files[i].name);
            state_unlock();
        } else if (!strncmp(line, "music ", 6)) {
            terminal_submit(JOB_MUSIC_PLAY, line + 6, NULL, 0);
        } else if (!strcmp(line, "music-toggle")) {
            terminal_submit(JOB_MUSIC_TOGGLE, NULL, NULL, 0);
        } else if (!strcmp(line, "backlight-toggle")) {
            backlight_toggle();
        } else if (!strcmp(line, "audio-probe")) {
            terminal_submit(JOB_AUDIO_PROBE, NULL, NULL, 0);
        } else if (!strncmp(line, "music-next ", 11)) {
            terminal_submit(JOB_MUSIC_NEXT, NULL, NULL, atoi(line + 11));
        } else if (!strcmp(line, "stop")) {
            terminal_submit(JOB_MUSIC_TOGGLE, NULL, NULL, -1);
        } else if (!strncmp(line, "page ", 5)) {
            int p = atoi(line + 5);
            if ((p >= 0 && p <= 18) || p == 21 || p == 22) {
                lvgl_port_lock(0);
                shell_open(p);
                lvgl_port_unlock();
            }
        } else if (!strcmp(line, "shot")) {
            lvgl_port_lock(0);
            lv_img_dsc_t *img = lv_snapshot_take(lv_scr_act(), LV_IMG_CF_TRUE_COLOR);
            if (img) {
                printf("SHOT %u %u\n", img->header.w, img->header.h);
                const uint16_t *px = (const uint16_t *)img->data;
                size_t count = img->header.w * img->header.h;
                unsigned runs = 0;
                for (size_t i = 0; i < count;) {
                    size_t run = 1;
                    while (i + run < count && px[i + run] == px[i] && run < 65535)
                        run++;
                    printf("%04x%04x", (unsigned)run, px[i]);
                    i += run;
                    if (++runs % 64 == 0)
                        vTaskDelay(1);
                }
                printf("\nSHOT_END\n");
                lv_snapshot_free(img);
            } else
                printf("SHOT_FAILED\n");
            lvgl_port_unlock();
        } else
            printf("COMMANDS status/page N/shot\n");
    }
}
void console_init(void) {
    xTaskCreate(task, "console", 4096, NULL, 2, NULL);
}
