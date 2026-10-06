#include "core/search_evidence.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    cJSON *j = search_rpc_reply("{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{}}", 2);
    assert(j); cJSON_Delete(j);
    assert(!search_rpc_reply("{\"id\":1,\"result\":{}}", 2));
    j = search_rpc_reply("event:message\ndata:{\"method\":\"notifications/progress\"}\n\ndata: {\"id\":2,\"result\":{}}\n\n", 2);
    assert(j); cJSON_Delete(j);
    assert(!search_rpc_reply("data: {\"id\":2,", 2));
    j = cJSON_Parse("{\"isError\":true,\"content\":[]}");
    assert(!search_evidence(j)); cJSON_Delete(j);
    cJSON *payload = cJSON_CreateObject(), *rows = cJSON_AddArrayToObject(payload, "results");
    for (int i = 0; i < 5; i++) {
        cJSON *row = cJSON_CreateObject(); cJSON_AddItemToArray(rows, row);
        cJSON_AddStringToObject(row, "title", "当日新闻");
        cJSON_AddStringToObject(row, "url", "https://example.com/2026/10/06/news");
        cJSON_AddStringToObject(row, "time", "2026-10-06T10:00:00");
        cJSON_AddStringToObject(row, "snippet", "机器人在西安展出。https://example.com/story 现场可互动。www.example.com 继续报道。");
        char huge[20000]; memset(huge, 'x', sizeof(huge)-1); huge[sizeof(huge)-1] = 0;
        cJSON_AddStringToObject(row, "content", huge);
    }
    char *raw = cJSON_PrintUnformatted(payload);
    j = cJSON_CreateObject();
    cJSON *blocks = cJSON_AddArrayToObject(j, "content"), *block = cJSON_CreateObject();
    cJSON_AddItemToArray(blocks, block); cJSON_AddStringToObject(block, "text", raw);
    free(raw); cJSON_Delete(payload);
    char *result = search_evidence(j); assert(result && strlen(result) < 20000);
    cJSON *parsed = cJSON_Parse(result);
    assert(cJSON_GetArraySize(cJSON_GetObjectItem(parsed, "results")) == 5);
    assert(strstr(result, "2026-10-06T10:00:00"));
    assert(strstr(result, "xxxx")); // Bounded detail retained for follow-up questions.
    char *sources = NULL;
    char *report = search_briefing(result, "2026-10-06", &sources);
    assert(report && sources && strstr(report, "2026-10-06"));
    assert(strstr(report, "机器人在西安展出") && strstr(report, "现场可互动"));
    assert(strstr(report, "details_for_followup") && strstr(report, "xxxx"));
    assert(!strstr(report, "http") && !strstr(report, "www.") && !strstr(report, "example.com"));
    assert(strstr(sources, "https://example.com/2026/10/06/news"));
    assert(strstr(sources, "5. 当日新闻"));
    free(sources);
    free(report);
    report = search_briefing(result, "2026-10-07", &sources);
    assert(report && !strstr(report, "https://") && strstr(report, "没有找到"));
    assert(!strstr(sources, "https://"));
    free(sources);
    free(report);
    free(result); cJSON_Delete(parsed); cJSON_Delete(j);
    puts("MCP reply matching, large evidence, sources and error handling PASS");
}
