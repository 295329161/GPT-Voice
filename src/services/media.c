#include "audio_player.h"
#include "core/terminal.h"
#include "esp32_s3_szp.h"
#include "esp_heap_caps.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
extern esp_err_t demo_audio_output(void *, size_t, size_t *);
static uint32_t music_rate = 16000;
static int channels = 2;
static bool initialized;
static SemaphoreHandle_t started;
static char (*playlist)[PATH_SIZE];
static int total, index_now;
static esp_err_t mute(AUDIO_PLAYER_MUTE_SETTING s) {
    if (!state->voice_active)
        pa_en(s == AUDIO_PLAYER_UNMUTE);
    return ESP_OK;
}
static esp_err_t clock_set(uint32_t rate, uint32_t bits, i2s_slot_mode_t ch) {
    if (state->voice_active)
        return ESP_ERR_INVALID_STATE;
    if (bits != 16)
        return ESP_ERR_NOT_SUPPORTED;
    music_rate = rate;
    channels = ch == I2S_SLOT_MODE_MONO ? 1 : 2;
    return bsp_codec_set_fs(rate, 32, I2S_SLOT_MODE_STEREO);
}
static esp_err_t write_pcm(void *buf, size_t len, size_t *done, uint32_t timeout) {
    *done = 0;
    if (state->voice_active)
        return ESP_ERR_INVALID_STATE;
    int16_t stereo[512];
    const int16_t *p = buf;
    size_t frames = len / (channels * 2);
    while (frames) {
        size_t n = frames > 256 ? 256 : frames;
        for (size_t i = 0; i < n; i++) {
            stereo[i * 2] = p[i * channels];
            stereo[i * 2 + 1] = p[i * channels + channels - 1];
        }
        size_t w = 0;
        esp_err_t e = demo_audio_output(stereo, n * 4, &w);
        *done += (w / 4) * channels * 2;
        if (e != ESP_OK || w != n * 4)
            return e == ESP_OK ? ESP_FAIL : e;
        p += n * channels;
        frames -= n;
    }
    return ESP_OK;
}
static void player_event(audio_player_cb_ctx_t *ctx) {
    if (ctx->audio_event == AUDIO_PLAYER_CALLBACK_EVENT_PLAYING ||
        ctx->audio_event == AUDIO_PLAYER_CALLBACK_EVENT_UNKNOWN_FILE_TYPE)
        xSemaphoreGive(started);
    state_lock();
    const char *s = "已停止";
    switch (ctx->audio_event) {
    case AUDIO_PLAYER_CALLBACK_EVENT_PLAYING:
        s = "正在播放";
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_PAUSE:
        s = "已暂停";
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_UNKNOWN_FILE_TYPE:
        s = "不支持或损坏的音频";
        break;
    default:
        break;
    }
    snprintf(state->music_status, sizeof(state->music_status), "%s", s);
    state_unlock();
}
static bool supported(const char *p) {
    const char *x = strrchr(p, '.');
    return x && (!strcasecmp(x, ".mp3") || !strcasecmp(x, ".wav"));
}
static void scan(const char *dir, int depth) {
    if (depth > 8 || total == 96)
        return;
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *e;
    while (total < 96 && (e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        char path[PATH_SIZE];
        if (snprintf(path, sizeof(path), "%s/%s", dir, e->d_name) >= sizeof(path))
            continue;
        struct stat st;
        if (stat(path, &st))
            continue;
        if (S_ISDIR(st.st_mode))
            scan(path, depth + 1);
        else if (supported(path))
            strcpy(playlist[total++], path);
    }
    closedir(d);
}
static void play(const char *path) {
    if (state->voice_active) {
        terminal_notice("请先结束语音会话");
        return;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        terminal_notice("无法打开音乐文件");
        return;
    }
    xSemaphoreTake(started, 0);
    if (audio_player_play(f) != ESP_OK) {
        fclose(f);
        terminal_notice("播放失败");
        return;
    }
    xSemaphoreTake(started, pdMS_TO_TICKS(1500));
    state_lock();
    snprintf(state->music_path, sizeof(state->music_path), "%s", path);
    state_unlock();
}
void media_init(void) {
    playlist = heap_caps_calloc(96, PATH_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(playlist);
    started = xSemaphoreCreateBinary();
    assert(started);
    state->audio_ready = bsp_codec_init() == ESP_OK;
    if (!state->audio_ready)
        return;
    bsp_codec_volume_set(config.volume, NULL);
    pa_en(0);
    audio_player_config_t c = {
        .mute_fn = mute, .clk_set_fn = clock_set, .write_fn = write_pcm, .priority = 5};
    initialized = audio_player_new(c) == ESP_OK;
    if (initialized)
        audio_player_callback_register(player_event, NULL);
}
bool media_busy_path(const char *p) {
    if (!initialized || audio_player_get_state() == AUDIO_PLAYER_STATE_IDLE)
        return false;
    size_t n = strlen(p);
    return !strncmp(state->music_path, p, n) &&
           (state->music_path[n] == 0 || state->music_path[n] == '/');
}
esp_err_t media_suspend(void) {
    if (!initialized)
        return ESP_ERR_INVALID_STATE;
    if (audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING) {
        audio_player_pause();
        for (int i = 0; i < 100 && audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING; i++)
            vTaskDelay(pdMS_TO_TICKS(10));
        if (audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING)
            return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
void media_resume_clock(void) {
    bsp_codec_set_fs(music_rate, 32, I2S_SLOT_MODE_STEREO);
}
void media_job(const terminal_job_t *j) {
    if (!initialized) {
        terminal_notice("音频不可用");
        return;
    }
    if (j->kind == JOB_MUSIC_TOGGLE && j->value == -1) {
        audio_player_stop();
        for (int i = 0; i < 100 && audio_player_get_state() != AUDIO_PLAYER_STATE_IDLE; i++)
            vTaskDelay(pdMS_TO_TICKS(10));
        return;
    }
    if (state->voice_active) {
        terminal_notice("请先结束语音会话");
        return;
    }
    if (!state->mounted) {
        terminal_notice("请先挂载 SD 卡");
        return;
    }
    if (j->kind == JOB_MUSIC_PLAY && j->a[0]) {
        if (storage_path_valid(j->a) && supported(j->a)) {
            char directory[PATH_SIZE];
            strcpy(directory, j->a);
            char *slash = strrchr(directory, '/');
            if (slash)
                *slash = 0;
            total = 0;
            scan(directory, 0);
            index_now = 0;
            for (int i = 0; i < total; i++)
                if (!strcmp(playlist[i], j->a))
                    index_now = i;
            if (!total) {
                total = 1;
                strcpy(playlist[0], j->a);
            }
            play(j->a);
        }
        return;
    }
    if (j->kind == JOB_MUSIC_TOGGLE) {
        audio_player_state_t s = audio_player_get_state();
        if (s == AUDIO_PLAYER_STATE_PLAYING) {
            audio_player_pause();
            return;
        }
        if (s == AUDIO_PLAYER_STATE_PAUSE) {
            media_resume_clock();
            audio_player_resume();
            return;
        }
    }
    if (!total) {
        scan("/sdcard/Music", 0);
        if (!total)
            scan("/sdcard", 0);
        index_now = 0;
    } else if (j->kind == JOB_MUSIC_NEXT)
        index_now = (index_now + (j->value < 0 ? total - 1 : 1)) % total;
    if (total)
        play(playlist[index_now]);
    else
        terminal_notice("/Music 中没有 MP3 或 WAV");
}
