#include "cJSON.h"
#include "core/conversation.h"
#include "core/resampler.h"
#include "core/terminal.h"
#include "esp32_s3_szp.h"
#include "esp_aec.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
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
#include <time.h>
extern esp_err_t demo_audio_raw_capture(void *, size_t, size_t *);
extern esp_err_t demo_audio_output(void *, size_t, size_t *);
#define MESSAGE_MAX (64 * 1024)
#define CAP_IDLE BIT0
#define PLAY_IDLE BIT1
static esp_websocket_client_handle_t ws;
static SemaphoreHandle_t send_mutex, capture_mutex, playback_mutex, parse_mutex, upload_mutex;
static EventGroupHandle_t idle;
static QueueHandle_t messages, playback, upload;
static atomic_uint connection_generation;
typedef struct {
    unsigned generation;
    size_t n;
    int16_t pcm[768];
} upload_packet_t;
static atomic_bool running, session_ready, desired, initialized, playback_blocked;
typedef struct {
    cJSON *event;
    unsigned generation;
} message_packet_t;
static atomic_uint lifecycle_request;
static unsigned lifecycle_applied;
static atomic_uint epoch;
static char *fragment;
static size_t received, total;
static aec_handle_t *aec;
static wifi_ps_type_t previous_wifi_ps;
static bool restore_wifi_ps;
static resampler_t up, down;
static char current_response[96], cancelled_response[96];
static conversation_t *conversation;
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
static void transcript_refresh(void) {
    state_lock();
    conversation_render(conversation, state->transcript, sizeof(state->transcript));
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
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    char date[64] = "尚未校时", instructions[768];
    if (now > 1700000000) strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S（北京时间）", &local);
    snprintf(instructions, sizeof(instructions),
             "你是桌面中文语音助手。默认用中文回答，除非用户明确要求其他语言。"
             "回答简洁自然。会话开始时设备时间：%s。"
             "询问当前时间、日期或星期必须调用 get_time，以工具结果为准，不能猜测。"
             "你已获准使用服务器内置 web_search 联网搜索。天气必须调用 get_weather，"
             "新闻、最新动态等需联网查询的信息必须调用 web_search，回答注明来源，"
             "失败时明确说明，不编造查询结果。", date);
    cJSON_AddStringToObject(s, "instructions", instructions);
    cJSON_AddStringToObject(s, "voice", c.voice);
    cJSON_AddStringToObject(s, "input_audio_format", "pcm16");
    cJSON_AddStringToObject(s, "output_audio_format", "pcm16");
    cJSON *vad = cJSON_AddObjectToObject(s, "turn_detection");
    cJSON_AddStringToObject(vad, "type", "server_vad");
    cJSON_AddNumberToObject(vad, "prefix_padding_ms", 500);
    cJSON_AddNumberToObject(vad, "silence_duration_ms", 500);
    // The board's post-AEC near-field speech is quieter than the service's
    // default energy threshold (2500). Keep above measured idle noise.
    cJSON_AddNumberToObject(vad, "energy_awakeness_threshold", 300);
    cJSON *tools = cJSON_AddArrayToObject(s, "tools");
    // StepFun executes this built-in tool server-side; no separate search key.
    cJSON *web_search = cJSON_CreateObject();
    cJSON_AddStringToObject(web_search, "type", "web_search");
    cJSON *search_function = cJSON_AddObjectToObject(web_search, "function");
    cJSON_AddStringToObject(search_function, "description", "用户询问新闻、最新动态或要求联网查询时，搜索互联网并返回来源");
    cJSON *search_options = cJSON_AddObjectToObject(search_function, "options");
    cJSON_AddNumberToObject(search_options, "top_k", 5);
    cJSON_AddNumberToObject(search_options, "timeout_seconds", 5);
    cJSON_AddItemToArray(tools, web_search);
    size_t tool_count = 0;
    const terminal_tool_t *registered = terminal_tools(&tool_count);
    for (size_t i = 0; i < tool_count; i++) {
        const terminal_tool_t *tool = &registered[i];
        if (!strcmp(tool->name, "external_web_search") && !c.search_key[0]) continue;
        cJSON *t = cJSON_CreateObject();
        cJSON_AddItemToArray(tools, t);
        cJSON_AddStringToObject(t, "type", "function");
        cJSON *f = cJSON_AddObjectToObject(t, "function");
        cJSON_AddStringToObject(f, "name", tool->name);
        cJSON_AddStringToObject(f, "description", tool->description);
        cJSON *p = cJSON_AddObjectToObject(f, "parameters");
        cJSON_AddStringToObject(p, "type", "object");
        cJSON *props = cJSON_AddObjectToObject(p, "properties");
        if (tool->parameter) {
            cJSON *a = cJSON_AddObjectToObject(props, tool->parameter);
            cJSON_AddStringToObject(a, "type", "string");
        }
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
static char submitted_calls[16][96];
static unsigned submitted_call_index;
static void submit_tool(cJSON *item) {
    const char *call_id = str(item, "call_id"), *name = str(item, "name"), *args = str(item, "arguments");
    if (!*call_id || !*name || strlen(call_id) >= sizeof(submitted_calls[0])) return;
    for (unsigned i = 0; i < 16; i++)
        if (!strcmp(call_id, submitted_calls[i])) return;
    tool_call_t t = {.generation = epoch};
    if (strlen(args) >= sizeof(t.arguments) || strlen(name) >= sizeof(t.name)) return;
    strcpy(t.call_id, call_id);
    strcpy(t.name, name);
    strcpy(t.arguments, args);
    if (xQueueSend(tools_queue, &t, 0) == pdTRUE)
        strcpy(submitted_calls[submitted_call_index++ % 16], call_id);
    else status("工具任务繁忙");
}
static void tool_worker(void *arg) {
    tool_call_t t;
    for (;;) {
        xQueueReceive(tools_queue, &t, portMAX_DELAY);
        if (!running || t.generation != epoch)
            continue;
        ESP_LOGI("voice", "tool %.64s", t.name);
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
static void parse(cJSON *j, unsigned generation) {
    if (!j)
        return;
    const char *type = str(j, "type");
    if (strcmp(type, "response.audio.delta") && strcmp(type, "response.audio_transcript.delta"))
        ESP_LOGI("voice", "event %.80s", type);
    if (!strcmp(type, "session.created"))
        configure();
    else if (!strcmp(type, "session.updated")) {
        cJSON *session = cJSON_GetObjectItem(j, "session");
        cJSON *vad = cJSON_GetObjectItem(session, "turn_detection");
        cJSON *threshold = cJSON_GetObjectItem(vad, "energy_awakeness_threshold");
        ESP_LOGI("voice", "session model=%s input=%s vad=%s threshold=%g",
                 str(session, "model"), str(session, "input_audio_format"), str(vad, "type"),
                 cJSON_IsNumber(threshold) ? threshold->valuedouble : -1);
        cJSON *reported_tools = cJSON_GetObjectItem(session, "tools"), *reported_tool;
        ESP_LOGI("voice", "server-reported tool count=%d", cJSON_GetArraySize(reported_tools));
        cJSON_ArrayForEach(reported_tool, reported_tools)
            ESP_LOGI("voice", "server tool type=%s name=%s", str(reported_tool, "type"),
                     str(cJSON_GetObjectItem(reported_tool, "function"), "name"));
        session_ready = true;
        status("正在聆听 · 可以随时说话");
    } else if (!strcmp(type, "input_audio_buffer.speech_started")) {
        if (current_response[0]) {
            snprintf(cancelled_response, sizeof(cancelled_response), "%s", current_response);
            simple("response.cancel");
        }
        // The WebSocket callback already muted and invalidated old audio.
        resampler_init(&down, 24000, 16000);
        status("正在聆听");
    } else if (!strcmp(type, "input_audio_buffer.committed")) {
        conversation_user_begin(conversation, str(j, "item_id"));
        transcript_refresh();
    } else if (!strcmp(type, "response.created")) {
        snprintf(current_response, sizeof(current_response), "%s",
                 str(cJSON_GetObjectItem(j, "response"), "id"));
        if (generation == epoch) playback_blocked = false;
        conversation_response_begin(conversation, current_response);
        transcript_refresh();
        status("正在回答");
    } else if (!strcmp(type, "response.audio.delta")) {
        if (playback_blocked || generation != epoch) goto done;
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
                if (playback_blocked || generation != epoch) break;
                size_t take = n / 2 - i;
                if (take > 300)
                    take = 300;
                size_t count = resampler_process(&down, (int16_t *)raw + i, take, out, 256);
                audio_packet_t p = {.epoch = generation, .n = count};
                memcpy(p.pcm, out, count * 2);
                if (xQueueSend(playback, &p, pdMS_TO_TICKS(100)) != pdTRUE) {
                    playback_blocked = true;
                    status("音频缓冲已满，本轮已停止");
                    simple("response.cancel");
                    clear_playback();
                    break;
                }
                i += take;
            }
        }
        free(raw);
    } else if (!strcmp(type, "response.audio_transcript.delta")) {
        const char *rid = str(j, "response_id");
        if (*rid && !strcmp(rid, cancelled_response)) goto done;
        conversation_response_text(conversation, *rid ? rid : current_response, str(j, "delta"));
        transcript_refresh();
    } else if (!strcmp(type, "conversation.item.input_audio_transcription.completed")) {
        conversation_user_text(conversation, str(j, "item_id"), str(j, "transcript"));
        transcript_refresh();
    } else if (!strcmp(type, "response.function_call_arguments.done")) {
        if (!playback_blocked && generation == epoch) submit_tool(j);
    } else if (!strcmp(type, "response.output_item.done")) {
        cJSON *item = cJSON_GetObjectItem(j, "item");
        if (!playback_blocked && generation == epoch && !strcmp(str(item, "type"), "function_call")) submit_tool(item);
    } else if (!strcmp(type, "response.done")) {
        const char *rid = str(cJSON_GetObjectItem(j, "response"), "id");
        if (*rid && strcmp(rid, current_response)) goto done;
        current_response[0] = 0;
        status("正在聆听");
    } else if (!strcmp(type, "error")) {
        cJSON *error = cJSON_GetObjectItem(j, "error");
        const char *code = str(error, "code");
        const char *detail = str(error, "message");
        if (!*detail) detail = str(j, "message");
        // Server VAD can finish/cancel before our interruption reaches it.
        if (!strcmp(detail, "no ongoing response to cancel")) goto done;
        status("语音服务返回错误，请检查配置");
        terminal_config_t safe_config;
        config_snapshot(&safe_config);
        if (!safe_config.api_key[0] || !strstr(detail, safe_config.api_key))
            ESP_LOGW("voice", "service error code=%.80s message=%.200s", code, detail);
        if (*code)
            terminal_notice("Realtime 错误：%.80s", code);
    }
done:
    return;
}
static void message_worker(void *arg) {
    message_packet_t message;
    for (;;) {
        xQueueReceive(messages, &message, portMAX_DELAY);
        xSemaphoreTake(parse_mutex, portMAX_DELAY);
        if (running)
            parse(message.event, message.generation);
        xSemaphoreGive(parse_mutex);
        cJSON_Delete(message.event);
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
            cJSON *j = cJSON_Parse(fragment);
            free(fragment);
            fragment = NULL;
            if (!j) return;
            if (!strcmp(str(j, "type"), "input_audio_buffer.speech_started")) {
                // Do not let a barge-in wait behind buffered audio events.
                playback_blocked = true;
                unsigned queued = uxQueueMessagesWaiting(playback);
                atomic_fetch_add(&epoch, 1);
                xQueueReset(playback);
                pa_en(0);
                ESP_LOGI("voice", "barge-in: muted speaker, discarded %u audio blocks", queued);
            }
            message_packet_t message = {.event = j, .generation = epoch};
            if (xQueueSend(messages, &message, 0) != pdTRUE) {
                cJSON_Delete(j);
                session_ready = false;
                status("消息队列拥塞，请重新连接");
            }
        }
    }
}
static void upload_task(void *arg) {
    upload_packet_t packet;
    int16_t *batch = heap_caps_malloc(4 * sizeof(packet.pcm), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char *base64 = heap_caps_malloc(8196, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(batch && base64);
    for (;;) {
        if (xQueueReceive(upload, &packet, portMAX_DELAY) != pdTRUE) continue;
        xSemaphoreTake(upload_mutex, portMAX_DELAY);
        if (!running || !session_ready || packet.generation != connection_generation) {
            xSemaphoreGive(upload_mutex);
            continue;
        }
        unsigned generation = packet.generation;
        size_t count = 0;
        for (unsigned i = 0; i < 4; i++) {
            if (packet.generation != generation || packet.n > 768) break;
            memcpy(batch + count, packet.pcm, packet.n * sizeof(int16_t));
            count += packet.n;
            if (i == 3 || xQueueReceive(upload, &packet, pdMS_TO_TICKS(20)) != pdTRUE) break;
        }
        size_t encoded = 0;
        int result = mbedtls_base64_encode((unsigned char *)base64, 8196, &encoded,
                                           (unsigned char *)batch, count * sizeof(int16_t));
        if (result == 0 && running && session_ready && generation == connection_generation) {
            base64[encoded] = 0;
            cJSON *j = cJSON_CreateObject();
            cJSON_AddStringToObject(j, "type", "input_audio_buffer.append");
            cJSON_AddStringToObject(j, "audio", base64);
            if (!send_json(j)) {
                session_ready = false;
                status("音频上传失败，请重新连接");
                ESP_LOGE("voice", "Audio upload failed");
            }
            cJSON_Delete(j);
        }
        xSemaphoreGive(upload_mutex);
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
        int16_t *mem = heap_caps_aligned_alloc(16, n * 2 * 7, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!mem || n > 512) {
            free(mem);
            session_ready = false;
            status("回声处理内存不足");
            xSemaphoreGive(capture_mutex);
            continue;
        }
        int16_t *raw = mem, *mic = raw + 4 * n, *ref = mic + n, *clean = ref + n;
        upload_packet_t packet = {.generation = connection_generation};
        unsigned frames = 0, dropped_frames = 0;
        upload_packet_t obsolete;
        int64_t read_us = 0, dsp_us = 0;
        int peaks[4] = {0}, clean_peak = 0, uploaded_peak = 0;
        while (running && session_ready) {
            int64_t frame_begin = esp_timer_get_time();
            size_t got = 0;
            esp_err_t err = demo_audio_raw_capture(raw, n * 8, &got);
            int64_t read_end = esp_timer_get_time();
            read_us += read_end - frame_begin;
            if (err != ESP_OK || got != n * 8) {
                status("麦克风采集失败");
                session_ready = false;
                break;
            }
            for (int i = 0; i < n; i++) {
                for (int ch = 0; ch < 4; ch++) {
                    int amplitude = abs(raw[i * 4 + ch]);
                    if (amplitude > peaks[ch]) peaks[ch] = amplitude;
                }
                mic[i] = raw[i * 4 + 1];
                ref[i] = raw[i * 4];
            }
            aec_process(aec, mic, ref, clean);
            for (int i = 0; i < n; i++) {
                int amplified = (int)clean[i] * 4;
                clean[i] = amplified > 32767 ? 32767 : amplified < -32768 ? -32768 : amplified;
                if (abs(clean[i]) > clean_peak) clean_peak = abs(clean[i]);
            }
            packet.n = resampler_process(&up, clean, n, packet.pcm, 768);
            for (size_t i = 0; i < packet.n; i++)
                if (abs(packet.pcm[i]) > uploaded_peak) uploaded_peak = abs(packet.pcm[i]);
            if (xQueueSend(upload, &packet, 0) != pdTRUE) {
                // A transient network stall must not permanently stop listening.
                // Keep the newest speech instead of accumulating stale latency.
                if (xQueueReceive(upload, &obsolete, 0) == pdTRUE) dropped_frames++;
                if (xQueueSend(upload, &packet, 0) != pdTRUE) dropped_frames++;
            }
            dsp_us += esp_timer_get_time() - read_end;
            if (++frames % 160 == 0) {
                ESP_LOGI("voice", "capture frames=%u peaks=%d,%d,%d,%d aec=%d upload=%d samples=%u/%u queued=%u",
                         frames, peaks[0], peaks[1], peaks[2], peaks[3], clean_peak,
                         uploaded_peak, (unsigned)n, (unsigned)packet.n,
                         (unsigned)uxQueueMessagesWaiting(upload));
                ESP_LOGI("voice", "frame average us read=%lld dsp=%lld dropped=%u", read_us / 160, dsp_us / 160, dropped_frames);
                read_us = dsp_us = 0;
                memset(peaks, 0, sizeof(peaks));
                clean_peak = uploaded_peak = 0;
            }
        }
        free(mem);
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
        if (!running || playback_blocked || p.epoch != epoch) {
            xSemaphoreGive(playback_mutex);
            continue;
        }
        xEventGroupClearBits(idle, PLAY_IDLE);
        for (size_t i = 0; i < p.n; i++)
            stereo[i * 2] = stereo[i * 2 + 1] = p.pcm[i];
        size_t done = 0;
        pa_en(1);
        demo_audio_output(stereo, p.n * 4, &done);
        if (playback_blocked || p.epoch != epoch) pa_en(0);
        xSemaphoreGive(playback_mutex);
    }
}
void voice_init(void) {
    conversation = heap_caps_calloc(1, sizeof(*conversation), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(conversation);
    send_mutex = xSemaphoreCreateMutex();
    capture_mutex = xSemaphoreCreateMutex();
    playback_mutex = xSemaphoreCreateMutex();
    parse_mutex = xSemaphoreCreateMutex();
    upload_mutex = xSemaphoreCreateMutex();
    idle = xEventGroupCreate();
    messages = xQueueCreateWithCaps(64, sizeof(message_packet_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    upload = xQueueCreateWithCaps(32, sizeof(upload_packet_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    playback =
        xQueueCreateWithCaps(1024, sizeof(audio_packet_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    tools_queue = xQueueCreateWithCaps(3, sizeof(tool_call_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(send_mutex && capture_mutex && playback_mutex && parse_mutex && upload_mutex && upload && idle && messages &&
           playback && tools_queue);
    xEventGroupSetBits(idle, CAP_IDLE | PLAY_IDLE);
    // Reserve internal RAM for DMA, AEC and WebSocket/FreeRTOS control objects.
    // These persistent workers can use external stacks (enabled by the BSP).
    BaseType_t created = xTaskCreateWithCaps(message_worker, "realtime_events", 8192, NULL, 5,
                                            NULL, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(created == pdPASS);
    created = xTaskCreateWithCaps(tool_worker, "voice_tools", 12288, NULL, 3, NULL,
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(created == pdPASS);
    created = xTaskCreatePinnedToCoreWithCaps(capture_task, "voice_capture", 8192, NULL, 6, NULL,
                                             1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(created == pdPASS);
    created = xTaskCreateWithCaps(upload_task, "voice_upload", 8192, NULL, 5, NULL,
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(created == pdPASS);
    created = xTaskCreate(playback_task, "voice_playback", 4096, NULL, 6, NULL);
    assert(created == pdPASS);
    initialized = true;
}
void voice_stop(void) {
    running = false;
    atomic_fetch_add(&connection_generation, 1);
    session_ready = false;
    atomic_fetch_add(&epoch, 1);
    xSemaphoreTake(capture_mutex, portMAX_DELAY);
    xSemaphoreTake(upload_mutex, portMAX_DELAY);
    xSemaphoreTake(playback_mutex, portMAX_DELAY);
    xSemaphoreTake(parse_mutex, portMAX_DELAY);
    xSemaphoreTake(send_mutex, portMAX_DELAY);
    if (ws) {
        esp_websocket_client_stop(ws);
        esp_websocket_client_destroy(ws);
        ws = NULL;
    }
    if (restore_wifi_ps) {
        esp_wifi_set_ps(previous_wifi_ps);
        restore_wifi_ps = false;
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
    xQueueReset(upload);
    pa_en(0);
    state_lock();
    state->voice_active = false;
    state_unlock();
    status("会话已结束");
    message_packet_t message;
    while (xQueueReceive(messages, &message, 0) == pdTRUE)
        cJSON_Delete(message.event);
    xSemaphoreGive(parse_mutex);
    xSemaphoreGive(playback_mutex);
    xSemaphoreGive(upload_mutex);
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
    aec = aec_create(16000, 4, 1, AEC_MODE_VOIP_LOW_COST);
    if (!aec) {
        status("回声消除初始化失败");
        return;
    }
    resampler_init(&up, 16000, 24000);
    resampler_init(&down, 24000, 16000);
    current_response[0] = cancelled_response[0] = 0;
    memset(submitted_calls, 0, sizeof(submitted_calls));
    submitted_call_index = 0;
    conversation_reset(conversation);
    playback_blocked = false;
    char uri[320], headers[320];
    snprintf(uri, sizeof(uri), "%s?model=%s", c.endpoint, c.model);
    snprintf(headers, sizeof(headers), "Authorization: Bearer %s\r\n", c.api_key);
    esp_websocket_client_config_t w = {.uri = uri,
                                       .headers = headers,
                                       .crt_bundle_attach = esp_crt_bundle_attach,
                                       .disable_auto_reconnect = true,
                                       .buffer_size = 8192,
                                       .task_stack = 6144,
                                       .network_timeout_ms = 10000};
    ws = esp_websocket_client_init(&w);
    if (!ws) {
        ESP_LOGE("voice", "WebSocket init failed: internal free=%u largest=%u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        aec_destroy(aec);
        aec = NULL;
        status("连接初始化失败");
        return;
    }
    esp_websocket_register_events(ws, WEBSOCKET_EVENT_ANY, websocket_event, NULL);
    if (esp_wifi_get_ps(&previous_wifi_ps) == ESP_OK && esp_wifi_set_ps(WIFI_PS_NONE) == ESP_OK)
        restore_wifi_ps = true;
    running = true;
    state_lock();
    state->voice_active = true;
    state->transcript[0] = 0;
    state_unlock();
    status("正在连接 StepAudio…");
    esp_err_t err = esp_websocket_client_start(ws);
    if (err != ESP_OK) {
        ESP_LOGE("voice", "WebSocket start failed: %s", esp_err_to_name(err));
        voice_stop();
        status("连接任务启动失败，请重试");
    }
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
