#pragma once
#include <stdbool.h>
#include <stddef.h>
typedef struct {
    char *data;
    size_t size, frame_size, frame_received;
    bool active;
} ws_text_t;
void ws_text_reset(ws_text_t *s);
// Returns 1 for a complete message, 0 for incomplete, -1 on malformed/oversize input.
// Control frames are ignored; the caller owns completed data until reset/next text.
int ws_text_feed(ws_text_t *s, unsigned opcode, bool fin, size_t frame_size,
                 size_t offset, const void *data, size_t size, size_t limit);
