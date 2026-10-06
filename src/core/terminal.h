#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#define FILE_LIMIT 96
#define PATH_SIZE 512
#define BLE_LIMIT 24

typedef struct {
    uint32_t version;
    int brightness, volume, sensitivity;
    char ssid[33], password[65], city[80];
    double latitude, longitude;
    bool location_set;
    char api_key[256], model[80], endpoint[160], voice[48], search_key[160];
    int best_dodge;
} terminal_config_t;
typedef struct {
    char name[256];
    bool directory;
    uint64_t size;
} file_entry_t;
typedef struct {
    uint8_t address[6], address_type;
    int rssi;
    char name[64];
} ble_entry_t;
typedef struct {
    bool online, time_valid, weather_valid, mounted, imu_valid, audio_ready, web_enabled;
    char web_code[16];
    uint64_t sd_total, sd_free;
    char ip[20], notice[192], weather_error[128], weather_city[80], weather_time[40];
    float temperature, feels, low, high;
    int weather_code;
    time_t weather_updated;
    float roll, pitch;
    char directory[PATH_SIZE];
    file_entry_t files[FILE_LIMIT];
    int file_count;
    unsigned files_generation;
    char wifi_names[16][33];
    int wifi_count;
    unsigned wifi_generation;
    ble_entry_t devices[BLE_LIMIT];
    int ble_count;
    unsigned ble_generation;
    char ble_status[160];
    char music_path[PATH_SIZE], music_status[128];
    char voice_status[160], transcript[32768];
    bool voice_active;
    unsigned revision;
} terminal_state_t;
extern terminal_config_t config;
extern terminal_state_t *state;
extern SemaphoreHandle_t state_mutex;
void state_lock(void);
void state_unlock(void);
void terminal_notice(const char *fmt, ...);
void config_snapshot(terminal_config_t *out);
esp_err_t terminal_config_save(const terminal_config_t *value);
void terminal_init(void);

typedef enum {
    JOB_WIFI_SCAN,
    JOB_WIFI_CONNECT,
    JOB_WEATHER,
    JOB_CITY,
    JOB_MOUNT,
    JOB_EJECT,
    JOB_LIST,
    JOB_MKDIR,
    JOB_RENAME,
    JOB_DELETE,
    JOB_WEB,
    JOB_BLE_SCAN,
    JOB_BLE_PAIR,
    JOB_MUSIC_PLAY,
    JOB_MUSIC_TOGGLE,
    JOB_MUSIC_NEXT,
    JOB_VOICE_START,
    JOB_VOICE_STOP,
    JOB_LEVELS,
    JOB_SCORE
} job_kind_t;
typedef struct {
    job_kind_t kind;
    char a[PATH_SIZE], b[256];
    int value;
} terminal_job_t;
bool terminal_submit(job_kind_t kind, const char *a, const char *b, int value);
void network_init(void);
void network_job(const terminal_job_t *job);
void storage_job(const terminal_job_t *job);
bool storage_path_valid(const char *path);
bool storage_name_valid(const char *name);
void media_init(void);
void media_job(const terminal_job_t *job);
esp_err_t media_suspend(void);
void media_resume_clock(void);
bool media_busy_path(const char *path);
void ble_init(void);
void ble_job(const terminal_job_t *job);
void voice_start(void);
void voice_stop(void);
void voice_init(void);
void voice_request(bool enable);
void voice_poll(void);
void voice_diagnostics(void);
void sensors_init(void);
char *weather_tool(const char *city);
char *search_tool(const char *query);
