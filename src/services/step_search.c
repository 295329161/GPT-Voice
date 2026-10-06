#include "esp_app_desc.h"
#include "core/terminal.h"
#include "core/search_evidence.h"
#include "services/step_search.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

typedef struct {
    char *body;
    size_t used;
    bool overflow;
    char session[192];
    int64_t deadline;
} search_http_t;
#define SEARCH_LIMIT 131072
static esp_err_t search_event(esp_http_client_event_t *e) {
    search_http_t *r = e->user_data;
    if (e->event_id == HTTP_EVENT_ON_HEADER &&
        !strcasecmp(e->header_key, "Mcp-Session-Id"))
        snprintf(r->session, sizeof(r->session), "%s", e->header_value);
    if (e->event_id == HTTP_EVENT_ON_DATA) {
        if (e->data_len < 0 || r->used + (size_t)e->data_len >= SEARCH_LIMIT) {
            r->overflow = true;
            return ESP_ERR_NO_MEM;
        }
        memcpy(r->body + r->used, e->data, e->data_len);
        r->used += e->data_len;
        r->body[r->used] = 0;
    }
    return ESP_OK;
}
static cJSON *search_rpc(search_http_t *r, const terminal_config_t *c,
                         const char *method, cJSON *params, int id) {
    r->used = 0;
    r->overflow = false;
    r->body[0] = 0;
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "jsonrpc", "2.0");
    if (id) cJSON_AddNumberToObject(j, "id", id);
    cJSON_AddStringToObject(j, "method", method);
    if (params) cJSON_AddItemToObject(j, "params", params);
    char *body = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!body) return NULL;
    int64_t remaining = (r->deadline - esp_timer_get_time()) / 1000;
    if (remaining <= 0) { free(body); return NULL; }
    esp_http_client_config_t cfg = {
        .url = "https://api.stepfun.com/step_plan/v1/mcp/web_search/mcp",
        .timeout_ms = remaining < 12000 ? remaining : 12000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = search_event, .user_data = r, .buffer_size = 2048,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) { free(body); return NULL; }
    char auth[sizeof(c->api_key) + 8];
    snprintf(auth, sizeof(auth), "Bearer %s", c->api_key);
    esp_http_client_set_header(h, "Authorization", auth);
    esp_http_client_set_header(h, "Content-Type", "application/json");
    esp_http_client_set_header(h, "Accept", "application/json, text/event-stream");
    esp_http_client_set_header(h, "MCP-Protocol-Version", "2024-11-05");
    if (r->session[0]) esp_http_client_set_header(h, "Mcp-Session-Id", r->session);
    esp_http_client_set_method(h, HTTP_METHOD_POST);
    esp_http_client_set_post_field(h, body, strlen(body));
    esp_err_t err = esp_http_client_perform(h);
    int code = esp_http_client_get_status_code(h);
    esp_http_client_cleanup(h);
    free(body);
    ESP_LOGI("search", "rpc=%s http=%d bytes=%u result=%s", method, code,
             (unsigned)r->used, esp_err_to_name(err));
    if (err != ESP_OK || code < 200 || code >= 300 || r->overflow) return NULL;
    if (!id) return cJSON_CreateObject();
    return search_rpc_reply(r->body, id);
}
bool step_search_available(void) {
    terminal_config_t c;
    config_snapshot(&c);
    return !strcmp(c.endpoint, "wss://api.stepfun.com/step_plan/v1/realtime") && c.api_key[0];
}
char *step_search_tool(const char *query) {
    terminal_config_t c;
    config_snapshot(&c);
    if (strcmp(c.endpoint, "wss://api.stepfun.com/step_plan/v1/realtime") || !c.api_key[0])
        return strdup("{\"error\":\"当前配置不支持 Step Plan 搜索\"}");
    search_http_t r = {.body = calloc(1, SEARCH_LIMIT),
                        .deadline = esp_timer_get_time() + 25000000};
    if (!r.body) return strdup("{\"error\":\"搜索内存不足\"}");
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "protocolVersion", "2024-11-05");
    cJSON_AddObjectToObject(p, "capabilities");
    cJSON *info = cJSON_AddObjectToObject(p, "clientInfo");
    cJSON_AddStringToObject(info, "name", "GPTVoice");
    cJSON_AddStringToObject(info, "version", esp_app_get_description()->version);
    char *output = NULL;
    cJSON *reply = search_rpc(&r, &c, "initialize", p, 1);
    if (reply && !cJSON_GetObjectItem(reply, "error")) {
        cJSON_Delete(reply);
        reply = search_rpc(&r, &c, "notifications/initialized", NULL, 0);
        bool initialized = reply != NULL;
        cJSON_Delete(reply);
        reply = NULL;
        if (initialized) {
            p = cJSON_CreateObject();
            cJSON_AddStringToObject(p, "name", "web_search");
            cJSON *args = cJSON_AddObjectToObject(p, "arguments");
            cJSON_AddStringToObject(args, "query", query);
            cJSON_AddNumberToObject(args, "n", 5);
            cJSON_AddBoolToObject(args, "use_common_search", true);
            reply = search_rpc(&r, &c, "tools/call", p, 2);
            cJSON *result = cJSON_GetObjectItem(reply, "result");
            output = search_evidence(result);
            if (output && strstr(output, c.api_key)) {
                free(output);
                output = NULL;
            }
        }
    }
    cJSON_Delete(reply);
    free(r.body);
    return output ? output : strdup("{\"error\":\"本次联网搜索失败或超时，未取得可核验资料。请明确告知用户，不能编造新闻或来源。\"}");
}
