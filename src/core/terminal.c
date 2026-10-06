#include "terminal.h"
#include "services/usb_transfer.h"
#include "esp32_s3_szp.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdarg.h>
#include <stdio.h>
terminal_config_t config;
terminal_state_t *state;
SemaphoreHandle_t state_mutex;
static QueueHandle_t jobs;
static SemaphoreHandle_t config_mutex;
void state_lock(void) {
    xSemaphoreTake(state_mutex, portMAX_DELAY);
}
void state_unlock(void) {
    state->revision++;
    xSemaphoreGive(state_mutex);
}
void terminal_notice(const char *fmt, ...) {
    state_lock();
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(state->notice, sizeof(state->notice), fmt, ap);
    va_end(ap);
    state->notice_generation++;
    state_unlock();
}
void config_snapshot(terminal_config_t *out) {
    state_lock();
    *out = config;
    state_unlock();
}
static esp_err_t config_save(const terminal_config_t *v) {
    if (v->version != 1 || v->brightness < 5 || v->brightness > 100 || v->volume < 0 ||
        v->volume > 90 || v->sensitivity < 1 || v->sensitivity > 5)
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t e = nvs_open("terminal", NVS_READWRITE, &h);
    if (e != ESP_OK)
        return e;
    e = nvs_set_blob(h, "config", v, sizeof(*v));
    if (e == ESP_OK)
        e = nvs_commit(h);
    nvs_close(h);
    if (e == ESP_OK) {
        state_lock();
        config = *v;
        state_unlock();
    }
    return e;
}
// Serialize NVS writes, merging only the caller-owned fields into the latest
// config. Network requests may finish after a newer volume/API change.
esp_err_t terminal_config_update(const terminal_config_t *v, config_field_t field) {
    xSemaphoreTake(config_mutex, portMAX_DELAY);
    terminal_config_t merged;
    config_snapshot(&merged);
    switch (field) {
    case CONFIG_BRIGHTNESS: merged.brightness = v->brightness; break;
    case CONFIG_VOLUME: merged.volume = v->volume; break;
    case CONFIG_SENSITIVITY: merged.sensitivity = v->sensitivity; break;
    case CONFIG_WIFI:
        memcpy(merged.ssid, v->ssid, sizeof(merged.ssid));
        memcpy(merged.password, v->password, sizeof(merged.password));
        break;
    case CONFIG_LOCATION:
        memcpy(merged.city, v->city, sizeof(merged.city));
        merged.latitude = v->latitude; merged.longitude = v->longitude;
        merged.location_set = v->location_set;
        break;
    case CONFIG_VOICE:
        memcpy(merged.api_key, v->api_key, sizeof(merged.api_key));
        memcpy(merged.endpoint, v->endpoint, sizeof(merged.endpoint));
        memcpy(merged.model, v->model, sizeof(merged.model));
        memcpy(merged.voice, v->voice, sizeof(merged.voice));
        memcpy(merged.search_key, v->search_key, sizeof(merged.search_key));
        break;
    case CONFIG_SCORE:
        if (v->best_dodge > merged.best_dodge) merged.best_dodge = v->best_dodge;
        break;
    default:
        xSemaphoreGive(config_mutex);
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t e = memcmp(&merged, &config, sizeof(merged)) ? config_save(&merged) : ESP_OK;
    xSemaphoreGive(config_mutex);
    return e;
}
bool terminal_submit(job_kind_t kind, const char *a, const char *b, int value) {
    terminal_job_t j = {.kind = kind, .value = value};
    if ((a && strlen(a) >= sizeof(j.a)) || (b && strlen(b) >= sizeof(j.b))) {
        terminal_notice("输入过长");
        return false;
    }
    if (a) {
        strcpy(j.a, a);
    }
    if (b) {
        strcpy(j.b, b);
    }
    if (xQueueSend(jobs, &j, 0) != pdTRUE) {
        terminal_notice("操作繁忙，请稍后重试");
        return false;
    }
    return true;
}
static void worker(void *arg) {
    terminal_job_t j;
    for (;;) {
        usb_transfer_poll();
        voice_poll();
        if (xQueueReceive(jobs, &j, pdMS_TO_TICKS(100)) != pdTRUE)
            continue;
        switch (j.kind) {
        case JOB_WIFI_SCAN:
        case JOB_WIFI_CONNECT:
        case JOB_WEATHER:
        case JOB_CITY:
        case JOB_WEB:
            network_job(&j);
            break;
        case JOB_BLE_SCAN:
        case JOB_BLE_PAIR:
            ble_job(&j);
            break;
        case JOB_MUSIC_PLAY:
        case JOB_MUSIC_TOGGLE:
        case JOB_MUSIC_NEXT:
            media_job(&j);
            break;
        case JOB_VOICE_START:
            voice_start();
            break;
        case JOB_VOICE_STOP:
            voice_stop();
            break;
        case JOB_VOICE_TEST:
            voice_test_prompt(j.a);
            break;
        case JOB_FIXTURES:
            diagnostics_fixtures(j.value != 0);
            break;
        case JOB_AUDIO_PROBE:
            media_audio_probe();
            break;
        case JOB_SCORE: {
            terminal_config_t c;
            config_snapshot(&c);
            if (j.value > c.best_dodge) {
                c.best_dodge = j.value;
                terminal_config_update(&c, CONFIG_SCORE);
            }
            break;
        }
        case JOB_LEVELS: {
            terminal_config_t c;
            config_snapshot(&c);
            if (j.value == 0)
                c.brightness = atoi(j.a);
            else if (j.value == 1)
                c.volume = atoi(j.a);
            else
                c.sensitivity = atoi(j.a);
            if (terminal_config_update(&c, j.value == 0 ? CONFIG_BRIGHTNESS : j.value == 1 ? CONFIG_VOLUME : CONFIG_SENSITIVITY) == ESP_OK) {
                backlight_refresh();
                if (state->audio_ready)
                    bsp_codec_volume_set(c.volume, NULL);
            }
            break;
        }
        default:
            storage_job(&j);
            break;
        }
    }
}
void terminal_init(void) {
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(state);
    state_mutex = xSemaphoreCreateMutex();
    config_mutex = xSemaphoreCreateMutex();
    jobs = xQueueCreateWithCaps(8, sizeof(terminal_job_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(state_mutex && config_mutex && jobs);
    ESP_ERROR_CHECK(nvs_flash_init());
    config = (terminal_config_t){.version = 1, .brightness = 80, .volume = 70, .sensitivity = 3};
    strcpy(config.model, "stepaudio-3-realtime-preview");
    strcpy(config.endpoint, "wss://api.stepfun.ai/v1/realtime");
    strcpy(config.voice, "soft-spoken-gentleman");
    nvs_handle_t h;
    if (nvs_open("terminal", NVS_READONLY, &h) == ESP_OK) {
        terminal_config_t c;
        size_t n = sizeof(c);
        if (nvs_get_blob(h, "config", &c, &n) == ESP_OK && n == sizeof(c) && c.version == 1)
            config = c;
        nvs_close(h);
    }
    strcpy(state->directory, "/sdcard");
    strcpy(state->voice_status, "点击进入实时对话");
    xTaskCreate(worker, "terminal_jobs", 12288, NULL, 4, NULL);
}
