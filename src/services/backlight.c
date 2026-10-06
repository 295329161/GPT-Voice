#include "core/terminal.h"
#include "core/button.h"
#include "driver/gpio.h"
#include "esp32_s3_szp.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/idf_additions.h"

static SemaphoreHandle_t backlight_mutex;

static void apply(bool toggle) {
    xSemaphoreTake(backlight_mutex, portMAX_DELAY);
    state_lock();
    bool enabled = toggle ? !state->backlight_on : state->backlight_on;
    int brightness = config.brightness;
    state_unlock();
    if (bsp_display_brightness_set(enabled ? brightness : 0) == ESP_OK) {
        state_lock();
        state->backlight_on = enabled;
        state_unlock();
    }
    xSemaphoreGive(backlight_mutex);
}
void backlight_toggle(void) {
    apply(true);
}
void backlight_refresh(void) {
    apply(false);
}
static void boot_key_task(void *arg) {
    (void)arg;
    button_t button;
    button_init(&button, (uint32_t)(esp_timer_get_time() / 1000));
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        if (button_pressed(&button, gpio_get_level(GPIO_NUM_0) == 0,
                           (uint32_t)(esp_timer_get_time() / 1000)))
            backlight_toggle();
        if (xTaskDelayUntil(&wake, pdMS_TO_TICKS(10)) == pdFALSE)
            wake = xTaskGetTickCount();
    }
}
void backlight_init(void) {
    backlight_mutex = xSemaphoreCreateMutex();
    assert(backlight_mutex);
    // Runtime-only state: every boot is lit; the saved brightness is untouched.
    state_lock();
    state->backlight_on = true;
    state_unlock();
    backlight_refresh();
    gpio_config_t key = {.pin_bit_mask = 1ULL << GPIO_NUM_0,
                         .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
                         .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&key));
    // Independent of network jobs and UI rendering, without consuming an
    // internal task stack needed by real-time audio and TLS.
    BaseType_t created = xTaskCreatePinnedToCoreWithCaps(boot_key_task, "boot_backlight", 3072,
                                                        NULL, 3, NULL, 0,
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(created == pdPASS);
}
