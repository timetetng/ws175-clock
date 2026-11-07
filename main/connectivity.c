/* Native ESP-IDF Wi-Fi scan and NimBLE observer lifecycle, with LVGL kept on
 * the GUI thread. References: examples/wifi/scan and examples/bluetooth/nimble/blecent.
 * Never erase NVS, print credentials, pair, advertise or execute remote code. */
#include "connectivity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "host/util/util.h"

#define NET_DOWN BIT0
#define NET_BLE_DONE BIT1
#define NET_HOST_EXIT BIT2
typedef enum { JOB_WIFI_SCAN, JOB_BLE_SCAN, JOB_CONNECT, JOB_DISCONNECT } net_job_t;
static portMUX_TYPE net_lock = portMUX_INITIALIZER_UNLOCKED;
static net_snapshot_t *net_data;
static EventGroupHandle_t net_events;
static atomic_bool net_busy, net_cancel, net_autoretry = true, net_save_pending;
static atomic_uint net_retries;
static atomic_int net_ble_error;
static atomic_bool net_ble_broken, net_ble_ready;
static net_job_t net_job;
static net_device_t net_selected;
static char net_password[65];

static void net_status(const char *message)
{
    if (!net_data) return;
    portENTER_CRITICAL(&net_lock);
    snprintf(net_data->status, sizeof(net_data->status), "%s", message);
    ++net_data->revision;
    portEXIT_CRITICAL(&net_lock);
}
static void net_error(const char *prefix, int code)
{
    char message[96];
    snprintf(message, sizeof(message), "%s (%d)", prefix, code);
    net_status(message);
}
void connectivity_wifi_error(const char *stage, int code)
{
    if (net_data) {
        portENTER_CRITICAL(&net_lock);
        net_data->mode = 1;
        portEXIT_CRITICAL(&net_lock);
    }
    net_error(stage, code);
}
bool connectivity_init(void)
{
    if (net_data) return true; /* Called only on boot / LVGL thread. */
    net_snapshot_t *data = heap_caps_calloc(1, sizeof(*data), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!data) return false;
    net_events = xEventGroupCreate();
    if (!net_events) { free(data); return false; }
    net_data = data;
    net_status("Tap SCAN to find devices");
    return true;
}
void connectivity_snapshot(net_snapshot_t *out)
{
    memset(out, 0, sizeof(*out));
    if (net_data) {
        portENTER_CRITICAL(&net_lock);
        *out = *net_data;
        portEXIT_CRITICAL(&net_lock);
    }
    out->busy = atomic_load(&net_busy);
}
void connectivity_wifi_down(void)
{
    if (net_events) xEventGroupSetBits(net_events, NET_DOWN);
    if (net_data) {
        portENTER_CRITICAL(&net_lock);
        net_data->wifi_up = false;
        net_data->ip[0] = 0;
        ++net_data->revision;
        portEXIT_CRITICAL(&net_lock);
    }
}
bool connectivity_wifi_retry(void)
{
    if (!atomic_load(&net_autoretry)) return false;
    if (atomic_load(&net_save_pending) && atomic_fetch_add(&net_retries, 1) >= 3) {
        atomic_store(&net_save_pending, false);
        atomic_store(&net_autoretry, false);
        net_status("Connection failed / check password");
        return false;
    }
    return true;
}
void connectivity_wifi_up(const char *ip)
{
    wifi_ap_record_t ap = {0};
    esp_wifi_sta_get_ap_info(&ap);
    if (net_data) {
        portENTER_CRITICAL(&net_lock);
        net_data->wifi_up = true;
        net_display_name(net_data->wifi_name, ap.ssid, 32);
        snprintf(net_data->ip, sizeof(net_data->ip), "%s", ip);
        ++net_data->revision;
        portEXIT_CRITICAL(&net_lock);
    }
    if (atomic_load(&net_save_pending)) {
        wifi_config_t config = {0};
        esp_err_t e = esp_wifi_get_config(WIFI_IF_STA, &config);
        if (e == ESP_OK) e = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
        if (e == ESP_OK) e = esp_wifi_set_config(WIFI_IF_STA, &config);
        net_secret_clear(&config, sizeof(config));
        net_status(e == ESP_OK ? "Connected / saved on device" : "Connected / profile save failed");
        atomic_store(&net_save_pending, false);
    }
}
static void net_wifi_scan(void)
{
    wifi_ap_record_t *aps = heap_caps_calloc(NET_MAX_RESULTS, sizeof(*aps),
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!aps) { net_status("Not enough memory for Wi-Fi scan"); return; }
    /* The bounded blocking driver call runs here, never on the display thread.
     * Leaving the page discards results after completion (no GUI pointer retained). */
    esp_err_t e = esp_wifi_scan_start(NULL, true);
    uint16_t count = NET_MAX_RESULTS;
    if (e == ESP_OK) e = esp_wifi_scan_get_ap_records(&count, aps);
    if (e == ESP_OK && !atomic_load(&net_cancel)) {
        portENTER_CRITICAL(&net_lock);
        for (unsigned i = 0; i < count; ++i) {
            net_device_t device = {.rssi = aps[i].rssi, .auth = aps[i].authmode};
            memcpy(device.address, aps[i].bssid, 6);
            memcpy(device.ssid, aps[i].ssid, 32);
            net_display_name(device.name, aps[i].ssid, 32);
            net_result_add(net_data, &device);
        }
        portEXIT_CRITICAL(&net_lock);
        net_status(count ? "Select a network to connect" : "No visible 2.4 GHz networks");
    } else if (atomic_load(&net_cancel)) net_status("Scan closed");
    else if (e == ESP_ERR_WIFI_STATE) net_status("Wi-Fi busy / DISCONNECT, then SCAN");
    else net_error("Wi-Fi scan failed", e);
    esp_wifi_clear_ap_list();
    free(aps);
}
static bool net_station_disconnect(void)
{
    atomic_store(&net_autoretry, false);
    atomic_store(&net_save_pending, false);
    xEventGroupClearBits(net_events, NET_DOWN);
    esp_err_t e = esp_wifi_disconnect();
    if (e == ESP_ERR_WIFI_NOT_CONNECT) return true;
    if (e != ESP_OK) { net_error("Disconnect failed", e); return false; }
    if (!(xEventGroupWaitBits(net_events, NET_DOWN, pdTRUE, pdFALSE, pdMS_TO_TICKS(3000)) & NET_DOWN)) {
        net_status("Disconnect timed out / try again");
        return false;
    }
    return true;
}
static void net_station_connect(void)
{
    wifi_auth_mode_t auth = (wifi_auth_mode_t)net_selected.auth;
    if (auth != WIFI_AUTH_OPEN && auth != WIFI_AUTH_WPA_PSK && auth != WIFI_AUTH_WPA2_PSK &&
        auth != WIFI_AUTH_WPA_WPA2_PSK && auth != WIFI_AUTH_WPA3_PSK && auth != WIFI_AUTH_WPA2_WPA3_PSK) {
        net_status("Enterprise/WEP networks unsupported");
        return;
    }
    if (!net_station_disconnect()) return;
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, net_selected.ssid, 32);
    memcpy(config.sta.password, net_password, strlen(net_password));
    config.sta.threshold.authmode = auth == WIFI_AUTH_WPA_WPA2_PSK ? WIFI_AUTH_WPA_PSK :
                                    auth == WIFI_AUTH_WPA2_WPA3_PSK ? WIFI_AUTH_WPA2_PSK : auth;
    config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    esp_err_t e = esp_wifi_set_storage(WIFI_STORAGE_RAM); /* Persist only after GOT_IP. */
    if (e == ESP_OK) e = esp_wifi_set_config(WIFI_IF_STA, &config);
    net_secret_clear(&config, sizeof(config));
    if (e == ESP_OK) {
        atomic_store(&net_retries, 0);
        atomic_store(&net_save_pending, true);
        atomic_store(&net_autoretry, true);
        net_status("Connecting... (profile saved on success)");
        e = esp_wifi_connect();
    }
    if (e != ESP_OK) {
        atomic_store(&net_save_pending, false);
        atomic_store(&net_autoretry, false);
        net_error("Connection request failed", e);
    } else {
        int64_t deadline = esp_timer_get_time() + 25000000;
        while (atomic_load(&net_save_pending) && esp_timer_get_time() < deadline)
            vTaskDelay(pdMS_TO_TICKS(100));
        if (atomic_exchange(&net_save_pending, false)) {
            atomic_store(&net_autoretry, false);
            esp_wifi_disconnect();
            net_status("Connection timed out / saved profile unchanged");
        }
    }
}
static int net_ble_gap(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type == BLE_GAP_EVENT_DISC && !atomic_load(&net_cancel)) {
        const struct ble_gap_disc_desc *disc = &event->disc;
        net_device_t device = {.rssi = disc->rssi, .address_type = disc->addr.type};
        memcpy(device.address, disc->addr.val, 6);
        struct ble_hs_adv_fields fields;
        if (ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) == 0 && fields.name)
            net_display_name(device.name, fields.name, fields.name_len);
        portENTER_CRITICAL(&net_lock);
        net_result_add(net_data, &device);
        portEXIT_CRITICAL(&net_lock);
    } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) xEventGroupSetBits(net_events, NET_BLE_DONE);
    return 0;
}
static void net_ble_sync(void)
{
    atomic_store(&net_ble_ready, true);
    if (atomic_load(&net_cancel)) {
        xEventGroupSetBits(net_events, NET_BLE_DONE);
        return;
    }
    uint8_t own_addr_type = 0;
    int rc = ble_hs_util_ensure_addr(0);
    if (!rc) rc = ble_hs_id_infer_auto(0, &own_addr_type);
    struct ble_gap_disc_params params = {.passive = 1, .filter_duplicates = 1};
    if (!rc) rc = ble_gap_disc(own_addr_type, 5000, &params, net_ble_gap, NULL);
    atomic_store(&net_ble_error, rc);
    if (rc) xEventGroupSetBits(net_events, NET_BLE_DONE);
}
static void net_ble_reset(int reason)
{
    atomic_store(&net_ble_error, reason);
    xEventGroupSetBits(net_events, NET_BLE_DONE);
}
static void net_ble_host(void *arg)
{
    (void)arg;
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    nimble_port_run();
    xEventGroupSetBits(net_events, NET_HOST_EXIT);
    vTaskSuspend(NULL); /* Worker owns deletion; no stack use after deinit. */
}
static void net_ble_scan(void)
{
    TaskHandle_t host = NULL;
    xEventGroupClearBits(net_events, NET_BLE_DONE | NET_HOST_EXIT);
    atomic_store(&net_ble_error, 0);
    atomic_store(&net_ble_ready, false);
    /* The IDF helper silently ignores task-create failure. Check it ourselves. */
    if (xTaskCreatePinnedToCore(net_ble_host, "ble-observer", 4096, NULL,
                               configMAX_PRIORITIES - 4, &host, 0) != pdPASS) {
        net_status("Not enough internal RAM for BLE task");
        return;
    }
    esp_err_t e = nimble_port_init();
    if (e != ESP_OK) {
        vTaskDelete(host);
        net_error("BLE init failed (internal RAM may be low)", e);
        return;
    }
    ble_hs_cfg.sync_cb = net_ble_sync;
    ble_hs_cfg.reset_cb = net_ble_reset;
    xTaskNotifyGive(host);
    int64_t deadline = esp_timer_get_time() + 10000000;
    bool done = false;
    while (esp_timer_get_time() < deadline) {
        if (atomic_load(&net_cancel) && atomic_load(&net_ble_ready)) break;
        if (xEventGroupWaitBits(net_events, NET_BLE_DONE, pdTRUE, pdFALSE, pdMS_TO_TICKS(100)) & NET_BLE_DONE) {
            done = true; break;
        }
    }
    /* Official init/deinit sequence; host must stop before controller deinit. */
    int stop = nimble_port_stop();
    if (!stop) {
        xEventGroupWaitBits(net_events, NET_HOST_EXIT, pdTRUE, pdFALSE, portMAX_DELAY);
        vTaskDelete(host);
        e = nimble_port_deinit();
    }
    if (stop || e != ESP_OK) {
        atomic_store(&net_ble_broken, true);
        net_error("BLE shutdown failed; restart before retry", stop ? stop : e);
    }
    else if (atomic_load(&net_ble_error)) net_error("BLE scan failed", atomic_load(&net_ble_error));
    else if (atomic_load(&net_cancel)) net_status("BLE scan closed / radio released");
    else net_status(done ? "BLE scan complete / radio released" : "BLE sync timed out / radio released");
}
static void net_worker(void *arg)
{
    (void)arg;
    switch (net_job) {
    case JOB_WIFI_SCAN: net_wifi_scan(); break;
    case JOB_BLE_SCAN: net_ble_scan(); break;
    case JOB_CONNECT: net_station_connect(); break;
    case JOB_DISCONNECT:
        if (net_station_disconnect()) net_status("Wi-Fi disconnected");
        break;
    }
    net_secret_clear(net_password, sizeof(net_password));
    atomic_store(&net_busy, false);
    vTaskDelete(NULL);
}
static bool net_launch(net_job_t job)
{
    net_job = job;
    atomic_store(&net_cancel, false);
    if (xTaskCreate(net_worker, "connectivity", 4096, NULL, 2, NULL) != pdPASS) {
        net_secret_clear(net_password, sizeof(net_password));
        atomic_store(&net_busy, false);
        net_status("Not enough RAM for network task");
        return false;
    }
    return true;
}
static bool net_reserve(void)
{
    bool expected = false;
    return net_data && atomic_compare_exchange_strong(&net_busy, &expected, true);
}
bool connectivity_scan(bool ble)
{
    if (ble && atomic_load(&net_ble_broken)) {
        net_status("Restart before scanning BLE again");
        return false;
    }
    if (!net_reserve()) return false;
    portENTER_CRITICAL(&net_lock);
    net_data->mode = ble ? 2 : 1;
    net_data->count = 0;
    memset(net_data->devices, 0, sizeof(net_data->devices));
    ++net_data->revision;
    portEXIT_CRITICAL(&net_lock);
    net_status(ble ? "Scanning BLE for 5 seconds..." : "Scanning 2.4 GHz Wi-Fi...");
    return net_launch(ble ? JOB_BLE_SCAN : JOB_WIFI_SCAN);
}
bool connectivity_connect(unsigned index, const char *password)
{
    if (!net_reserve()) return false;
    bool valid;
    portENTER_CRITICAL(&net_lock);
    valid = net_data->mode == 1 && index < net_data->count;
    if (valid) net_selected = net_data->devices[index];
    portEXIT_CRITICAL(&net_lock);
    if (!valid || !net_password_valid(password, net_selected.auth == WIFI_AUTH_OPEN)) {
        net_status("Password needs 8-63 chars or 64 hex digits");
        atomic_store(&net_busy, false);
        return false;
    }
    snprintf(net_password, sizeof(net_password), "%s", password);
    net_status("Switching network...");
    return net_launch(JOB_CONNECT);
}
bool connectivity_disconnect(void)
{
    if (!net_reserve()) return false;
    return net_launch(JOB_DISCONNECT);
}
void connectivity_cancel_scan(void)
{
    /* Connect attempts intentionally survive closing the page; scans do not. */
    atomic_store(&net_cancel, true);
}
