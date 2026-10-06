#pragma once
#include <stddef.h>
#define CONVERSATION_TURNS 8
typedef struct {
    char item_id[96], response_id[96];
    char user[768], assistant[3072];
} conversation_turn_t;
typedef struct {
    conversation_turn_t turns[CONVERSATION_TURNS];
    unsigned first, count;
} conversation_t;
void conversation_reset(conversation_t *c);
void conversation_user_begin(conversation_t *c, const char *item_id);
void conversation_user_text(conversation_t *c, const char *item_id, const char *text);
void conversation_response_begin(conversation_t *c, const char *response_id);
void conversation_response_text(conversation_t *c, const char *response_id, const char *text);
void conversation_render(const conversation_t *c, char *output, size_t capacity);
