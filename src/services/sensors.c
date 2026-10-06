#include "core/terminal.h"
#include "esp32_s3_szp.h"
extern esp_err_t qmi8658_register_read(uint8_t, uint8_t *, size_t);
extern esp_err_t qmi8658_register_write_byte(uint8_t, uint8_t);
static void task(void *p) {
    for (;;) {
        uint8_t raw[6];
        esp_err_t e = qmi8658_register_read(QMI8658_AX_L, raw, sizeof(raw));
        state_lock();
        state->imu_valid = e == ESP_OK;
        if (e == ESP_OK) {
            float a[3];
            for (int i = 0; i < 3; i++)
                a[i] = (int16_t)(raw[2 * i] | raw[2 * i + 1] << 8) / 8192.f;
            float r = atan2f(a[1], a[2]) * 180 / M_PI,
                  t = atan2f(-a[0], sqrtf(a[1] * a[1] + a[2] * a[2])) * 180 / M_PI;
            state->roll += .25f * (r - state->roll);
            state->pitch += .25f * (t - state->pitch);
        }
        state_unlock();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
void sensors_init(void) {
    uint8_t id = 0;
    if (qmi8658_register_read(0, &id, 1) != ESP_OK || id != 5)
        return;
    if (qmi8658_register_write_byte(QMI8658_RESET, 0xb0) != ESP_OK) {
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    if (qmi8658_register_write_byte(QMI8658_CTRL1, 0x40) != ESP_OK ||
        qmi8658_register_write_byte(QMI8658_CTRL2, 0x15) != ESP_OK ||
        qmi8658_register_write_byte(QMI8658_CTRL3, 0x55) != ESP_OK ||
        qmi8658_register_write_byte(QMI8658_CTRL7, 3) != ESP_OK)
        return;
    xTaskCreate(task, "posture", 3072, NULL, 3, NULL);
}
