#include "search_intent.h"
#include <ctype.h>
#include <string.h>
#include <stdio.h>

// Explicit retrieval requests have a device-controlled completion path because
// the realtime model may finish with a spoken promise without invoking a tool.
bool search_intent(const char *text) {
    if (!text || !*text) return false;
    static const char *decline[] = {"不用查", "不用搜", "不要查", "不要搜", "别查", "别搜", "停止搜索", "取消搜索"};
    for (unsigned i = 0; i < sizeof(decline) / sizeof(decline[0]); i++)
        if (strstr(text, decline[i])) return false;
    // Questions about the results already in this conversation need their
    // existing sources, not an unrelated search for the follow-up sentence.
    if (!strstr(text, "重新") && !strstr(text, "再查") && !strstr(text, "再搜")) {
        static const char *followup[] = {"刚才查到", "刚刚查到", "刚才的", "刚才搜", "刚刚搜", "第一条", "第二条", "第三条", "这条新闻", "那条新闻", "这部分", "那部分", "这方面", "那方面", "这个话题", "那个话题", "展开", "详细说", "具体讲"};
        for (unsigned i = 0; i < sizeof(followup) / sizeof(followup[0]); i++)
            if (strstr(text, followup[i])) return false;
    }
    // Leave the existing time/weather tools in control of their own requests.
    if (strstr(text, "天气") || strstr(text, "几点") || strstr(text, "星期几")) return false;
    static const char *terms[] = {"搜索", "搜一下", "搜一搜", "搜新闻", "搜资料", "查找", "查寻", "查询", "查一下", "查一查", "查新闻", "查资料", "查查", "联网", "上网", "新闻", "最新消息", "最新动态"};
    for (unsigned i = 0; i < sizeof(terms) / sizeof(terms[0]); i++)
        if (strstr(text, terms[i])) return true;
    char lower[768];
    size_t n = strlen(text);
    if (n >= sizeof(lower)) n = sizeof(lower) - 1;
    for (size_t i = 0; i < n; i++) lower[i] = (char)tolower((unsigned char)text[i]);
    lower[n] = 0;
    if (strstr(lower, "don't search") || strstr(lower, "do not search")) return false;
    return strstr(lower, "search ") || strstr(lower, "look up ") || strstr(lower, "news");
}

void search_query(const char *text, const char *today, char *out, size_t capacity) {
    if (!capacity) return;
    // Output-format instructions are not useful search keywords. Keep the topic
    // clause, stripping only common request prefixes and quantity modifiers.
    static const char *noise[] = {"请帮我", "帮我", "请", "联网", "搜索", "查询", "查寻", "查找", "查一下", "查一查", "搜一下", "一下", "的两条", "的一条", "两条", "一条"};
    size_t used = 0;
    bool has_date = false;
    for (const char *p = text; *p && used + 1 < capacity;) {
        if ((strstr(p, "给出") == p || strstr(p, "给我") == p ||
             strstr(p, "，") == p || *p == ',' || strstr(p, "。") == p) && used) break;
        bool skip = false;
        for (unsigned i = 0; i < sizeof(noise) / sizeof(noise[0]); i++) {
            size_t n = strlen(noise[i]);
            if (!strncmp(p, noise[i], n)) { p += n; skip = true; break; }
        }
        if (skip) continue;
        if (!strncmp(p, "今天", 6) || !strncmp(p, "今日", 6)) {
            p += 6;
            // Keep an explicit date immediately following "今天" only once.
            if (strncmp(p, "20", 2) && strncmp(p, "19", 2)) {
                size_t n = strlen(today);
                if (used + n + 1 < capacity) { memcpy(out + used, today, n); used += n; out[used++] = ' '; }
                has_date = true;
            }
            if (!strncmp(p, "的", 3)) p += 3;
            continue;
        }
        if ((!strncmp(p, "20", 2) || !strncmp(p, "19", 2)) &&
            isdigit((unsigned char)p[2]) && isdigit((unsigned char)p[3])) has_date = true;
        out[used++] = *p++;
    }
    // If a byte limit split a UTF-8 character, discard that partial character.
    if (used && ((unsigned char)out[used - 1] & 0x80)) {
        size_t start = used - 1;
        while (start && ((unsigned char)out[start] & 0xc0) == 0x80) start--;
        unsigned char first = (unsigned char)out[start];
        size_t need = first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
        if (used - start < need) used = start;
    }
    out[used] = 0;
    if (!has_date && strstr(text, "新闻") && used + strlen(today) + 1 < capacity)
        snprintf(out + used, capacity - used, " %s", today);
}

// Only explicit current-clock questions. Historical/conceptual dates and
// current news stay with the appropriate model/search path.
bool time_intent(const char *text) {
    if (!text || !*text || strstr(text, "新闻") || strstr(text, "天气") ||
        strstr(text, "明天") || strstr(text, "昨天") || strstr(text, "不用") || strstr(text, "不要")) return false;
    const char *current[] = {"今天", "今日", "现在", "当前"};
    bool now = false;
    for (unsigned i = 0; i < sizeof(current) / sizeof(current[0]); i++)
        if (strstr(text, current[i])) now = true;
    const char *questions[] = {"几点", "几号", "几月", "哪天", "星期几", "周几", "日期", "时间", "哪一年", "多少号"};
    for (unsigned i = 0; i < sizeof(questions) / sizeof(questions[0]); i++)
        if (strstr(text, questions[i]) && (now || !strcmp(text, questions[i]))) return true;
    char lower[768];
    size_t n = strlen(text);
    if (n >= sizeof(lower)) n = sizeof(lower) - 1;
    for (size_t i = 0; i < n; i++) lower[i] = (char)tolower((unsigned char)text[i]);
    lower[n] = 0;
    return strstr(lower, "what time is it") || strstr(lower, "today's date") ||
           strstr(lower, "what day is it") || strstr(lower, "current time");
}
