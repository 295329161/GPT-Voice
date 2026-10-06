#include "apps/shell.h"
#include "core/terminal.h"
#include "driver/uart.h"
#include "esp32_s3_szp.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <stdlib.h>
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
            state_unlock();
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
        } else if (!strcmp(line, "stop")) {
            terminal_submit(JOB_MUSIC_TOGGLE, NULL, NULL, -1);
        } else if (!strncmp(line, "page ", 5)) {
            int p = atoi(line + 5);
            if (p >= 0 && p <= 18) {
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
