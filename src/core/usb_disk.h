#pragma once
#include <stdbool.h>
#include <stdint.h>
#define USB_DISK_SECTOR_SIZE 512u
#define USB_TRANSFER_WAIT_MS 60000u
typedef bool (*usb_disk_read_fn)(void *ctx, uint32_t sector, void *data);
typedef bool (*usb_disk_write_fn)(void *ctx, uint32_t sector, const void *data);
typedef struct {
    void *ctx;
    usb_disk_read_fn read;
    usb_disk_write_fn write;
    uint32_t sectors;
} usb_disk_t;
// Synchronous, bounded block I/O. Partial writes preserve adjacent bytes.
bool usb_disk_transfer(const usb_disk_t *disk, uint32_t lba, uint32_t offset,
                       void *buffer, uint32_t size, bool write, uint8_t scratch[512]);
unsigned usb_transfer_wait_seconds(uint32_t start_ms, uint32_t now_ms, bool connected_once);
