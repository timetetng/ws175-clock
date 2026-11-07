/* Shared preferences. LVGL changes values; the existing power-key task saves
 * only after RELEASED/back + debounce. No new task, automatic erase or PMU writes. */
#include "nvs.h"
#include "settings_model.h"
static atomic_int mu_volume = SETTINGS_DEFAULT; /* Global speaker volume. */
static atomic_uint preferences_revision, preferences_saved_revision;
static atomic_uint preferences_requested_at;
static atomic_bool preferences_save_armed, preferences_save_error;
static atomic_uint preferences_battery = 255;
static bool preferences_storage_ok;

static void preferences_load(void)
{
    esp_err_t err = nvs_flash_init();
    preferences_storage_ok = err == ESP_OK;
    if (!preferences_storage_ok) {
        ESP_LOGE("settings", "NVS unavailable: %s; defaults active, storage left untouched", esp_err_to_name(err));
        return;
    }
    nvs_handle_t handle;
    err = nvs_open("ws175_ui", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        uint8_t value;
        if (nvs_get_u8(handle, "brightness", &value) == ESP_OK && value >= 5 && value <= 100)
            atomic_store(&scr_brightness, value);
        if (nvs_get_u8(handle, "volume", &value) == ESP_OK && value <= 100)
            atomic_store(&mu_volume, value);
        nvs_close(handle);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW("settings", "Preferences unreadable: %s; using defaults", esp_err_to_name(err));
    }
    ESP_LOGI("settings", "loaded: brightness=%d volume=%d", atomic_load(&scr_brightness), atomic_load(&mu_volume));
}

static void preferences_changed(void)
{
    atomic_store(&preferences_save_armed, false);
    atomic_fetch_add(&preferences_revision, 1);
}

static bool preferences_brightness(int requested)
{
    int value = settings_clamp(requested, 5);
    if (value == atomic_load(&scr_brightness)) return true;
    if (scr_on && bsp_display_brightness_set(value) != ESP_OK) return false;
    atomic_store(&scr_brightness, value);
    preferences_changed();
    return true;
}

static void preferences_volume(int requested)
{
    int value = settings_clamp(requested, 0);
    if (value != atomic_load(&mu_volume)) {
        atomic_store(&mu_volume, value);
        preferences_changed();
    }
}

static void preferences_request_save(void)
{
    atomic_store(&preferences_requested_at, (uint32_t)(esp_timer_get_time() / 1000));
    atomic_store(&preferences_save_armed, true);
}

static void preferences_release_cb(lv_event_t *e)
{
    (void)e;
    preferences_request_save();
}

/* Call only from power_key_task; don't hold the display lock over NVS writes. */
static void preferences_save_pending(void)
{
    if (!preferences_storage_ok || !atomic_load(&preferences_save_armed)) return;
    unsigned revision = atomic_load(&preferences_revision);
    if (revision == atomic_load(&preferences_saved_revision)) return;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    unsigned delay = atomic_load(&preferences_save_error) ? 1000 : 600;
    if (now - atomic_load(&preferences_requested_at) < delay) return;
    int light = atomic_load(&scr_brightness), volume = atomic_load(&mu_volume);
    nvs_handle_t handle;
    esp_err_t err = nvs_open("ws175_ui", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, "brightness", light);
        if (err == ESP_OK) err = nvs_set_u8(handle, "volume", volume);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    atomic_store(&preferences_save_error, err != ESP_OK);
    if (err == ESP_OK) {
        atomic_store(&preferences_saved_revision, revision);
        ESP_LOGI("settings", "saved: brightness=%d volume=%d", light, volume);
    } else {
        atomic_store(&preferences_requested_at, now); /* Retry after 1s. */
        ESP_LOGE("settings", "Save failed: %s", esp_err_to_name(err));
    }
}
