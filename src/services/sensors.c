#include "core/terminal.h"
#include "core/tilt.h"
#include "esp32_s3_szp.h"
#include "esp_log.h"
#include "esp_timer.h"
extern esp_err_t qmi8658_register_read(uint8_t, uint8_t *, size_t);
extern esp_err_t qmi8658_register_write_byte(uint8_t, uint8_t);
static void task(void *p) {
    tilt_filter_t filter = {0};
    TickType_t wake = xTaskGetTickCount();
    int64_t last = esp_timer_get_time(), report = last, max_gap = 0;
    unsigned reads = 0, failures = 0;
    for (;;) {
        uint8_t raw[6];
        esp_err_t e = qmi8658_register_read(QMI8658_AX_L, raw, sizeof(raw));
        int64_t now = esp_timer_get_time(), gap = now - last;
        last = now;
        if (gap > max_gap)
            max_gap = gap;
        reads++;
        if (e != ESP_OK)
            failures++;
        if (e == ESP_OK) {
            float a[3];
            for (int i = 0; i < 3; i++)
                a[i] = (int16_t)(raw[2 * i] | raw[2 * i + 1] << 8) / 8192.f;
            float r = atan2f(a[1], a[2]) * 180 / M_PI;
            float pitch = atan2f(-a[0], sqrtf(a[1] * a[1] + a[2] * a[2])) * 180 / M_PI;
            tilt_filter_update(&filter, r, pitch, gap / 1000000.f);
        } else
            filter.ready = false;
        state_lock();
        state->imu_valid = e == ESP_OK;
        if (e == ESP_OK) {
            state->roll = filter.roll;
            state->pitch = filter.pitch;
        }
        state_unlock();
        if (now - report >= 10000000) {
            ESP_LOGI("posture", "read_rate=%.1fHz max_gap=%lldus failures=%u",
                     reads * 1000000.f / (now - report), max_gap, failures);
            report = now;
            reads = failures = 0;
            max_gap = 0;
        }
        if (xTaskDelayUntil(&wake, pdMS_TO_TICKS(10)) == pdFALSE)
            wake = xTaskGetTickCount();
    }
}
void sensors_init(void) {
    uint8_t id = 0;
    if (qmi8658_register_read(0, &id, 1) != ESP_OK || id != 5)
        return;
    if (qmi8658_register_write_byte(QMI8658_RESET, 0xb0) != ESP_OK)
        return;
    vTaskDelay(pdMS_TO_TICKS(20));
    // Accelerometer remains at 250 Hz / 4g; read its latest sample at 100 Hz.
    if (qmi8658_register_write_byte(QMI8658_CTRL1, 0x40) != ESP_OK ||
        qmi8658_register_write_byte(QMI8658_CTRL2, 0x15) != ESP_OK ||
        qmi8658_register_write_byte(QMI8658_CTRL3, 0x55) != ESP_OK ||
        qmi8658_register_write_byte(QMI8658_CTRL7, 3) != ESP_OK)
        return;
    // Keep short IMU reads ahead of LVGL rendering; audio capture remains higher priority.
    xTaskCreatePinnedToCore(task, "posture", 3072, NULL, 5, NULL, 1);
}
