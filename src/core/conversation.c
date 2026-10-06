#include "conversation.h"
#include <string.h>

static void append(char *dest, size_t capacity, const char *text) {
    if (!capacity) return;
    size_t used = strlen(dest), count = strlen(text);
    size_t available = capacity - used - 1;
    if (count > available) {
        count = available;
        while (count && ((unsigned char)text[count] & 0xc0) == 0x80) count--;
    }
    memcpy(dest + used, text, count);
    dest[used + count] = 0;
}
static conversation_turn_t *find_user(conversation_t *c, const char *id) {
    for (unsigned i = 0; i < c->count; i++) {
        conversation_turn_t *turn = &c->turns[(c->first + i) % CONVERSATION_TURNS];
        if (!strcmp(turn->item_id, id)) return turn;
    }
    return NULL;
}
static conversation_turn_t *latest(conversation_t *c) {
    return c->count ? &c->turns[(c->first + c->count - 1) % CONVERSATION_TURNS] : NULL;
}
void conversation_reset(conversation_t *c) { memset(c, 0, sizeof(*c)); }
void conversation_user_begin(conversation_t *c, const char *id) {
    if (find_user(c, id)) return;
    if (c->count == CONVERSATION_TURNS) {
        c->first = (c->first + 1) % CONVERSATION_TURNS;
        c->count--;
    }
    conversation_turn_t *turn = &c->turns[(c->first + c->count++) % CONVERSATION_TURNS];
    memset(turn, 0, sizeof(*turn));
    append(turn->item_id, sizeof(turn->item_id), id);
}
void conversation_user_text(conversation_t *c, const char *id, const char *text) {
    conversation_turn_t *turn = find_user(c, id);
    if (!turn) {
        conversation_user_begin(c, id);
        turn = latest(c);
    }
    turn->user[0] = 0;
    append(turn->user, sizeof(turn->user), *text ? text : "（未识别到文字）");
}
void conversation_response_begin(conversation_t *c, const char *id) {
    if (!c->count) conversation_user_begin(c, "");
    conversation_turn_t *turn = latest(c);
    turn->response_id[0] = 0;
    append(turn->response_id, sizeof(turn->response_id), id);
}
void conversation_response_text(conversation_t *c, const char *id, const char *text) {
    for (unsigned i = 0; i < c->count; i++) {
        conversation_turn_t *turn = &c->turns[(c->first + i) % CONVERSATION_TURNS];
        if (!strcmp(turn->response_id, id)) {
            append(turn->assistant, sizeof(turn->assistant), text);
            return;
        }
    }
}
void conversation_render(const conversation_t *c, char *output, size_t capacity) {
    if (!capacity) return;
    output[0] = 0;
    for (unsigned i = 0; i < c->count; i++) {
        const conversation_turn_t *turn = &c->turns[(c->first + i) % CONVERSATION_TURNS];
        if (i) append(output, capacity, "\n\n");
        append(output, capacity, "你：");
        append(output, capacity, *turn->user ? turn->user : "（识别中…）");
        append(output, capacity, "\n助手：");
        append(output, capacity, turn->assistant);
    }
}
