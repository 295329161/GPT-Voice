#include "core/conversation.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static conversation_t c;
static char rendered[32768];
int main(void) {
    conversation_reset(&c);
    conversation_user_begin(&c, "u1");
    conversation_response_begin(&c, "r1");
    conversation_response_text(&c, "r1", "第一轮回答");
    conversation_user_begin(&c, "u2");
    conversation_response_begin(&c, "r2");
    conversation_response_text(&c, "r2", "第二轮");
    conversation_user_text(&c, "u2", "第二个问题");
    // First-turn ASR arrives after the second answer has already begun.
    conversation_user_text(&c, "u1", "第一个问题");
    conversation_response_text(&c, "r2", "回答");
    conversation_render(&c, rendered, sizeof(rendered));
    assert(!strcmp(rendered, "你：第一个问题\n助手：第一轮回答\n\n你：第二个问题\n助手：第二轮回答"));
    // A tool continuation belongs to the same user turn.
    conversation_response_begin(&c, "r2-tool");
    conversation_response_text(&c, "r2-tool", "，查询完成");
    assert(c.count == 2);
    conversation_render(&c, rendered, sizeof(rendered));
    assert(strstr(rendered, "第二轮回答，查询完成"));
    // Bounded history evicts complete turns, preserving speaker order.
    for (unsigned i = 3; i < 12; i++) {
        char id[16];
        snprintf(id, sizeof(id), "u%u", i);
        conversation_user_begin(&c, id);
        conversation_user_text(&c, id, id);
    }
    conversation_render(&c, rendered, sizeof(rendered));
    assert(c.count == CONVERSATION_TURNS);
    assert(!strncmp(rendered, "你：u4\n助手：", strlen("你：u4\n助手：")));
    char small[8];
    conversation_render(&c, small, sizeof(small));
    assert(small[sizeof(small) - 1] == 0);
    conversation_reset(&c);
    conversation_user_begin(&c, "utf8");
    char long_text[6000];
    for (unsigned i = 0; i < 1999; i++) memcpy(long_text + i * 3, "中", 3);
    long_text[5997] = 0;
    conversation_user_text(&c, "utf8", long_text);
    assert(strlen(c.turns[0].user) % 3 == 0);
    assert(strlen(c.turns[0].user) < sizeof(c.turns[0].user));
    puts("Conversation late ASR, streaming order, tool continuation and bounded UTF-8 history PASS");
}
