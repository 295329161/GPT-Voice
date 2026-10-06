#include "apps/shell.h"
#include "core/terminal.h"
#include "esp32_s3_szp.h"
extern esp_err_t pca9557_register_write_byte(uint8_t, uint8_t);
void console_init(void);
void app_main(void) {
    terminal_init();
    ESP_ERROR_CHECK(bsp_i2c_init());
    ESP_ERROR_CHECK(pca9557_register_write_byte(PCA9557_OUTPUT_PORT, 0x05));
    ESP_ERROR_CHECK(pca9557_register_write_byte(PCA9557_CONFIGURATION_PORT, 0xf8));
    bsp_lvgl_start();
    bsp_display_brightness_set(config.brightness);
    lvgl_port_lock(0);
    shell_init();
    lvgl_port_unlock();
    media_init();
    voice_init();
    sensors_init();
    network_init();
    ble_init();
    terminal_submit(JOB_MOUNT, NULL, NULL, 0);
    console_init();
    ESP_LOGI("terminal", "READY desktop v0.1");
}
