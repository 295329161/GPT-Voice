#include "ws_text.h"
#include <stdlib.h>
#include <string.h>
void ws_text_reset(ws_text_t *s) {
    free(s->data);
    memset(s, 0, sizeof(*s));
}
int ws_text_feed(ws_text_t *s, unsigned opcode, bool fin, size_t frame_size,
                 size_t offset, const void *data, size_t size, size_t limit) {
    if (opcode >= 8) return 0;
    if (opcode != 0 && opcode != 1) goto invalid;
    if (!offset) {
        if (opcode == 1) {
            ws_text_reset(s);
            s->active = true;
        } else if (!s->active || s->frame_received != s->frame_size) goto invalid;
        if (s->size > limit || frame_size > limit - s->size) goto invalid;
        char *next = realloc(s->data, s->size + frame_size + 1);
        if (!next) goto invalid;
        s->data = next;
        s->frame_size = frame_size;
        s->frame_received = 0;
    }
    if (!s->active || offset != s->frame_received || frame_size != s->frame_size ||
        offset > frame_size || size > frame_size - offset) goto invalid;
    if (size) memcpy(s->data + s->size, data, size);
    s->size += size;
    s->frame_received += size;
    s->data[s->size] = 0;
    if (fin && s->frame_received == s->frame_size) {
        s->active = false;
        return 1;
    }
    return 0;
invalid:
    ws_text_reset(s);
    return -1;
}
