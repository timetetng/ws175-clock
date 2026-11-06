/* Compiled ONLY with -DMUSIC_SELFTEST=ON. Speaker remains muted in music_task. */
static bool music_test_wait(int state, int seconds)
{
    for (int i = 0; i < seconds * 20; ++i) {
        if (atomic_load(&mu_state) == state) return true;
        if (atomic_load(&mu_state) == MU_ERROR) return false;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return false;
}

static bool music_test_close(void)
{
    atomic_store(&mu_play, false);
    atomic_store(&mu_exit, true);
    for (int i = 0; i < 200; ++i) {
        bsp_display_lock(-1);
        bool closed = !music_active();
        bsp_display_unlock();
        if (closed && !atomic_load(&mu_running)) return true;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return false;
}

static bool music_test_volume(int requested, int expected)
{
    bsp_display_lock(-1);
    lv_slider_set_value(mu_volume_slider, requested, LV_ANIM_OFF);
    lv_obj_send_event(mu_volume_slider, LV_EVENT_VALUE_CHANGED, NULL);
    bsp_display_unlock();
    for (int i = 0; i < 100; ++i) {
        if (atomic_load(&mu_applied_volume) == expected) {
            ESP_LOGI("music-test", "volume request=%d applied=%d", requested, expected);
            return atomic_load(&mu_volume) == expected;
        }
        if (atomic_load(&mu_state) == MU_ERROR) return false;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;
}

static void music_device_test(void)
{
    ESP_LOGW("music-test", "SILENT SELFTEST: no speaker unmute is compiled in");
    vTaskDelay(pdMS_TO_TICKS(8000)); /* Include live WiFi/SNTP in the memory test. */
    bool ok = true;
    unsigned baseline = 0;
    for (int cycle = 0; cycle < 4 && ok; ++cycle) {
        bsp_display_lock(-1);
        music_enter();
        bsp_display_unlock();
        ok = music_test_wait(MU_PAUSED, 15);
        if (ok && cycle > 0) ok = atomic_load(&mu_volume) == 25 && atomic_load(&mu_applied_volume) == 25;
        if (ok && cycle == 0) {
            /* UI events clamp endpoints, reach the worker and don't start playback. */
            ok = music_test_volume(-10, 0) && music_test_volume(100, 100) &&
                 music_test_volume(110, 100) && music_test_volume(30, 30);
            ok = ok && atomic_load(&mu_state) == MU_PAUSED && atomic_load(&mu_sent) == 0;
            if (!ok) { music_test_close(); break; }
            int64_t start = esp_timer_get_time();
            atomic_store(&mu_play, true);
            ok = music_test_wait(MU_FINISHED, 75);
            double wall = (esp_timer_get_time() - start) / 1e6;
            double audio = (double)mu_wav.bytes / (mu_wav.rate * 2);
            ESP_LOGI("music-test", "full stream: wall=%.2fs audio=%.2fs sent=%u/%lu",
                     wall, audio, atomic_load(&mu_sent), (unsigned long)mu_wav.bytes);
            ok = ok && atomic_load(&mu_sent) == mu_wav.bytes && wall > audio * .9 && wall < audio * 1.1;
            /* Replay and then pause/resume the same stream. */
            if (ok) {
                atomic_store(&mu_play, true);
                ok = music_test_wait(MU_PLAYING, 3);
                vTaskDelay(pdMS_TO_TICKS(500));
                ok = ok && music_test_volume(0, 0) && music_test_volume(60, 60);
                atomic_store(&mu_play, false);
                ok = ok && music_test_wait(MU_PAUSED, 3);
                unsigned sent = atomic_load(&mu_sent);
                vTaskDelay(pdMS_TO_TICKS(300));
                ok = ok && sent == atomic_load(&mu_sent);
                ok = ok && music_test_volume(25, 25) && atomic_load(&mu_state) == MU_PAUSED;
                atomic_store(&mu_play, true);
                ok = ok && music_test_wait(MU_PLAYING, 3);
            }
        }
        ok = music_test_close() && ok;
        vTaskDelay(pdMS_TO_TICKS(500)); /* Idle task frees deleted worker stack. */
        unsigned available = mu_dma_free();
        if (cycle == 0) baseline = available;
        ESP_LOGI("music-test", "cycle=%d pass=%d DMA after cleanup=%u baseline=%u",
                 cycle + 1, ok, available, baseline);
        ok = ok && available + 2048 >= baseline;
    }
    ESP_LOGW("music-test", "RESULT: %s", ok ? "PASS" : "FAIL");
}
