#include "cJSON.h"
#include "core/terminal.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static httpd_handle_t web;
static char web_token[9];
static int64_t web_until;
static esp_netif_t *station;
static bool sntp_started;
typedef struct {
    char *data;
    size_t used, capacity;
} response_t;
static esp_err_t http_event(esp_http_client_event_t *e) {
    response_t *r = e->user_data;
    if (e->event_id == HTTP_EVENT_ON_DATA) {
        if (r->used + e->data_len >= r->capacity)
            return ESP_ERR_NO_MEM;
        memcpy(r->data + r->used, e->data, e->data_len);
        r->used += e->data_len;
        r->data[r->used] = 0;
    }
    return ESP_OK;
}
static char *request(const char *url, const char *post, const char *key) {
    response_t r = {.capacity = 32768};
    r.data = calloc(1, r.capacity);
    if (!r.data)
        return NULL;
    esp_http_client_config_t cfg = {.url = url,
                                    .timeout_ms = 12000,
                                    .crt_bundle_attach = esp_crt_bundle_attach,
                                    .event_handler = http_event,
                                    .user_data = &r,
                                    .buffer_size = 2048};
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) {
        free(r.data);
        return NULL;
    }
    if (key) {
        char auth[256];
        snprintf(auth, sizeof(auth), "Bearer %s", key);
        esp_http_client_set_header(h, "Authorization", auth);
    }
    if (post) {
        esp_http_client_set_method(h, HTTP_METHOD_POST);
        esp_http_client_set_header(h, "Content-Type", "application/json");
        esp_http_client_set_post_field(h, post, strlen(post));
    }
    esp_err_t e = esp_http_client_perform(h);
    int status = esp_http_client_get_status_code(h);
    esp_http_client_cleanup(h);
    if (e != ESP_OK || status != 200) {
        free(r.data);
        return NULL;
    }
    return r.data;
}
static void encode(const char *s, char *out, size_t cap) {
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    for (; *s && n + 4 < cap; s++) {
        unsigned char c = *s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_')
            out[n++] = c;
        else {
            out[n++] = '%';
            out[n++] = hex[c >> 4];
            out[n++] = hex[c & 15];
        }
    }
    out[n] = 0;
}
static bool locate(const char *city, double *lat, double *lon, char *name, size_t cap) {
    char q[256], url[512];
    encode(city, q, sizeof(q));
    snprintf(
        url, sizeof(url),
        "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=zh&format=json",
        q);
    char *s = request(url, NULL, NULL);
    if (!s)
        return false;
    cJSON *root = cJSON_Parse(s);
    free(s);
    cJSON *v = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "results"), 0);
    cJSON *a = cJSON_GetObjectItem(v, "latitude"), *b = cJSON_GetObjectItem(v, "longitude"),
          *n = cJSON_GetObjectItem(v, "name");
    bool ok = cJSON_IsNumber(a) && cJSON_IsNumber(b) && cJSON_IsString(n);
    if (ok) {
        *lat = a->valuedouble;
        *lon = b->valuedouble;
        cJSON *admin = cJSON_GetObjectItem(v, "admin1");
        snprintf(name, cap, "%s%s%s", cJSON_IsString(admin) ? admin->valuestring : "",
                 cJSON_IsString(admin) ? " " : "", n->valuestring);
    }
    cJSON_Delete(root);
    return ok;
}
static cJSON *fetch_weather(double lat, double lon) {
    char url[768];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/"
             "forecast?latitude=%.5f&longitude=%.5f&current=temperature_2m,apparent_temperature,"
             "weather_code&daily=temperature_2m_max,temperature_2m_min&timezone=Asia%%2FShanghai&"
             "forecast_days=1",
             lat, lon);
    char *s = request(url, NULL, NULL);
    if (!s)
        return NULL;
    cJSON *j = cJSON_Parse(s);
    free(s);
    return j;
}
static double number(cJSON *j, const char *name, double def) {
    cJSON *v = cJSON_GetObjectItem(j, name);
    return cJSON_IsNumber(v) ? v->valuedouble : def;
}
static void update_weather(void) {
    terminal_config_t c;
    config_snapshot(&c);
    if (!state->online || !c.location_set)
        return;
    cJSON *r = fetch_weather(c.latitude, c.longitude);
    cJSON *now = cJSON_GetObjectItem(r, "current"), *daily = cJSON_GetObjectItem(r, "daily");
    double temp = number(now, "temperature_2m", NAN);
    state_lock();
    if (isfinite(temp)) {
        state->temperature = temp;
        state->feels = number(now, "apparent_temperature", temp);
        state->weather_code = number(now, "weather_code", 0);
        cJSON *lo = cJSON_GetArrayItem(cJSON_GetObjectItem(daily, "temperature_2m_min"), 0),
              *hi = cJSON_GetArrayItem(cJSON_GetObjectItem(daily, "temperature_2m_max"), 0);
        state->low = cJSON_IsNumber(lo) ? lo->valuedouble : temp;
        state->high = cJSON_IsNumber(hi) ? hi->valuedouble : temp;
        state->weather_valid = true;
        state->weather_updated = time(NULL);
        snprintf(state->weather_city, sizeof(state->weather_city), "%s", c.city);
        cJSON *t = cJSON_GetObjectItem(now, "time");
        snprintf(state->weather_time, sizeof(state->weather_time), "%s",
                 cJSON_IsString(t) ? t->valuestring : "");
        state->weather_error[0] = 0;
    } else
        strcpy(state->weather_error, "天气更新失败，保留上次数据");
    state_unlock();
    cJSON_Delete(r);
}
char *weather_tool(const char *city) {
    terminal_config_t c;
    config_snapshot(&c);
    double lat = c.latitude, lon = c.longitude;
    char name[80];
    strcpy(name, c.city);
    if (city && *city) {
        if (!locate(city, &lat, &lon, name, sizeof(name)))
            return strdup("{\"error\":\"城市查询失败\"}");
    } else if (!c.location_set)
        return strdup("{\"error\":\"请先设置天气城市\"}");
    cJSON *r = fetch_weather(lat, lon);
    if (!r)
        return strdup("{\"error\":\"天气服务不可用\"}");
    cJSON_AddStringToObject(r, "city", name);
    cJSON_AddStringToObject(r, "source", "Open-Meteo");
    char *s = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    return s;
}
char *search_tool(const char *query) {
    terminal_config_t c;
    config_snapshot(&c);
    if (!c.search_key[0])
        return strdup("{\"error\":\"尚未配置网页搜索密钥，请在配置网页填写 Tavily API Key\"}");
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "query", query);
    cJSON_AddNumberToObject(body, "max_results", 3);
    cJSON_AddBoolToObject(body, "include_raw_content", false);
    char *p = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    char *s = request("https://api.tavily.com/search", p, c.search_key);
    free(p);
    return s ? s : strdup("{\"error\":\"搜索服务不可用\"}");
}
static void synced(struct timeval *tv) {
    state_lock();
    state->time_valid = true;
    state_unlock();
    terminal_submit(JOB_WEATHER, NULL, NULL, 0);
}
static void events(void *a, esp_event_base_t base, int32_t id, void *data) {
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        state_lock();
        state->online = true;
        snprintf(state->ip, sizeof(state->ip), IPSTR, IP2STR(&e->ip_info.ip));
        state_unlock();
        if (!sntp_started) {
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "ntp.aliyun.com");
            esp_sntp_setservername(1, "pool.ntp.org");
            esp_sntp_set_sync_interval(86400000);
            esp_sntp_set_time_sync_notification_cb(synced);
            esp_sntp_init();
            sntp_started = true;
        } else
            esp_sntp_restart();
        terminal_submit(JOB_WEATHER, NULL, NULL, 0);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        state_lock();
        state->online = false;
        state->ip[0] = 0;
        state_unlock();
    }
}
static const char web_page[] =
    "<!doctype html><html lang='zh'><meta charset='utf-8'><meta name='viewport' "
    "content='width=device-width'><title>桌面终端配置</"
    "title><style>body{background:#101827;color:#eee;font:17px "
    "sans-serif;max-width:600px;margin:30px "
    "auto;padding:20px}input,button{box-sizing:border-box;width:100%;padding:12px;margin:8px "
    "0;background:#203047;color:white;border:1px solid "
    "#456;border-radius:9px}small{color:#bac8d8}</style><h1>桌面终端配置</"
    "h1><small>在设备屏幕开启配置后使用。密钥留空表示保留原值。请仅在可信局域网使用。</small><form "
    "id='f'>"
    "<label>设备屏幕上的配对码<input name='token' required maxlength='32'></label><label>StepFun "
    "API Key<input name='api_key' type='password' maxlength='255'></label><label>WebSocket "
    "地址<input name='endpoint' placeholder='wss://api.stepfun.ai/v1/realtime' "
    "maxlength='159'></label><label>模型<input "
    "name='model' placeholder='stepaudio-3-realtime-preview' maxlength='79'></label><label>声音 "
    "ID<input name='voice' placeholder='soft-spoken-gentleman' "
    "maxlength='47'></label><label>天气城市（中文或拼音）<input name='city' "
    "maxlength='79'></label><label>Tavily 搜索 Key（可选）<input name='search_key' type='password' "
    "maxlength='159'></label><button>保存</button></form><p id='s'></p><script>f.onsubmit=async "
    "e=>{e.preventDefault();s.textContent='正在保存…';try{let r=await "
    "fetch('/config',{method:'POST',headers:{'Content-Type':'application/"
    "json'},body:JSON.stringify(Object.fromEntries(new FormData(f)))});s.textContent=await "
    "r.text();if(r.ok){f.api_key.value='';f.search_key.value=''}}catch(e){s.textContent='连接失败'}"
    "}</script></html>";
