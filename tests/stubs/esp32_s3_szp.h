#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <string.h>
#include <strings.h>
esp_err_t bsp_sdcard_mount(void);
esp_err_t bsp_sdcard_unmount(void);
esp_err_t esp_vfs_fat_info(const char *path, uint64_t *total, uint64_t *free_bytes);
