#include "core/terminal.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include <stdio.h>
#include <string.h>
static esp_gatt_if_t client = ESP_GATT_IF_NONE;
static uint16_t connection;
static bool linked, ready;
static void status(const char *s) {
    state_lock();
    snprintf(state->ble_status, sizeof(state->ble_status), "%s", s);
    state_unlock();
}
static void gap(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *p) {
    if (event == ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT) {
        if (p->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS)
            esp_ble_gap_start_scanning(8);
        else
            status("扫描配置失败");
    } else if (event == ESP_GAP_BLE_SCAN_RESULT_EVT) {
        if (p->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT) {
            status("扫描完成，点击设备配对");
            return;
        }
        if (p->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT)
            return;
        state_lock();
        int i;
        for (i = 0; i < state->ble_count; i++)
            if (!memcmp(state->devices[i].address, p->scan_rst.bda, 6))
                break;
        if (i < BLE_LIMIT) {
            if (i == state->ble_count)
                state->ble_count++;
            ble_entry_t *d = &state->devices[i];
            memcpy(d->address, p->scan_rst.bda, 6);
            d->address_type = p->scan_rst.ble_addr_type;
            d->rssi = p->scan_rst.rssi;
            uint8_t len = 0;
            uint8_t *name =
                esp_ble_resolve_adv_data(p->scan_rst.ble_adv, ESP_BLE_AD_TYPE_NAME_CMPL, &len);
            if (name)
                snprintf(d->name, sizeof(d->name), "%.*s", len, name);
            else
                snprintf(d->name, sizeof(d->name), "%02X:%02X:%02X:%02X:%02X:%02X", d->address[0],
                         d->address[1], d->address[2], d->address[3], d->address[4], d->address[5]);
            state->ble_generation++;
        }
        state_unlock();
    } else if (event == ESP_GAP_BLE_SEC_REQ_EVT) {
        esp_ble_gap_security_rsp(p->ble_security.ble_req.bd_addr, linked);
    } else if (event == ESP_GAP_BLE_AUTH_CMPL_EVT) {
        char s[120];
        snprintf(s, sizeof(s),
                 p->ble_security.auth_cmpl.success ? "配对成功，已保存绑定"
                                                   : "配对失败，原因 0x%02x",
                 p->ble_security.auth_cmpl.fail_reason);
        status(s);
    } else if (event == ESP_GAP_BLE_PASSKEY_REQ_EVT) {
        esp_ble_passkey_reply(p->ble_security.ble_req.bd_addr, false, 0);
        status("对方要求密码输入，首版不支持此配对方式");
    } else if (event == ESP_GAP_BLE_NC_REQ_EVT) {
        esp_ble_confirm_reply(p->ble_security.key_notif.bd_addr, false);
        status("对方要求数字确认，无法使用免输入配对");
    }
}
static void gatt(esp_gattc_cb_event_t event, esp_gatt_if_t interface, esp_ble_gattc_cb_param_t *p) {
    if (event == ESP_GATTC_REG_EVT) {
        client = interface;
        ready = p->reg.status == ESP_GATT_OK;
    } else if (event == ESP_GATTC_OPEN_EVT) {
        if (p->open.status != ESP_GATT_OK) {
            linked = false;
            status("连接失败：对方可能不接受连接");
            return;
        }
        connection = p->open.conn_id;
        linked = true;
        status("已连接，正在协商配对");
        esp_err_t e = esp_ble_set_encryption(p->open.remote_bda, ESP_BLE_SEC_ENCRYPT);
        if (e != ESP_OK)
            status("无法发起配对");
    } else if (event == ESP_GATTC_DISCONNECT_EVT) {
        linked = false;
        status("BLE 连接已断开（绑定记录保留）");
    }
}
void ble_init(void) {
    esp_bt_controller_config_t c = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if (esp_bt_controller_init(&c) != ESP_OK ||
        esp_bt_controller_enable(ESP_BT_MODE_BLE) != ESP_OK || esp_bluedroid_init() != ESP_OK ||
        esp_bluedroid_enable() != ESP_OK) {
        status("BLE 初始化失败");
        return;
    }
    esp_ble_gap_register_callback(gap);
    esp_ble_gattc_register_callback(gatt);
    esp_ble_gattc_app_register(0);
    esp_ble_auth_req_t auth = ESP_LE_AUTH_BOND;
    esp_ble_io_cap_t io = ESP_IO_CAP_NONE;
    uint8_t size = 16;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth, sizeof(auth));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &io, sizeof(io));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &size, sizeof(size));
    status("扫描附近 BLE 设备");
}
void ble_job(const terminal_job_t *j) {
    if (!ready) {
        status("BLE 尚未就绪");
        return;
    }
    if (j->kind == JOB_BLE_SCAN) {
        state_lock();
        state->ble_count = 0;
        state->ble_generation++;
        state_unlock();
        static esp_ble_scan_params_t p = {.scan_type = BLE_SCAN_TYPE_ACTIVE,
                                          .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
                                          .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
                                          .scan_interval = 0x50,
                                          .scan_window = 0x30,
                                          .scan_duplicate = BLE_SCAN_DUPLICATE_ENABLE};
        status("正在扫描…");
        esp_ble_gap_set_scan_params(&p);
    } else {
        ble_entry_t d;
        state_lock();
        bool valid = j->value >= 0 && j->value < state->ble_count;
        if (valid)
            d = state->devices[j->value];
        state_unlock();
        if (!valid)
            return;
        esp_ble_gap_stop_scanning();
        if (linked) {
            esp_ble_gattc_close(client, connection);
            linked = false;
        }
        status("正在连接并配对…");
        if (esp_ble_gattc_open(client, d.address, d.address_type, true) != ESP_OK)
            status("发起连接失败");
    }
}
