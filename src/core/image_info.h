#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Validate encoded PNG/baseline JPEG dimensions before invoking LVGL decoders.
bool image_dimensions(const uint8_t *data, size_t length, unsigned *width, unsigned *height);
