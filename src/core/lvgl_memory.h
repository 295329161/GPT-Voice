#pragma once
#include "esp_heap_caps.h"

/* LCD transfer buffers are separately allocated by esp_lvgl_port with DMA caps. */
static inline void *terminal_lv_malloc(size_t size) {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static inline void *terminal_lv_realloc(void *ptr, size_t size) {
    return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
