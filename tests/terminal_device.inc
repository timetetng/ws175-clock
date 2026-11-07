/* Visual only, no audio or genuine command execution. */
static void terminal_device_test(void)
{
    vTaskDelay(pdMS_TO_TICKS(6000));
    ESP_LOGW("terminal-test", "SILENT SELFTEST: keyboard and fake commands only");
    bool ok = true;
    unsigned dma_baseline = 0, psram_baseline = 0;
    for (int cycle = 0; cycle < 6 && ok; ++cycle) {
        bsp_display_lock(-1);
        terminal_enter();
        ok = terminal_page && terminal_history;
        if (ok) {
            ok = strstr(terminal_history, "Flash: 32 MiB") &&
                 strstr(terminal_history, "PSRAM: 8 MiB") &&
                 lv_obj_has_flag(terminal_keyboard, LV_OBJ_FLAG_HIDDEN);
            lv_obj_send_event(terminal_type_button, LV_EVENT_CLICKED, NULL);
            ok = ok && !lv_obj_has_flag(terminal_keyboard, LV_OBJ_FLAG_HIDDEN);
            lv_textarea_set_text(terminal_input, "echo keyboard $USER; reboot");
            const char * const *map = lv_keyboard_get_map_array(terminal_keyboard);
            unsigned index = 0;
            bool enter = false;
            for (unsigned i = 0; map[i][0]; ++i) {
                if (!strcmp(map[i], "\n")) continue;
                if (!strcmp(map[i], "Enter") || !strcmp(map[i], LV_SYMBOL_NEW_LINE) ||
                    !strcmp(map[i], LV_SYMBOL_OK)) {
                    lv_buttonmatrix_set_selected_button(terminal_keyboard, index);
                    lv_obj_send_event(terminal_keyboard, LV_EVENT_VALUE_CHANGED, NULL);
                    enter = true;
                    break;
                }
                ++index;
            }
            ok = ok && enter && strstr(terminal_history, "keyboard $USER; reboot\n") &&
                 lv_obj_has_flag(terminal_keyboard, LV_OBJ_FLAG_HIDDEN);
            terminal_run("clear");
            ok = ok && terminal_history[0] == '\0';
            /* Buffer-trimming stress runs in the host sanitizer test. Keep the
             * device lock short so the display task can run between cycles. */
            for (int i = 0; i < 5; ++i) terminal_run("echo hello from a pretend terminal");
            ok = ok && strlen(terminal_history) < TERMINAL_HISTORY_BYTES;
            terminal_run("help");
            ok = ok && strstr(terminal_history, "No real Unix");
            lv_obj_send_event(terminal_back_button, LV_EVENT_CLICKED, NULL);
            ok = ok && !terminal_page && !terminal_history;
        }
        bsp_display_unlock();
        vTaskDelay(pdMS_TO_TICKS(300));
        unsigned dma = mu_dma_free(), psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        if (!cycle) { dma_baseline = dma; psram_baseline = psram; }
        ok = ok && dma + 2048 >= dma_baseline && psram + 16384 >= psram_baseline;
        ESP_LOGI("terminal-test", "cycle=%d DMA=%u baseline=%u PSRAM=%u baseline=%u pass=%d",
                 cycle + 1, dma, dma_baseline, psram, psram_baseline, ok);
    }
    ESP_LOGW("terminal-test", "RESULT: %s", ok ? "PASS" : "FAIL");
}
