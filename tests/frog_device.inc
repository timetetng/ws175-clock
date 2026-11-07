/* Opt-in visual test (-DFROG_SELFTEST=ON). The shared audio worker stays muted. */
static bool frog_device_cycle(bool full)
{
    bsp_display_lock(-1);
    frog_enter();
    bool ok = frog_page && !frog_playing && !lv_obj_has_flag(frog_idle, LV_OBJ_FLAG_HIDDEN);
    if (ok) {
        lv_obj_send_event(frog_idle, LV_EVENT_CLICKED, NULL);
        ok = frog_playing && lv_gif_is_loaded(frog_gif);
    }
    bsp_display_unlock();
    vTaskDelay(pdMS_TO_TICKS(500));
    bsp_display_lock(-1);
    ok = ok && lv_gif_get_current_frame_index(frog_gif) > 1;
    lv_obj_send_event(frog_reset_button, LV_EVENT_CLICKED, NULL);
    int32_t paused = lv_gif_get_current_frame_index(frog_gif);
    ok = ok && !frog_playing && !lv_obj_has_flag(frog_idle, LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();
    vTaskDelay(pdMS_TO_TICKS(300));
    bsp_display_lock(-1);
    ok = ok && paused == lv_gif_get_current_frame_index(frog_gif);
    if (ok) {
        lv_obj_send_event(frog_idle, LV_EVENT_CLICKED, NULL);
        uint32_t started = frog_started_at;
        lv_obj_send_event(frog_gif, LV_EVENT_CLICKED, NULL); /* Don't restart a running clip. */
        ok = frog_playing && started == frog_started_at && lv_gif_get_loop_count(frog_gif) == 1;
    }
    bsp_display_unlock();
    if (ok && full) {
        uint32_t waited = 0;
        int32_t max_frame = 0;
        while (waited < 26000) {
            vTaskDelay(pdMS_TO_TICKS(50));
            waited += 50;
            bsp_display_lock(-1);
            bool playing = frog_playing;
            int32_t frame = lv_gif_get_current_frame_index(frog_gif);
            if (frame > max_frame) max_frame = frame;
            bsp_display_unlock();
            if (!playing) break;
        }
        bsp_display_lock(-1);
        uint32_t elapsed = lv_tick_get() - frog_started_at;
        ok = !frog_playing && max_frame >= 200 && elapsed >= 16500 && elapsed < 24000;
        ok = ok && !lv_obj_has_flag(frog_idle, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI("frog-test", "complete: elapsed=%lu ms last_frame=%ld pass=%d",
                 (unsigned long)elapsed, (long)max_frame, ok);
        bsp_display_unlock();
    } else {
        vTaskDelay(pdMS_TO_TICKS(300)); /* BACK during playback must free its active timer. */
    }
    bsp_display_lock(-1);
    lv_obj_send_event(frog_back_button, LV_EVENT_CLICKED, NULL);
    ok = ok && !frog_active() && !frog_playing;
    bsp_display_unlock();
    vTaskDelay(pdMS_TO_TICKS(300)); /* A deleted timer must not fire after page teardown. */
    return ok;
}

static void frog_device_test(void)
{
    ESP_LOGW("frog-test", "VISUAL SELFTEST: frog animates automatically, no sound");
    vTaskDelay(pdMS_TO_TICKS(6000));
    bool ok = true;
    unsigned dma_baseline = 0, psram_baseline = 0;
    for (int cycle = 0; cycle < 4 && ok; ++cycle) {
        ok = frog_device_cycle(cycle == 0);
        unsigned dma = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        unsigned psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        if (!cycle) { dma_baseline = dma; psram_baseline = psram; }
        ok = ok && dma + 2048 >= dma_baseline && psram + 16384 >= psram_baseline;
        ESP_LOGI("frog-test", "cycle=%d pass=%d DMA=%u baseline=%u PSRAM=%u baseline=%u",
                 cycle + 1, ok, dma, dma_baseline, psram, psram_baseline);
    }
    ESP_LOGW("frog-test", "RESULT: %s", ok ? "PASS" : "FAIL");
}
