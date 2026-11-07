/* Opt-in, silent settings test. Uses only its own NVS namespace and restores
 * the user's original values. One software reboot verifies real persistence. */
static bool settings_test_saved(void)
{
    for (int i = 0; i < 150; ++i) {
        if (atomic_load(&preferences_revision) == atomic_load(&preferences_saved_revision)) return true;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;
}

static void settings_device_test(void)
{
    vTaskDelay(pdMS_TO_TICKS(6000));
    ESP_LOGW("settings-test", "SILENT SELFTEST: no audio; brightness stays at or below 30%%");
    nvs_handle_t handle;
    if (nvs_open("ws175_ui", NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGE("settings-test", "RESULT: FAIL (storage unavailable)");
        return;
    }
    uint8_t phase = 0, original_light = atomic_load(&scr_brightness), original_volume = atomic_load(&mu_volume);
    nvs_get_u8(handle, "test_phase", &phase);
    bool ok = true;
    if (phase == 1) {
        nvs_get_u8(handle, "test_light", &original_light);
        nvs_get_u8(handle, "test_volume", &original_volume);
        ok = atomic_load(&scr_brightness) == 20 && atomic_load(&mu_volume) == 15 &&
             bsp_display_brightness_get() == 20;
        ESP_LOGI("settings-test", "after reboot: brightness=%d volume=%d pass=%d",
                 atomic_load(&scr_brightness), atomic_load(&mu_volume), ok);
    } else {
        ok = nvs_set_u8(handle, "test_light", original_light) == ESP_OK &&
             nvs_set_u8(handle, "test_volume", original_volume) == ESP_OK &&
             nvs_commit(handle) == ESP_OK;
        unsigned baseline = 0;
        for (int i = 0; i < 6 && ok; ++i) {
            bsp_display_lock(-1);
            settings_enter();
            ok = settings_page != NULL;
            if (ok) {
                if (i == 0) {
                    lv_slider_set_value(settings_light_slider, 20, LV_ANIM_OFF);
                    lv_obj_send_event(settings_light_slider, LV_EVENT_VALUE_CHANGED, NULL);
                    lv_slider_set_value(settings_volume_slider, 15, LV_ANIM_OFF);
                    lv_obj_send_event(settings_volume_slider, LV_EVENT_VALUE_CHANGED, NULL);
                    ok = atomic_load(&scr_brightness) == 20 && atomic_load(&mu_volume) == 15;
                    lv_obj_send_event(settings_volume_slider, LV_EVENT_RELEASED, NULL);
                    screen_apply(false);
                    ok = ok && !scr_on && bsp_display_brightness_get() == 0;
                    screen_apply(true);
                    ok = ok && scr_on && bsp_display_brightness_get() == 20;
                }
                lv_obj_send_event(settings_back_button, LV_EVENT_CLICKED, NULL);
                ok = ok && !settings_page;
            }
            bsp_display_unlock();
            ok = settings_test_saved() && ok;
            vTaskDelay(pdMS_TO_TICKS(150));
            unsigned free_dma = mu_dma_free();
            if (!i) baseline = free_dma;
            ok = ok && free_dma + 2048 >= baseline;
            ESP_LOGI("settings-test", "cycle=%d DMA=%u baseline=%u pass=%d", i + 1, free_dma, baseline, ok);
        }
        unsigned battery = atomic_load(&preferences_battery);
        ESP_LOGI("settings-test", "battery: valid=%d connected=%d usb=%d charging=%d percent=%u",
                 !!(battery & BAT_VALID), !!(battery & BAT_CONNECTED), !!(battery & BAT_USB),
                 !!(battery & BAT_CHARGING), battery & 255);
        ok = ok && (battery & BAT_VALID);
        if (ok) {
            ok = nvs_set_u8(handle, "test_phase", 1) == ESP_OK && nvs_commit(handle) == ESP_OK;
            if (ok) {
                nvs_close(handle);
                ESP_LOGW("settings-test", "Rebooting to verify saved settings");
                vTaskDelay(pdMS_TO_TICKS(300));
                esp_restart();
            }
        }
    }
    bsp_display_lock(-1);
    if (settings_page) settings_back_cb(NULL);
    ok = preferences_brightness(original_light) && ok;
    preferences_volume(original_volume);
    preferences_request_save();
    bsp_display_unlock();
    ok = settings_test_saved() && ok;
    nvs_erase_key(handle, "test_phase");
    nvs_erase_key(handle, "test_light");
    nvs_erase_key(handle, "test_volume");
    ok = nvs_commit(handle) == ESP_OK && ok;
    nvs_close(handle);
    ESP_LOGW("settings-test", "RESULT: %s", ok ? "PASS" : "FAIL");
}
