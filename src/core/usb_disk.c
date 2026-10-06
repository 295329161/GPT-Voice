#include "usb_disk.h"
#include <string.h>
bool usb_disk_transfer(const usb_disk_t *d, uint32_t lba, uint32_t offset,
                       void *buffer, uint32_t size, bool write, uint8_t scratch[512]) {
    if (!d || !d->read || (write && !d->write) || !d->sectors || !buffer || !scratch)
        return false;
    uint64_t start = (uint64_t)lba * USB_DISK_SECTOR_SIZE + offset;
    uint64_t capacity = (uint64_t)d->sectors * USB_DISK_SECTOR_SIZE;
    if (start > capacity || size > capacity - start)
        return false;
    uint8_t *p = buffer;
    while (size) {
        uint32_t sector = start / USB_DISK_SECTOR_SIZE;
        unsigned skip = start % USB_DISK_SECTOR_SIZE;
        unsigned n = USB_DISK_SECTOR_SIZE - skip;
        if (n > size) n = size;
        if (!skip && n == USB_DISK_SECTOR_SIZE) {
            if (write ? !d->write(d->ctx, sector, p) : !d->read(d->ctx, sector, p))
                return false;
        } else {
            if (!d->read(d->ctx, sector, scratch)) return false;
            if (write) {
                memcpy(scratch + skip, p, n);
                if (!d->write(d->ctx, sector, scratch)) return false;
            } else memcpy(p, scratch + skip, n);
        }
        start += n; p += n; size -= n;
    }
    return true;
}
unsigned usb_transfer_wait_seconds(uint32_t start, uint32_t now, bool connected_once) {
    if (connected_once) return 0;
    uint32_t elapsed = now - start;
    return elapsed >= USB_TRANSFER_WAIT_MS ? 0 : (USB_TRANSFER_WAIT_MS - elapsed + 999) / 1000;
}
