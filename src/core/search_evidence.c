#include "search_evidence.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static cJSON *matching(const char *text, int id) {
    cJSON *j = cJSON_Parse(text), *value = cJSON_GetObjectItem(j, "id");
    if (cJSON_IsNumber(value) && value->valuedouble == id) return j;
    cJSON_Delete(j);
    return NULL;
}
cJSON *search_rpc_reply(const char *body, int id) {
    if (!body) return NULL;
    cJSON *j = matching(body, id);
    if (j) return j;
    // Skip SSE notifications and accept only the response to this request.
    const char *p = body;
    while ((p = strstr(p, "data:"))) {
        bool start = p == body || p[-1] == '\n';
        p += 5;
        if (start && (j = matching(p, id))) return j;
    }
    return NULL;
}
static void field(cJSON *out, cJSON *in, const char *name, size_t limit) {
    cJSON *v = cJSON_GetObjectItem(in, name);
    if (!cJSON_IsString(v)) return;
    size_t n = strlen(v->valuestring);
    if (n > limit) {
        n = limit;
        while (n && ((unsigned char)v->valuestring[n] & 0xc0) == 0x80) n--;
    }
    char *text = malloc(n + 1);
    if (!text) return;
    memcpy(text, v->valuestring, n);
    text[n] = 0;
    cJSON_AddStringToObject(out, name, text);
    free(text);
}
char *search_evidence(cJSON *result) {
    if (!cJSON_IsObject(result) || cJSON_IsTrue(cJSON_GetObjectItem(result, "isError"))) return NULL;
    cJSON *root = NULL, *block;
    cJSON_ArrayForEach(block, cJSON_GetObjectItem(result, "content")) {
        cJSON *text = cJSON_GetObjectItem(block, "text");
        if (!cJSON_IsString(text)) continue;
        root = cJSON_Parse(text->valuestring);
        if (cJSON_IsArray(cJSON_GetObjectItem(root, "results"))) break;
        cJSON_Delete(root);
        root = NULL;
    }
    if (!root) return NULL;
    cJSON *out = cJSON_CreateObject(), *items = cJSON_AddArrayToObject(out, "results");
    cJSON_AddStringToObject(out, "source", "StepSearch");
    cJSON *item;
    cJSON_ArrayForEach(item, cJSON_GetObjectItem(root, "results")) {
        cJSON *url = cJSON_GetObjectItem(item, "url"), *title = cJSON_GetObjectItem(item, "title");
        if (!cJSON_IsString(url) || !cJSON_IsString(title) ||
            (strncmp(url->valuestring, "https://", 8) && strncmp(url->valuestring, "http://", 7)) ||
            strlen(url->valuestring) > 1024) continue;
        cJSON *row = cJSON_CreateObject();
        cJSON_AddItemToArray(items, row);
        field(row, item, "title", 384);
        field(row, item, "url", 1024);
        field(row, item, "time", 64);
        field(row, item, "snippet", 1200);
        field(row, item, "content", 3200);
        if (cJSON_GetArraySize(items) == 5) break;
    }
    cJSON_AddNumberToObject(out, "count", cJSON_GetArraySize(items));
    char *text = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    cJSON_Delete(root);
    return text;
}

// URLs belong to the screen. Remove inline links too: search snippets and
// article bodies can contain URLs even after the dedicated url field is removed.
static char *spoken_text(const char *s) {
    char *out = malloc(strlen(s) + 1);
    if (!out) return NULL;
    size_t n = 0;
    while (*s) {
        if (!strncmp(s, "http://", 7) || !strncmp(s, "https://", 8) || !strncmp(s, "www.", 4)) {
            while (*s && (unsigned char)*s > 32 && (unsigned char)*s < 127 &&
                   *s != '"' && *s != '<' && *s != '>') s++;
            if (n && out[n - 1] != ' ') out[n++] = ' ';
        } else out[n++] = *s++;
    }
    out[n] = 0;
    return out;
}
static const char *value(cJSON *j, const char *key) {
    cJSON *v = cJSON_GetObjectItem(j, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}
static void spoken_field(cJSON *out, cJSON *in, const char *from, const char *to) {
    char *s = spoken_text(value(in, from));
    if (s) { cJSON_AddStringToObject(out, to, s); free(s); }
}
char *search_briefing(const char *evidence, const char *required_date, char **sources) {
    *sources = NULL;
    cJSON *j = cJSON_Parse(evidence), *rows = cJSON_GetObjectItem(j, "results");
    if (!cJSON_IsArray(rows)) { cJSON_Delete(j); return NULL; }
    char *links = calloc(1, 8192);
    if (!links) { cJSON_Delete(j); return NULL; }
    cJSON *out = cJSON_CreateObject(), *reports = cJSON_AddArrayToObject(out, "reports");
    size_t used = 0;
    unsigned count = 0;
    cJSON *row;
    cJSON_ArrayForEach(row, rows) {
        const char *title = value(row, "title"), *url = value(row, "url"), *date = value(row, "time");
        if (!*title || !*url) continue;
        if (required_date && *required_date && strncmp(date, required_date, strlen(required_date))) continue;
        cJSON *report = cJSON_CreateObject();
        cJSON_AddItemToArray(reports, report);
        cJSON_AddNumberToObject(report, "source_id", ++count);
        spoken_field(report, row, "title", "headline");
        spoken_field(report, row, "time", "reported_date");
        spoken_field(report, row, "snippet", "facts");
        spoken_field(report, row, "content", "details_for_followup");
        int n = snprintf(links + used, 8192 - used, "%u. %s\n日期：%s\n%s\n\n",
                         count, title, *date ? date : "未标注", url);
        if (n >= 0 && (size_t)n < 8192 - used) used += n;
        else { links[used] = 0; break; }
        if (count == 5) break;
    }
    cJSON_AddNumberToObject(out, "count", count);
    if (!count) {
        cJSON_AddStringToObject(out, "notice", "本次检索没有找到日期匹配的报道，不能把旧新闻当作今天的新闻。");
        snprintf(links, 8192, "本次检索未找到日期匹配的来源。");
    }
    char *briefing = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    cJSON_Delete(j);
    if (!briefing) free(links);
    else *sources = links;
    return briefing;
}
