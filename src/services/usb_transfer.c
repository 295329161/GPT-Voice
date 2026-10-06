#include "usb_transfer.h"
#include "core/terminal.h"
#include "core/usb_disk.h"
#include "esp32_s3_szp.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_private/usb_phy.h"
#include "esp_timer.h"
#include "tusb.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

// USB never sees the ESP flash/NVS, only a card whose local VFS is unmounted.
// All block callbacks run synchronously in the same USB task. Nothing remains
// queued for a later SD write when the SCSI command is acknowledged.
enum { EXIT_NONE, EXIT_CANCEL, EXIT_TIMEOUT, EXIT_EJECT, EXIT_DISCONNECT, EXIT_ERROR };
static atomic_bool busy, task_done, ready, host_seen, suspended, cancel_requested;
static atomic_bool io_error, test_nohost;
static atomic_int requested, exit_reason;
static atomic_uint started_ms, last_read_ms, last_write_ms;
static bool task_created, normal_unmounted, raw_host_initialized, raw_slot_initialized;
static sdmmc_card_t *card;
static usb_phy_handle_t serial_phy;
static uint8_t *scratch;
static usb_disk_t disk;
static uint64_t read_bytes, written_bytes;
static portMUX_TYPE counters_mux = portMUX_INITIALIZER_UNLOCKED;
static char serial_number[24];
// Only the USB task touches these SCSI fields.
static bool eject_pending, removal_prevented;
static uint32_t eject_ack_ms;
static bool eject_acknowledged;
static uint32_t millis(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static const tusb_desc_device_t descriptor = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200, .bMaxPacketSize0 = 64,
    .idVendor = 0x303a, .idProduct = 0x4002, .bcdDevice = 0x0110,
    .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3, .bNumConfigurations = 1,
};
static const uint8_t configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN, 0, 100),
    TUD_MSC_DESCRIPTOR(0, 4, 0x01, 0x81, 64),
};
const uint8_t *tud_descriptor_device_cb(void) { return (const uint8_t *)&descriptor; }
const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    return index == 0 ? configuration_descriptor : NULL;
}
const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static uint16_t text[40];
    const char *strings[] = {NULL, "GPT Voice", "GPT Voice SD", serial_number, "SD Card"};
    unsigned n;
    if (index == 0) { text[1] = 0x0409; n = 1; }
    else {
        if (index >= sizeof(strings) / sizeof(strings[0])) return NULL;
        n = strlen(strings[index]);
        if (n > 38) n = 38;
        for (unsigned i = 0; i < n; i++) text[i + 1] = (uint8_t)strings[index][i];
    }
    text[0] = (TUSB_DESC_STRING << 8) | (2 * n + 2);
    return text;
}
void tud_mount_cb(void) {
    host_seen = true;
    suspended = false;
    ESP_LOGI("usb_transfer", "host configured");
}
void tud_umount_cb(void) {
    // A bus reset is NOT an unplug event. TinyUSB distinguishes them.
    if (host_seen) { ready = false; exit_reason = EXIT_DISCONNECT; }
}
void tud_suspend_cb(bool remote_wakeup) { (void)remote_wakeup; suspended = true; }
void tud_resume_cb(void) { suspended = false; }
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor[8], uint8_t product[16], uint8_t revision[4]) {
    (void)lun;
    memcpy(vendor, "GPTVOICE", 8);
    memcpy(product, "SD CARD         ", 16);
    memcpy(revision, "1.10", 4);
}
bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    if (lun != 0 || !ready) { tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0); return false; }
    return true;
}
void tud_msc_capacity_cb(uint8_t lun, uint32_t *count, uint16_t *size) {
    *count = lun == 0 && ready ? disk.sectors : 0;
    *size = USB_DISK_SECTOR_SIZE;
}
bool tud_msc_is_writable_cb(uint8_t lun) { return lun == 0 && ready; }
bool tud_msc_prevent_allow_medium_removal_cb(uint8_t lun, uint8_t prohibit, uint8_t control) {
    (void)control;
    if (lun != 0) return false;
    removal_prevented = prohibit != 0;
    return true;
}
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power, bool start, bool load_eject) {
    (void)power;
    if (lun != 0) return false;
    if (load_eject && !start) {
        if (removal_prevented) {
            tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x53, 2);
            return false;
        }
        ready = false;
        eject_pending = true;
    }
    return true;
}
void tud_msc_scsi_complete_cb(uint8_t lun, const uint8_t command[16]) {
    (void)lun;
    if (command[0] == SCSI_CMD_START_STOP_UNIT && eject_pending) {
        // Wait until the eject CSW has actually been sent, then disconnect.
        eject_acknowledged = true;
        eject_ack_ms = millis();
    }
}
int32_t tud_msc_scsi_cb(uint8_t lun, const uint8_t command[16], void *buffer, uint16_t size) {
    (void)buffer; (void)size;
    // SYNCHRONIZE CACHE(10). SD writes above are synchronous; no device cache.
    if (command[0] == 0x35 && tud_msc_test_unit_ready_cb(lun)) return 0;
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0);
    return -1;
}
static bool sector_read(void *ctx, uint32_t sector, void *data) {
    return sdmmc_read_sectors(ctx, data, sector, 1) == ESP_OK;
}
static bool sector_write(void *ctx, uint32_t sector, const void *data) {
    return sdmmc_write_sectors(ctx, data, sector, 1) == ESP_OK;
}
static int32_t transfer(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer,
                        uint32_t size, bool write) {
    if (!tud_msc_test_unit_ready_cb(lun)) return -1;
    if (!usb_disk_transfer(&disk, lba, offset, buffer, size, write, scratch)) {
        io_error = true;
        tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, write ? 0x0c : 0x11, 0);
        ESP_LOGE("usb_transfer", "SD %s failed lba=%lu size=%lu", write ? "write" : "read",
                 (unsigned long)lba, (unsigned long)size);
        // Preserve host ownership so an I/O failure cannot trigger concurrent
        // local access. The host can report the error and safely eject.
        return -1;
    }
    taskENTER_CRITICAL(&counters_mux);
    if (write) written_bytes += size; else read_bytes += size;
    taskEXIT_CRITICAL(&counters_mux);
    if (write) last_write_ms = millis(); else last_read_ms = millis();
    return size;
}
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t size) {
    return transfer(lun, lba, offset, buffer, size, false);
}
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size) {
    return transfer(lun, lba, offset, buffer, size, true);
}

