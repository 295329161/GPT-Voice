#include "tools.h"
#include "cJSON.h"
#include "core/terminal.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
static char *time_tool(const char *unused) {
    (void)unused;
    state_lock();
    bool valid = state->time_valid;
    state_unlock();
    if (!valid) return strdup("{\"error\":\"设备尚未完成网络校时，当前时间不可确认\"}");
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    char datetime[32];
    strftime(datetime, sizeof(datetime), "%Y-%m-%d %H:%M:%S", &local);
    static const char *weekdays[] = {"星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"};
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "datetime", datetime);
    cJSON_AddStringToObject(j, "timezone", "Asia/Shanghai");
    cJSON_AddStringToObject(j, "weekday", weekdays[local.tm_wday]);
    cJSON_AddNumberToObject(j, "unix_timestamp", (double)now);
    cJSON_AddStringToObject(j, "source", "设备 SNTP 校准时钟");
    char *result = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    return result;
}
static const terminal_tool_t tools[] = {
    {"get_time", "读取设备校准后的北京时间、日期和星期，无需参数", NULL, false, time_tool},
    {"get_weather", "查询实时天气；city 留空使用设备设置的城市", "city", false, weather_tool},
    {"external_web_search", "备用外部网页搜索，返回来源", "query", true, search_tool}};
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
            if (!t->parameter) {
                result = t->execute("");
                break;
            }
            cJSON *v = cJSON_GetObjectItemCaseSensitive(args, t->parameter);
            if ((!v && !t->required) || (cJSON_IsString(v) && (!t->required || *v->valuestring)))
                result = t->execute(v ? v->valuestring : "");
            break;
        }
    cJSON_Delete(args);
    return result ? result : strdup("{\"error\":\"工具名称或参数无效\"}");
}
