#include "cJSON.h"
#include "core/resampler.h"
#include "core/terminal.h"
#include "esp32_s3_szp.h"
#include "esp_aec.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_websocket_client.h"
#include "freertos/event_groups.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "mbedtls/base64.h"
#include "services/tools.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern esp_err_t demo_audio_raw_capture(void *, size_t, size_t *);
extern esp_err_t demo_audio_output(void *, size_t, size_t *);
#define MESSAGE_MAX (64 * 1024)
#define CAP_IDLE BIT0
#define PLAY_IDLE BIT1
static esp_websocket_client_handle_t ws;
static SemaphoreHandle_t send_mutex, capture_mutex, playback_mutex, parse_mutex;
static EventGroupHandle_t idle;
static QueueHandle_t messages, playback;
static atomic_bool running, session_ready, desired, initialized;
static atomic_uint lifecycle_request;
static unsigned lifecycle_applied;
static atomic_uint epoch;
static char *fragment;
static size_t received, total;
static aec_handle_t *aec;
static resampler_t up, down;
static char current_response[96], cancelled_response[96];
typedef struct {
    unsigned epoch;
    size_t n;
    int16_t pcm[256];
} audio_packet_t;
static void status(const char *s) {
    state_lock();
    snprintf(state->voice_status, sizeof(state->voice_status), "%s", s);
    state_unlock();
}
static void transcript(const char *s, bool newline) {
    state_lock();
    size_t n = strlen(state->transcript), k = strlen(s);
    if (n + k + 3 >= sizeof(state->transcript)) {
        state->transcript[0] = 0;
        n = 0;
    }
    snprintf(state->transcript + n, sizeof(state->transcript) - n, "%s%s", newline ? "\n" : "", s);
    state_unlock();
}
static bool send_json(cJSON *j) {
    char *s = cJSON_PrintUnformatted(j);
    if (!s)
        return false;
    xSemaphoreTake(send_mutex, portMAX_DELAY);
    int n =
        ws && running ? esp_websocket_client_send_text(ws, s, strlen(s), pdMS_TO_TICKS(1500)) : -1;
    xSemaphoreGive(send_mutex);
    free(s);
    return n > 0;
}
static void simple(const char *type) {
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "type", type);
    send_json(j);
    cJSON_Delete(j);
}
static void clear_playback(void) {
    atomic_fetch_add(&epoch, 1);
    audio_packet_t p;
    while (xQueueReceive(playback, &p, 0) == pdTRUE) {
    }
    resampler_init(&down, 24000, 16000);
}
static void configure(void) {
    terminal_config_t c;
    config_snapshot(&c);
    cJSON *j = cJSON_CreateObject(), *s = cJSON_AddObjectToObject(j, "session");
    cJSON_AddStringToObject(j, "type", "session.update");
    cJSON *m = cJSON_AddArrayToObject(s, "modalities");
    cJSON_AddItemToArray(m, cJSON_CreateString("text"));
    cJSON_AddItemToArray(m, cJSON_CreateString("audio"));
    cJSON_AddStringToObject(
        s, "instructions",
        "你是桌面中文语音助手。回答简洁自然。天气必须调用 get_weather，时效信息必须调用 "
        "web_search，失败时明确说明，不编造查询结果。");
    cJSON_AddStringToObject(s, "voice", c.voice);
    cJSON_AddStringToObject(s, "input_audio_format", "pcm16");
    cJSON_AddStringToObject(s, "output_audio_format", "pcm16");
    cJSON *vad = cJSON_AddObjectToObject(s, "turn_detection");
    cJSON_AddStringToObject(vad, "type", "server_vad");
    cJSON_AddNumberToObject(vad, "prefix_padding_ms", 500);
    cJSON_AddNumberToObject(vad, "silence_duration_ms", 500);
    cJSON *tools = cJSON_AddArrayToObject(s, "tools");
    size_t tool_count = 0;
    const terminal_tool_t *registered = terminal_tools(&tool_count);
    for (size_t i = 0; i < tool_count; i++) {
        const terminal_tool_t *tool = &registered[i];
        cJSON *t = cJSON_CreateObject();
        cJSON_AddItemToArray(tools, t);
        cJSON_AddStringToObject(t, "type", "function");
        cJSON *f = cJSON_AddObjectToObject(t, "function");
        cJSON_AddStringToObject(f, "name", tool->name);
        cJSON_AddStringToObject(f, "description", tool->description);
        cJSON *p = cJSON_AddObjectToObject(f, "parameters");
        cJSON_AddStringToObject(p, "type", "object");
        cJSON *props = cJSON_AddObjectToObject(p, "properties"),
              *a = cJSON_AddObjectToObject(props, tool->parameter);
        cJSON_AddStringToObject(a, "type", "string");
        if (tool->required) {
            cJSON *req = cJSON_AddArrayToObject(p, "required");
            cJSON_AddItemToArray(req, cJSON_CreateString(tool->parameter));
        }
    }
    send_json(j);
    cJSON_Delete(j);
}
static const char *str(cJSON *j, const char *key) {
    cJSON *s = cJSON_GetObjectItem(j, key);
    return cJSON_IsString(s) ? s->valuestring : "";
}
typedef struct {
    char name[64], arguments[1024], call_id[96];
    unsigned generation;
} tool_call_t;
static QueueHandle_t tools_queue;
static void tool_worker(void *arg) {
    tool_call_t t;
    for (;;) {
        xQueueReceive(tools_queue, &t, portMAX_DELAY);
        if (!running || t.generation != epoch)
            continue;
        char *result = terminal_tool_execute(t.name, t.arguments);
        if (running && t.generation == epoch) {
            cJSON *j = cJSON_CreateObject();
            cJSON_AddStringToObject(j, "type", "conversation.item.create");
            cJSON *item = cJSON_AddObjectToObject(j, "item");
            cJSON_AddStringToObject(item, "type", "function_call_output");
            cJSON_AddStringToObject(item, "call_id", t.call_id);
            cJSON_AddStringToObject(item, "output", result);
            send_json(j);
            cJSON_Delete(j);
            simple("response.create");
        }
        free(result);
    }
}
static void parse(char *message) {
    cJSON *j = cJSON_Parse(message);
    if (!j)
        return;
    const char *type = str(j, "type");
    if (!strcmp(type, "session.created"))
        configure();
    else if (!strcmp(type, "session.updated")) {
        session_ready = true;
        status("正在聆听 · 可以随时说话");
    } else if (!strcmp(type, "input_audio_buffer.speech_started")) {
        if (current_response[0]) {
            snprintf(cancelled_response, sizeof(cancelled_response), "%s", current_response);
            simple("response.cancel");
        }
        clear_playback();
        status("正在聆听");
    } else if (!strcmp(type, "response.created")) {
        snprintf(current_response, sizeof(current_response), "%s",
                 str(cJSON_GetObjectItem(j, "response"), "id"));
        status("正在回答");
    } else if (!strcmp(type, "response.audio.delta")) {
        const char *rid = str(j, "response_id");
        if (*rid && !strcmp(rid, cancelled_response))
            goto done;
        const char *s = str(j, "delta");
        size_t cap = strlen(s) * 3 / 4 + 4, n = 0;
        uint8_t *raw = malloc(cap);
        if (!raw)
            goto done;
        if (mbedtls_base64_decode(raw, cap, &n, (const unsigned char *)s, strlen(s)) == 0 &&
            n % 2 == 0) {
            int16_t out[256];
            for (size_t i = 0; i < n / 2;) {
                size_t take = n / 2 - i;
                if (take > 300)
                    take = 300;
                size_t count = resampler_process(&down, (int16_t *)raw + i, take, out, 256);
                audio_packet_t p = {.epoch = epoch, .n = count};
                memcpy(p.pcm, out, count * 2);
                if (xQueueSend(playback, &p, pdMS_TO_TICKS(100)) != pdTRUE) {
                    status("音频缓冲已满，本轮已停止");
                    simple("response.cancel");
                    clear_playback();
                    break;
                }
                i += take;
            }
        }
        free(raw);
    } else if (!strcmp(type, "response.audio_transcript.delta"))
        transcript(str(j, "delta"), false);
    else if (!strcmp(type, "conversation.item.input_audio_transcription.completed")) {
        transcript("你：", true);
        transcript(str(j, "transcript"), false);
        transcript("助手：", true);
    } else if (!strcmp(type, "response.output_item.done")) {
        cJSON *item = cJSON_GetObjectItem(j, "item");
        if (!strcmp(str(item, "type"), "function_call")) {
            tool_call_t t = {.generation = epoch};
            snprintf(t.name, sizeof(t.name), "%s", str(item, "name"));
            snprintf(t.call_id, sizeof(t.call_id), "%s", str(item, "call_id"));
            const char *a = str(item, "arguments");
            if (strlen(a) < sizeof(t.arguments)) {
                strcpy(t.arguments, a);
                if (xQueueSend(tools_queue, &t, 0) != pdTRUE)
                    status("工具任务繁忙");
            }
        }
    } else if (!strcmp(type, "response.done")) {
        current_response[0] = 0;
        status("正在聆听");
    } else if (!strcmp(type, "error")) {
        status("语音服务返回错误，请检查配置");
        const char *code = str(cJSON_GetObjectItem(j, "error"), "code");
        if (*code)
            terminal_notice("Realtime 错误：%.80s", code);
    }
done:
    cJSON_Delete(j);
}
static void message_worker(void *arg) {
    char *s;
    for (;;) {
        xQueueReceive(messages, &s, portMAX_DELAY);
        xSemaphoreTake(parse_mutex, portMAX_DELAY);
        if (running)
            parse(s);
        xSemaphoreGive(parse_mutex);
        free(s);
    }
}
static void websocket_event(void *arg, esp_event_base_t base, int32_t event, void *data) {
    esp_websocket_event_data_t *e = data;
    if (event == WEBSOCKET_EVENT_CONNECTED) {
        status("连接成功，正在配置会话");
    } else if (event == WEBSOCKET_EVENT_DISCONNECTED || event == WEBSOCKET_EVENT_ERROR) {
        session_ready = false;
        status("会话已断开，请退出后重新进入");
    } else if (event == WEBSOCKET_EVENT_DATA && (e->op_code == 1 || e->op_code == 0)) {
        if (e->payload_offset == 0) {
            free(fragment);
            fragment = NULL;
            received = 0;
            total = e->payload_len;
            if (total > MESSAGE_MAX)
                return;
            fragment = malloc(total + 1);
        }
        if (!fragment || e->payload_offset != received || received + e->data_len > total)
            return;
        memcpy(fragment + received, e->data_ptr, e->data_len);
        received += e->data_len;
        if (received == total) {
            fragment[total] = 0;
            if (xQueueSend(messages, &fragment, 0) != pdTRUE) {
                free(fragment);
                session_ready = false;
                status("消息队列拥塞，请重新连接");
            }
            fragment = NULL;
        }
    }
}
static void capture_task(void *arg) {
    for (;;) {
        if (!running || !session_ready) {
            xEventGroupSetBits(idle, CAP_IDLE);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        xSemaphoreTake(capture_mutex, portMAX_DELAY);
        if (!running || !session_ready) {
            xSemaphoreGive(capture_mutex);
            continue;
        }
        xEventGroupClearBits(idle, CAP_IDLE);
        int n = aec_get_chunksize(aec);
        int16_t *mem = heap_caps_aligned_alloc(16, n * 2 * 8, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!mem) {
            session_ready = false;
            status("回声处理内存不足");
            xSemaphoreGive(capture_mutex);
            continue;
        }
        int16_t *raw = mem, *mic = raw + 4 * n, *ref = mic + n, *clean = ref + n,
                *out = clean + n; /* output separately: 24k needs 1.5 frames */
        int16_t *resampled = malloc((n * 2 + 8) * 2);
        char *base64 = malloc(n * 6 + 64);
        if (!resampled || !base64) {
            free(mem);
            free(resampled);
            free(base64);
            session_ready = false;
            xSemaphoreGive(capture_mutex);
            continue;
        }
        (void)out;
        while (running && session_ready) {
            size_t got = 0;
            esp_err_t err = demo_audio_raw_capture(raw, n * 8, &got);
            if (err != ESP_OK || got != n * 8) {
                status("麦克风采集失败");
                session_ready = false;
                break;
            }
            for (int i = 0; i < n; i++) {
                mic[i] = raw[i * 4 + 1];
                ref[i] = raw[i * 4];
            }
            aec_process(aec, mic, ref, clean);
            size_t count = resampler_process(&up, clean, n, resampled, n * 2 + 8), encoded = 0;
            mbedtls_base64_encode((unsigned char *)base64, n * 6 + 64, &encoded,
                                  (unsigned char *)resampled, count * 2);
            base64[encoded] = 0;
            cJSON *j = cJSON_CreateObject();
            cJSON_AddStringToObject(j, "type", "input_audio_buffer.append");
            cJSON_AddStringToObject(j, "audio", base64);
            if (!send_json(j))
                session_ready = false;
            cJSON_Delete(j);
        }
        free(mem);
        free(resampled);
        free(base64);
        xEventGroupSetBits(idle, CAP_IDLE);
        xSemaphoreGive(capture_mutex);
    }
}
static void playback_task(void *arg) {
    audio_packet_t p;
    int16_t stereo[512];
    for (;;) {
        xEventGroupSetBits(idle, PLAY_IDLE);
        if (xQueueReceive(playback, &p, pdMS_TO_TICKS(50)) != pdTRUE)
            continue;
        xSemaphoreTake(playback_mutex, portMAX_DELAY);
        if (!running || p.epoch != epoch) {
            xSemaphoreGive(playback_mutex);
            continue;
        }
        xEventGroupClearBits(idle, PLAY_IDLE);
        for (size_t i = 0; i < p.n; i++)
            stereo[i * 2] = stereo[i * 2 + 1] = p.pcm[i];
        size_t done = 0;
        pa_en(1);
        demo_audio_output(stereo, p.n * 4, &done);
        xSemaphoreGive(playback_mutex);
    }
}
void voice_init(void) {
    send_mutex = xSemaphoreCreateMutex();
    capture_mutex = xSemaphoreCreateMutex();
    playback_mutex = xSemaphoreCreateMutex();
    parse_mutex = xSemaphoreCreateMutex();
    idle = xEventGroupCreate();
    messages = xQueueCreate(8, sizeof(char *));
    playback =
        xQueueCreateWithCaps(64, sizeof(audio_packet_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    tools_queue = xQueueCreateWithCaps(3, sizeof(tool_call_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(send_mutex && capture_mutex && playback_mutex && parse_mutex && idle && messages &&
           playback && tools_queue);
    xEventGroupSetBits(idle, CAP_IDLE | PLAY_IDLE);
    xTaskCreate(message_worker, "realtime_events", 8192, NULL, 5, NULL);
    xTaskCreate(tool_worker, "voice_tools", 12288, NULL, 3, NULL);
    xTaskCreate(capture_task, "voice_capture", 8192, NULL, 6, NULL);
    xTaskCreate(playback_task, "voice_playback", 4096, NULL, 6, NULL);
    initialized = true;
}
void voice_stop(void) {
    running = false;
    session_ready = false;
    atomic_fetch_add(&epoch, 1);
    xSemaphoreTake(capture_mutex, portMAX_DELAY);
    xSemaphoreTake(playback_mutex, portMAX_DELAY);
    xSemaphoreTake(parse_mutex, portMAX_DELAY);
    xSemaphoreTake(send_mutex, portMAX_DELAY);
    if (ws) {
        esp_websocket_client_stop(ws);
        esp_websocket_client_destroy(ws);
        ws = NULL;
    }
    xSemaphoreGive(send_mutex);
    free(fragment);
    fragment = NULL;
    if (aec) {
        aec_destroy(aec);
        aec = NULL;
    }
    audio_packet_t p;
    while (xQueueReceive(playback, &p, 0) == pdTRUE) {
    }
    pa_en(0);
    state_lock();
    state->voice_active = false;
    state_unlock();
    status("会话已结束");
    char *message;
    while (xQueueReceive(messages, &message, 0) == pdTRUE)
        free(message);
    xSemaphoreGive(parse_mutex);
    xSemaphoreGive(playback_mutex);
    xSemaphoreGive(capture_mutex);
}
void voice_start(void) {
    if (!desired)
        return;
    if (running)
        return;
    terminal_config_t c;
    config_snapshot(&c);
    if (!state->online) {
        status("请先连接 Wi-Fi");
        return;
    }
    if (!c.api_key[0]) {
        status("请在设置中开启网页，填写 API Key");
        return;
    }
    if (media_suspend() != ESP_OK) {
        status("无法暂停音乐");
        return;
    }
    if (bsp_codec_set_fs(16000, 32, I2S_SLOT_MODE_STEREO) != ESP_OK) {
        status("音频配置失败");
        return;
    }
    aec = aec_create(16000, 4, 1, AEC_MODE_VOIP_HIGH_PERF);
    if (!aec) {
        status("回声消除初始化失败");
        return;
    }
    resampler_init(&up, 16000, 24000);
    resampler_init(&down, 24000, 16000);
    current_response[0] = cancelled_response[0] = 0;
    char uri[320], headers[320];
    snprintf(uri, sizeof(uri), "%s?model=%s", c.endpoint, c.model);
    snprintf(headers, sizeof(headers), "Authorization: Bearer %s\r\n", c.api_key);
    esp_websocket_client_config_t w = {.uri = uri,
                                       .headers = headers,
                                       .crt_bundle_attach = esp_crt_bundle_attach,
                                       .disable_auto_reconnect = true,
                                       .buffer_size = 4096,
                                       .task_stack = 6144,
                                       .network_timeout_ms = 10000};
    ws = esp_websocket_client_init(&w);
    if (!ws) {
        aec_destroy(aec);
        aec = NULL;
        status("连接初始化失败");
        return;
    }
    esp_websocket_register_events(ws, WEBSOCKET_EVENT_ANY, websocket_event, NULL);
    running = true;
    state_lock();
    state->voice_active = true;
    state->transcript[0] = 0;
    state_unlock();
    status("正在连接 StepAudio…");
    if (esp_websocket_client_start(ws) != ESP_OK)
        voice_stop();
}

void voice_request(bool enable) {
    desired = enable;
    if (!enable) {
        running = false;
        session_ready = false;
        atomic_fetch_add(&epoch, 1);
    }
    atomic_fetch_add(&lifecycle_request, 1);
}

// The main service worker consumes a separate lifecycle mailbox. Shutdown
// requests cannot be lost when the ordinary jobs queue is full.
void voice_poll(void) {
    if (!initialized)
        return;
    unsigned requested = lifecycle_request;
    if (requested == lifecycle_applied)
        return;
    lifecycle_applied = requested;
    if (ws)
        voice_stop();
    if (desired)
        voice_start();
}
