#include "tools.h"
#include "cJSON.h"
#include "core/terminal.h"
#include <stdlib.h>
#include <string.h>
static const terminal_tool_t tools[] = {
    {"get_weather", "查询实时天气；city 留空使用设备设置的城市", "city", false, weather_tool},
    {"web_search", "搜索网页中的最新信息，返回来源", "query", true, search_tool}};
const terminal_tool_t *terminal_tools(size_t *count) {
    *count = sizeof(tools) / sizeof(tools[0]);
    return tools;
}
char *terminal_tool_execute(const char *name, const char *arguments) {
    cJSON *args = cJSON_Parse(arguments);
    char *result = NULL;
    if (cJSON_IsObject(args))
        for (size_t i = 0; i < sizeof(tools) / sizeof(tools[0]); i++) {
            const terminal_tool_t *t = &tools[i];
            if (strcmp(name, t->name))
                continue;
            cJSON *v = cJSON_GetObjectItemCaseSensitive(args, t->parameter);
            if ((!v && !t->required) || (cJSON_IsString(v) && (!t->required || *v->valuestring)))
                result = t->execute(v ? v->valuestring : "");
            break;
        }
    cJSON_Delete(args);
    return result ? result : strdup("{\"error\":\"工具名称或参数无效\"}");
}