static esp_err_t root_get(httpd_req_t *r) {
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_send(r, web_page, HTTPD_RESP_USE_STRLEN);
}
static bool copy_field(cJSON *j, const char *key, char *out, size_t size) {
    cJSON *v = cJSON_GetObjectItem(j, key);
    if (!v)
        return true;
    if (!cJSON_IsString(v) || strlen(v->valuestring) >= size || strchr(v->valuestring, '\r') ||
        strchr(v->valuestring, '\n'))
        return false;
    if (*v->valuestring)
        strcpy(out, v->valuestring);
    return true;
}
static esp_err_t post_config(httpd_req_t *r) {
    char body[2048];
    if (r->content_len <= 0 || r->content_len >= sizeof(body))
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "请求过大");
    int n = 0;
    while (n < r->content_len) {
        int k = httpd_req_recv(r, body + n, r->content_len - n);
        if (k <= 0)
            return ESP_FAIL;
        n += k;
    }
    body[n] = 0;
    cJSON *j = cJSON_Parse(body);
    cJSON *token = cJSON_GetObjectItem(j, "token");
    if (!cJSON_IsString(token) || strcmp(token->valuestring, web_token) ||
        esp_timer_get_time() > web_until) {
        cJSON_Delete(j);
        return httpd_resp_send_err(r, HTTPD_403_FORBIDDEN, "配对码无效或已过期");
    }
    terminal_config_t c;
    config_snapshot(&c);
    char city[80] = "";
    bool ok = copy_field(j, "endpoint", c.endpoint, sizeof(c.endpoint)) &&
              copy_field(j, "api_key", c.api_key, sizeof(c.api_key)) &&
              copy_field(j, "model", c.model, sizeof(c.model)) &&
              copy_field(j, "voice", c.voice, sizeof(c.voice)) &&
              copy_field(j, "search_key", c.search_key, sizeof(c.search_key)) &&
              copy_field(j, "city", city, sizeof(city));
    cJSON_Delete(j);
    if (!ok || strncmp(c.endpoint, "wss://", 6) || strpbrk(c.endpoint, " ?#@") ||
        strspn(c.model, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") !=
            strlen(c.model))
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "配置字段无效");
    if (terminal_config_save(&c) != ESP_OK) {
        return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "保存失败");
    }
    if (city[0]) {
        terminal_submit(JOB_CITY, city, NULL, 0);
    }
    return httpd_resp_sendstr(r, "已保存。城市解析结果请查看设备屏幕。");
}
static void web_toggle(bool enable) {
    if (web) {
        httpd_stop(web);
        web = NULL;
    }
    state_lock();
    state->web_enabled = false;
    state_unlock();
    if (!enable)
        return;
    if (!state->online) {
        terminal_notice("请先连接 Wi-Fi");
        return;
    }
    snprintf(web_token, sizeof(web_token), "%08lx", (unsigned long)esp_random());
    web_until = esp_timer_get_time() + 600000000LL;
    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    c.stack_size = 6144;
    c.max_uri_handlers = 4;
    c.lru_purge_enable = true;
    if (httpd_start(&web, &c) == ESP_OK) {
        httpd_uri_t get = {.uri = "/", .method = HTTP_GET, .handler = root_get},
                    post = {.uri = "/config", .method = HTTP_POST, .handler = post_config};
        httpd_register_uri_handler(web, &get);
        httpd_register_uri_handler(web, &post);
        state_lock();
        state->web_enabled = true;
        snprintf(state->web_code, sizeof(state->web_code), "%s", web_token);
        state_unlock();
        terminal_notice("http://%s\n配对码 %s（10 分钟）", state->ip, web_token);
    } else
        terminal_notice("配置网页启动失败");
}
void network_job(const terminal_job_t *j) {
    if (j->kind == JOB_WIFI_SCAN) {
        wifi_scan_config_t c = {0};
        if (esp_wifi_scan_start(&c, true) != ESP_OK) {
            terminal_notice("扫描失败");
            return;
        }
        wifi_ap_record_t ap[16];
        uint16_t n = 16;
        esp_wifi_scan_get_ap_records(&n, ap);
        state_lock();
        state->wifi_count = n;
        for (int i = 0; i < n; i++)
            snprintf(state->wifi_names[i], 33, "%s", ap[i].ssid);
        state->wifi_generation++;
        state_unlock();
        return;
    }
    if (j->kind == JOB_WIFI_CONNECT) {
        if (!*j->a || strlen(j->a) > 32 || strlen(j->b) > 63) {
            terminal_notice("Wi-Fi 信息无效");
            return;
        }
        terminal_config_t c;
        config_snapshot(&c);
        strcpy(c.ssid, j->a);
        strcpy(c.password, j->b);
        if (terminal_config_save(&c) != ESP_OK)
            return;
        wifi_config_t w = {0};
        memcpy(w.sta.ssid, c.ssid, strlen(c.ssid));
        memcpy(w.sta.password, c.password, strlen(c.password));
        esp_wifi_disconnect();
        esp_wifi_set_config(WIFI_IF_STA, &w);
        esp_wifi_connect();
        terminal_notice("正在连接 Wi-Fi");
        return;
    }
    if (j->kind == JOB_CITY) {
        terminal_config_t c;
        config_snapshot(&c);
        char name[80];
        double lat, lon;
        if (locate(j->a, &lat, &lon, name, sizeof(name))) {
            c.latitude = lat;
            c.longitude = lon;
            c.location_set = true;
            strcpy(c.city, name);
            terminal_config_save(&c);
            terminal_notice("天气城市：%s", name);
            update_weather();
        } else
            terminal_notice("城市查询失败，请尝试拼音或检查网络");
        return;
    }
    if (j->kind == JOB_WEB) {
        web_toggle(j->value);
        return;
    }
    update_weather();
}
static void maintenance(void *arg) {
    int elapsed = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        elapsed += 10;
        if (!state->online && config.ssid[0])
            esp_wifi_connect();
        if (elapsed >= 900) {
            terminal_submit(JOB_WEATHER, NULL, NULL, 0);
            elapsed = 0;
        }
        if (web && esp_timer_get_time() > web_until)
            terminal_submit(JOB_WEB, NULL, NULL, 0);
    }
}
void network_init(void) {
    setenv("TZ", "CST-8", 1);
    tzset();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    station = esp_netif_create_default_wifi_sta();
    assert(station);
    wifi_init_config_t c = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&c));
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, events, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, events, NULL);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    if (config.ssid[0])
        terminal_submit(JOB_WIFI_CONNECT, config.ssid, config.password, 0);
    xTaskCreate(maintenance, "network_tick", 3072, NULL, 2, NULL);
}
