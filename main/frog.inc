/* A single-shot GIF toy with click-triggered audio. UI uses the locked LVGL task.
 * Artwork and voice live in the app image; audio reuses the music worker.
 * READY only pauses/hides the widget: deleting it inside LVGL's GIF callback is unsafe. */
extern const uint8_t frog_gif_start[] asm("_binary_frog_laugh_gif_start");
extern const uint8_t frog_gif_end[] asm("_binary_frog_laugh_gif_end");
extern const uint8_t frog_idle_start[] asm("_binary_frog_idle_rgb565_start");

#define FROG_WIDTH 280
#define FROG_HEIGHT 392
static lv_image_dsc_t frog_gif_source = {
    .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RAW,
                .w = FROG_WIDTH, .h = FROG_HEIGHT },
    .data = frog_gif_start,
};
static const lv_image_dsc_t frog_idle_source = {
    .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
                .w = FROG_WIDTH, .h = FROG_HEIGHT, .stride = FROG_WIDTH * 2 },
    .data_size = FROG_WIDTH * FROG_HEIGHT * 2,
    .data = frog_idle_start,
};
static lv_obj_t *frog_page, *frog_gif, *frog_idle, *frog_hint;
static lv_obj_t *frog_reset_button, *frog_back_button;
static bool frog_playing;
static uint32_t frog_started_at;

static bool frog_active(void)
{
    return frog_page != NULL;
}

static void frog_reset(void)
{
    if (!frog_page) return;
    atomic_store(&mu_play, false);
    atomic_store(&mu_rewind, true);
    lv_gif_pause(frog_gif);
    lv_obj_add_flag(frog_gif, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(frog_idle, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(frog_hint, "");
    frog_playing = false;
    scr_last_activity = lv_tick_get();
}

static void frog_play_cb(lv_event_t *e)
{
    (void)e;
    if (!frog_page || !scr_on || frog_playing) return;
    /* Reopen to draw frame zero immediately, including after a mid-animation reset.
     * Merely restarting the timer would briefly expose the previous frame. */
    lv_gif_set_src(frog_gif, &frog_gif_source);
    if (!lv_gif_is_loaded(frog_gif)) {
        lv_gif_pause(frog_gif);
        lv_label_set_text(frog_hint, "ANIMATION UNAVAILABLE");
        ESP_LOGE("frog", "GIF could not be loaded");
        return;
    }
    lv_gif_set_loop_count(frog_gif, 1);
    lv_obj_add_flag(frog_idle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(frog_gif, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(frog_hint, atomic_load(&mu_state) == MU_ERROR ? "AUDIO UNAVAILABLE" : "");
    /* Worker rewinds before its next write, including rapid RESET -> PLAY. */
    atomic_store(&mu_rewind, true);
    atomic_store(&mu_play, true);
    frog_playing = true;
    frog_started_at = lv_tick_get();
    scr_last_activity = frog_started_at;
    ESP_LOGI("frog", "playing: PSRAM free=%u DMA free=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
}

static void frog_ready_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI("frog", "completed in %lu ms", (unsigned long)(lv_tick_get() - frog_started_at));
    frog_reset();
}

static void frog_reset_cb(lv_event_t *e)
{
    (void)e;
    if (scr_on) frog_reset();
}

static void frog_back_cb(lv_event_t *e)
{
    (void)e;
    if (!scr_on || !frog_page) return;
    atomic_store(&mu_play, false);
    atomic_store(&mu_exit, true);
    lv_gif_pause(frog_gif);
    lv_obj_delete(frog_page); /* Also destroys the decoder, framebuffer and GIF timer. */
    frog_page = frog_gif = frog_idle = frog_hint = NULL;
    frog_reset_button = frog_back_button = NULL;
    frog_playing = false;
    scr_last_activity = lv_tick_get();
    ESP_LOGI("frog", "closed: PSRAM free=%u DMA free=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
}

static lv_obj_t *frog_label(const char *text, int y, uint32_t color)
{
    lv_obj_t *label = lv_label_create(frog_page);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

static lv_obj_t *frog_button(const char *text, int x, int y, int w, lv_event_cb_t cb)
{
    lv_obj_t *button = lv_button_create(frog_page);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, w, 36);
    lv_obj_align(button, LV_ALIGN_TOP_MID, x, y);
    lv_obj_set_style_radius(button, 18, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xF8D060), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xE6B94C), LV_STATE_PRESSED);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x1A1612), 0);
    lv_obj_center(label);
    return button;
}

static void frog_enter(void)
{
    if (frog_page || music_active()) return;
    frog_gif_source.data_size = (uint32_t)(frog_gif_end - frog_gif_start);
    frog_playing = false;
    frog_page = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(frog_page);
    lv_obj_set_size(frog_page, SCREEN_W, SCREEN_W);
    lv_obj_set_style_bg_color(frog_page, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(frog_page, LV_OPA_COVER, 0);
    lv_obj_remove_flag(frog_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(frog_page, LV_OBJ_FLAG_CLICKABLE); /* Modal; don't pass taps to the menu. */

    frog_gif = lv_gif_create(frog_page);
    lv_gif_set_color_format(frog_gif, LV_COLOR_FORMAT_RGB565);
    lv_obj_align(frog_gif, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_add_flag(frog_gif, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(frog_gif, frog_play_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(frog_gif, frog_ready_cb, LV_EVENT_READY, NULL);
    frog_idle = lv_image_create(frog_page);
    lv_image_set_src(frog_idle, &frog_idle_source);
    lv_obj_align(frog_idle, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_add_flag(frog_idle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(frog_idle, frog_play_cb, LV_EVENT_CLICKED, NULL);

    /* No heading or instruction text; reserve the display for the frog.
     * This empty label is only populated if the GIF cannot be decoded. */
    frog_hint = frog_label("", 76, 0xB74329);
    frog_back_button = frog_button(LV_SYMBOL_LEFT, -110, 26, 36, frog_back_cb);
    frog_reset_button = frog_button(LV_SYMBOL_REFRESH "  RESET", 0, 414, 116, frog_reset_cb);
    scr_last_activity = lv_tick_get();
    music_session_start(true); /* Preload muted; only a frog tap requests playback. */
    ESP_LOGI("frog", "entered idle (audio muted)");
}
