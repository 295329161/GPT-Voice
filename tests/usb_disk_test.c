#include "core/usb_disk.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t media[4 * USB_DISK_SECTOR_SIZE];
static unsigned reads, writes;
static bool fail_read, fail_write;
static bool read_sector(void *ctx, uint32_t sector, void *out) {
    assert(ctx == media && sector < 4);
    reads++;
    if (fail_read) return false;
    memcpy(out, media + sector * 512, 512);
    return true;
}
static bool write_sector(void *ctx, uint32_t sector, const void *in) {
    assert(ctx == media && sector < 4);
    writes++;
    if (fail_write) return false;
    memcpy(media + sector * 512, in, 512);
    return true;
}
int main(void) {
    usb_disk_t disk = {media, read_sector, write_sector, 4};
    uint8_t scratch[512], input[1100], output[1100], original[sizeof(media)];
    for (unsigned i = 0; i < sizeof(media); i++) media[i] = i * 7;
    for (unsigned i = 0; i < sizeof(input); i++) input[i] = i * 13 + 5;
    memcpy(original, media, sizeof(media));
    // Cross four sectors, including read/modify/write at both boundaries.
    assert(usb_disk_transfer(&disk, 0, 477, input, sizeof(input), true, scratch));
    assert(reads == 2 && writes == 4);
    assert(!memcmp(media, original, 477));
    assert(!memcmp(media + 477, input, sizeof(input)));
    assert(!memcmp(media + 1577, original + 1577, sizeof(media) - 1577));
    assert(usb_disk_transfer(&disk, 0, 477, output, sizeof(output), false, scratch));
    assert(!memcmp(input, output, sizeof(input)));
    // Offsets may advance past a sector; full sectors avoid extra reads.
    reads = writes = 0;
    assert(usb_disk_transfer(&disk, 0, 512, input, 512, true, scratch));
    assert(reads == 0 && writes == 1);
    assert(!memcmp(media + 512, input, 512));
    reads = writes = 0;
    assert(!usb_disk_transfer(&disk, 3, 511, input, 2, true, scratch));
    assert(!usb_disk_transfer(&disk, UINT32_MAX, UINT32_MAX, input, 1, false, scratch));
    assert(!usb_disk_transfer(&disk, 0, 0, input, UINT32_MAX, false, scratch));
    assert(usb_disk_transfer(&disk, 4, 0, input, 0, true, scratch));
    assert(!usb_disk_transfer(&disk, 4, 1, input, 0, true, scratch));
    assert(reads == 0 && writes == 0);
    memcpy(original, media, sizeof(media));
    fail_read = true;
    assert(!usb_disk_transfer(&disk, 0, 1, input, 20, true, scratch));
    assert(writes == 0 && !memcmp(media, original, sizeof(media)));
    assert(!usb_disk_transfer(&disk, 0, 0, output, 512, false, scratch));
    fail_read = false; fail_write = true;
    assert(!usb_disk_transfer(&disk, 0, 0, input, 512, true, scratch));
    assert(!usb_disk_transfer(&disk, 0, 1, input, 20, true, scratch));
    assert(!memcmp(media, original, sizeof(media)));
    assert(!usb_disk_transfer(NULL, 0, 0, input, 1, false, scratch));
    assert(!usb_disk_transfer(&disk, 0, 0, NULL, 1, false, scratch));
    disk.write = NULL;
    assert(!usb_disk_transfer(&disk, 0, 0, input, 1, true, scratch));
    assert(usb_transfer_wait_seconds(100, 100, false) == 60);
    assert(usb_transfer_wait_seconds(100, 101, false) == 60);
    assert(usb_transfer_wait_seconds(100, 1100, false) == 59);
    assert(usb_transfer_wait_seconds(100, 60099, false) == 1);
    assert(usb_transfer_wait_seconds(100, 60100, false) == 0);
    assert(usb_transfer_wait_seconds(100, 60101, false) == 0);
    assert(usb_transfer_wait_seconds(UINT32_MAX - 500, 499, false) == 59);
    assert(usb_transfer_wait_seconds(100, 100, true) == 0);
    puts("USB bounded I/O, partial-sector preservation, errors and 60s deadline PASS");
}
