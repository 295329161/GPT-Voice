#include "core/search_intent.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    assert(time_intent("今天是几月几号，星期几？"));
    assert(time_intent("现在几点了？"));
    assert(time_intent("What's today's date?"));
    assert(!time_intent("今天新闻发生在几点？"));
    assert(!time_intent("查询明天天气"));
    assert(!time_intent("春节是几月几号"));
    assert(!time_intent("时间为什么会变慢"));

    assert(search_intent("请联网查询今天2026年10月6日的新闻，给出发布日期和链接"));
    assert(search_intent("帮我查找ESP32-S3技术手册"));
    assert(search_intent("搜索今天科技新闻，不要旧新闻"));
    assert(search_intent("查寻一下这个芯片的资料"));
    assert(search_intent("有什么最新消息？"));
    assert(search_intent("Search today's news"));
    assert(!search_intent("不要搜索了"));
    assert(!search_intent("取消搜索，讲一个笑话"));
    assert(!search_intent("Don't search, just explain"));
    assert(!search_intent("查询今天星期几"));
    assert(!search_intent("帮我查一下深圳天气"));
    assert(!search_intent("你好，介绍一下你自己"));
    assert(!search_intent("刚才查到的新闻是哪一天的？一句话回答。"));
    assert(!search_intent("我想了解机器人那部分的新闻，展开说说"));
    assert(!search_intent("第二条新闻详细说一下"));
    assert(!search_intent("第三条具体讲讲"));
    assert(search_intent("重新搜索机器人那部分的新闻"));
    assert(search_intent("重新搜索今天的新闻"));
    assert(!search_intent(NULL));
    assert(!search_intent(""));
    char query[256];
    search_query("请联网查询今天2026年10月6日的两条科技新闻，给出标题、发布日期和原文链接，不要旧新闻。", "2026-10-06", query, sizeof(query));
    assert(!strcmp(query, "2026年10月6日科技新闻"));
    search_query("搜索今天科技新闻", "2026-10-06", query, sizeof(query));
    assert(!strcmp(query, "2026-10-06 科技新闻"));
    search_query("帮我查一下今天的科技新闻，先说重点", "2026-10-06", query, sizeof(query));
    assert(!strcmp(query, "2026-10-06 科技新闻"));
    search_query("帮我查找ESP32-S3技术手册", "2026-10-06", query, sizeof(query));
    assert(!strcmp(query, "ESP32-S3技术手册"));
    search_query("搜索2025年科技新闻", "2026-10-06", query, sizeof(query));
    assert(!strcmp(query, "2025年科技新闻"));
    search_query("搜索芯片资料", "2026-10-06", query, 5);
    assert(!strcmp(query, "芯"));
    puts("Search intent, cancellation and time/weather isolation PASS");
}