static void phase(usb_transfer_phase_t p, const char *message) {
    state_lock();
    state->usb_phase = p;
    snprintf(state->usb_status, sizeof(state->usb_status), "%s", message);
    state_unlock();
}
bool usb_transfer_busy(void) { return busy; }
void usb_transfer_request(bool enable) {
    if (enable) {
        bool expected = false;
        if (!atomic_compare_exchange_strong(&busy, &expected, true)) return;
        test_nohost = false;
        phase(USB_TRANSFER_PREPARING, "正在准备 SD 卡...");
        requested = 1;
    } else if (busy) requested = -1;
}
void usb_transfer_test_nohost(void) {
    bool expected = false;
    if (!atomic_compare_exchange_strong(&busy, &expected, true)) return;
    test_nohost = true;
    phase(USB_TRANSFER_PREPARING, "正在验证未连接超时...");
    requested = 1;
}
static void raw_close(void) {
    if (raw_host_initialized) {
        // IDF's global deinit also resets zero-filled GPIOs of unused slot 0,
        // disabling the BOOT input. Release only our initialized slot 1.
        if (raw_slot_initialized) sdmmc_host_deinit_slot(SDMMC_HOST_SLOT_1);
        else {
            sdmmc_host_deinit();
            gpio_set_direction(GPIO_NUM_0, GPIO_MODE_INPUT);
            gpio_set_pull_mode(GPIO_NUM_0, GPIO_PULLUP_ONLY);
        }
        raw_host_initialized = raw_slot_initialized = false;
    }
    free(card); card = NULL;
    free(scratch); scratch = NULL;
}
static esp_err_t raw_open(void) {
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = 10000;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1; slot.clk = SD_CLK_IO; slot.cmd = SD_CMD_IO; slot.d0 = SD_DAT0_IO;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    card = heap_caps_calloc(1, sizeof(*card), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    scratch = heap_caps_malloc(USB_DISK_SECTOR_SIZE, MALLOC_CAP_DMA);
    if (!card || !scratch) { free(card); card = NULL; free(scratch); scratch = NULL; return ESP_ERR_NO_MEM; }
    esp_err_t err = sdmmc_host_init();
    raw_host_initialized = err == ESP_OK;
    if (err == ESP_OK) err = sdmmc_host_init_slot(host.slot, &slot);
    raw_slot_initialized = err == ESP_OK;
    if (err == ESP_OK) err = sdmmc_card_init(&host, card);
    if (err == ESP_OK && (!card->csd.capacity || card->csd.sector_size != USB_DISK_SECTOR_SIZE))
        err = ESP_ERR_NOT_SUPPORTED;
    if (err != ESP_OK) { raw_close(); return err; }
    disk = (usb_disk_t){.ctx = card, .read = sector_read, .write = sector_write,
                        .sectors = card->csd.capacity};
    return ESP_OK;
}
static void usb_task(void *arg) {
    (void)arg;
    usb_phy_handle_t phy = NULL;
    if (serial_phy) { usb_del_phy(serial_phy); serial_phy = NULL; }
    usb_phy_config_t phy_config = {.controller = USB_PHY_CTRL_OTG, .target = USB_PHY_TARGET_INT,
                                   .otg_mode = USB_OTG_MODE_DEVICE};
    esp_err_t err = usb_new_phy(&phy_config, &phy);
    bool initialized = false;
    if (err == ESP_OK) {
        const tusb_rhport_init_t rh = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
        initialized = tud_rhport_init(0, &rh);
    }
    if (initialized) {
        if (test_nohost) {
            tud_disconnect();
            ESP_LOGI("usb_transfer", "diagnostic: host connection inhibited");
        }
        started_ms = millis();
        ready = true;
        phase(USB_TRANSFER_WAITING, "等待电脑连接");
        ESP_LOGI("usb_transfer", "ready sectors=%lu timeout=60s", (unsigned long)disk.sectors);
        while (exit_reason == EXIT_NONE) {
            tud_task_ext(10, false);
            if (cancel_requested && !host_seen) exit_reason = EXIT_CANCEL;
            if (!host_seen && !usb_transfer_wait_seconds(started_ms, millis(), false))
                exit_reason = EXIT_TIMEOUT;
            if (eject_acknowledged && (uint32_t)(millis() - eject_ack_ms) >= 200)
                exit_reason = EXIT_EJECT;
            // USB suspend means sleep, not cable removal. Keep the SD exclusive.
        }
        ready = false;
        tud_deinit(0); // stop IRQs, detach, and delete the USB queue/mutex
    } else {
        ESP_LOGE("usb_transfer", "USB init failed: %s", esp_err_to_name(err));
        exit_reason = EXIT_ERROR;
        if (tud_inited()) tud_deinit(0);
    }
    if (phy) usb_del_phy(phy);
    // Restore the existing USB Serial/JTAG hardware path without burning eFuses.
    phy_config = (usb_phy_config_t){.controller = USB_PHY_CTRL_SERIAL_JTAG,
                                    .target = USB_PHY_TARGET_INT};
    err = usb_new_phy(&phy_config, &serial_phy);
    if (err != ESP_OK) ESP_LOGE("usb_transfer", "USB debug restore: %s", esp_err_to_name(err));
    task_done = true;
    vTaskDelete(NULL);
}
static void restore(void) {
    phase(USB_TRANSFER_FINISHING, "正在恢复设备访问...");
    raw_close();
    esp_err_t err = ESP_OK;
    if (normal_unmounted) {
        lvgl_port_lock(0);
        err = bsp_sdcard_mount();
        state_lock();
        state->mounted = err == ESP_OK;
        state->file_count = 0;
        state->files_generation++;
        strcpy(state->directory, "/sdcard");
        state_unlock();
        lvgl_port_unlock();
        media_storage_changed();
    }
    normal_unmounted = false;
    task_created = false;
    const char *message = exit_reason == EXIT_TIMEOUT ? "60 秒未连接，已返回普通模式" :
        exit_reason == EXIT_EJECT ? "电脑已弹出，可以拔线" :
        exit_reason == EXIT_DISCONNECT ? "连接已断开，已返回普通模式" : "已返回普通模式";
    bool failed = err != ESP_OK || exit_reason == EXIT_ERROR;
    phase(failed ? USB_TRANSFER_ERROR : USB_TRANSFER_OFF,
          failed ? "恢复失败，请检查 SD 卡后重试" : message);
    state_lock();
    state->usb_wait_seconds = 0;
    state->usb_reading = state->usb_writing = state->usb_suspended = false;
    state_unlock();
    busy = false;
    if (state->mounted) terminal_submit(JOB_LIST, "/sdcard", NULL, 0);
    ESP_LOGI("usb_transfer", "normal mode reason=%d sd=%d", (int)exit_reason, state->mounted);
}
static void begin(void) {
    host_seen = suspended = cancel_requested = task_done = ready = io_error = false;
    state_lock(); state->usb_io_error = false; state_unlock();
    exit_reason = EXIT_NONE;
    last_read_ms = last_write_ms = 0;
    eject_pending = removal_prevented = eject_acknowledged = false;
    taskENTER_CRITICAL(&counters_mux); read_bytes = written_bytes = 0; taskEXIT_CRITICAL(&counters_mux);
    voice_request(false);
    voice_poll();
    media_job(&(terminal_job_t){.kind = JOB_MUSIC_TOGGLE, .value = -1});
    if (media_busy_path("/sdcard")) {
        phase(USB_TRANSFER_ERROR, "音乐文件未释放，请稍后重试"); busy = false; return;
    }
    lvgl_port_lock(0); // no image can be opening while the VFS is unmounted
    esp_err_t err = state->mounted ? bsp_sdcard_unmount() : ESP_OK;
    normal_unmounted = true;
    state_lock();
    state->mounted = false; state->file_count = 0; state->files_generation++;
    state_unlock();
    lvgl_port_unlock();
    if (err == ESP_OK) err = raw_open();
    if (err != ESP_OK) { exit_reason = EXIT_ERROR; restore(); return; }
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac); // read only; download/debug eFuses are unchanged
    snprintf(serial_number, sizeof(serial_number), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    task_created = xTaskCreatePinnedToCore(usb_task, "usb_transfer", 4096, NULL, 5, NULL, 0) == pdPASS;
    if (!task_created) { exit_reason = EXIT_ERROR; restore(); }
}
void usb_transfer_poll(void) {
    int action = atomic_exchange(&requested, 0);
    if (action == 1) begin();
    else if (action == -1 && busy) {
        if (host_seen && !task_done) terminal_notice("请先在电脑上安全弹出 SD 卡");
        else if (task_created) cancel_requested = true;
        else { busy = false; phase(USB_TRANSFER_OFF, "已取消连接"); }
    }
    if (!task_created) return;
    if (task_done) { restore(); return; }
    uint64_t r, w;
    taskENTER_CRITICAL(&counters_mux); r = read_bytes; w = written_bytes; taskEXIT_CRITICAL(&counters_mux);
    uint32_t now = millis(), last_r = last_read_ms, last_w = last_write_ms;
    state_lock();
    if (host_seen) state->usb_phase = USB_TRANSFER_CONNECTED;
    state->usb_wait_seconds = usb_transfer_wait_seconds(started_ms, now, host_seen);
    state->usb_suspended = suspended;
    state->usb_io_error = io_error;
    state->usb_writing = last_w && (uint32_t)(now - last_w) < 700;
    state->usb_reading = last_r && (uint32_t)(now - last_r) < 700;
    state->usb_read_bytes = r; state->usb_written_bytes = w;
    state_unlock();
}
void usb_transfer_diagnostics(void) {
    state_lock();
    printf("USB phase=%d busy=%d host=%d suspended=%d wait=%u read=%llu written=%llu\n",
           state->usb_phase, (int)busy, (int)host_seen, (int)suspended,
           state->usb_wait_seconds, (unsigned long long)state->usb_read_bytes,
           (unsigned long long)state->usb_written_bytes);
    state_unlock();
}
