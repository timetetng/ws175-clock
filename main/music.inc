/* Music is a modal app over the existing clock/menu. UI is LVGL-task-only;
 * one worker owns the file, codec and TX channel. BACK requests cooperative
 * shutdown; the UI waits for cleanup before allowing another session. */
#include <stdatomic.h>
#include "esp_spiffs.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev.h"
#include "driver/i2s_std.h"
#include "music_wav.h"

#define MUSIC_CHUNK 2048
static const char *MUSIC_TAG = "music";
/* Artwork is an optional LVGL image descriptor in flash. NULL uses the vinyl cover. */
static const struct {
    const char *path, *title, *artist;
    const lv_image_dsc_t *cover;
} mu_tracks[] = {
    { "/spiffs/demo60.wav", "RAIN LOVE", "Si Feng version", NULL },
    { "/spiffs/gondboy.wav", "GONDBOY", "Old priest", NULL },
};
static unsigned mu_track_index;
#define mu_track mu_tracks[mu_track_index]
static lv_obj_t *mu_title;
static atomic_int mu_source_request = -1, sfx_requested = -1;
static atomic_bool mu_session_closed = true, sfx_playing;
static atomic_uint mu_total_bytes, mu_sample_rate = 22050;
typedef enum { MU_LOADING, MU_PAUSED, MU_PLAYING, MU_FINISHED, MU_ERROR } mu_state_t;
static lv_obj_t *music_page, *mu_time, *mu_status, *mu_info, *mu_button_text, *mu_bar, *mu_artist;
static atomic_int mu_state;
static atomic_int mu_applied_volume = -1;
static lv_obj_t *mu_volume_slider, *mu_volume_text;
static atomic_bool mu_running, mu_exit, mu_play, mu_rewind;
/* Published before starting the sole audio worker; immutable until it exits. */
/* Audio sources are opened only by the single persistent worker. */
extern const uint8_t frog_voice_start[] asm("_binary_frog_voice_wav_start");
extern const uint8_t frog_voice_end[] asm("_binary_frog_voice_wav_end");
static atomic_uint mu_sent;
/* Published before state changes to PAUSED/ERROR; not changed during a session. */
static music_wav_t mu_wav;
static char mu_error[72];

static unsigned mu_dma_free(void)
{
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
}

static void music_task(void *arg)
{
    (void)arg;
    FILE *f = NULL, *effect = NULL;
    uint8_t *buf = NULL;
    i2s_chan_handle_t tx = NULL;
    const audio_codec_data_if_t *data = NULL;
    const audio_codec_ctrl_if_t *ctrl = NULL;
    const audio_codec_gpio_if_t *gpio = NULL;
    const audio_codec_if_t *codec = NULL;
    esp_codec_dev_handle_t spk = NULL;
    const char *error = NULL;
    bool mounted = false;
    unsigned before = mu_dma_free();
    music_wav_t effect_wav = {0};
    uint32_t effect_left = 0;
#define MU_CHECK(test, message) do { if (!(test)) { error = (message); goto cleanup; } } while (0)
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs", .partition_label = "storage", .max_files = 3,
        .format_if_mount_failed = false,
    };
    MU_CHECK(esp_vfs_spiffs_register(&conf) == ESP_OK, "SPIFFS mount failed");
    mounted = true;
    mu_wav.rate = 22050;
    /* This buffer is copied into DMA by i2s_channel_write; it needn't live in SRAM. */
    buf = heap_caps_malloc(MUSIC_CHUNK * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    MU_CHECK(buf, "Audio buffer unavailable");
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_BSP_I2S_NUM, I2S_ROLE_MASTER);
    chan.auto_clear = true;
    chan.dma_desc_num = 4;
    chan.dma_frame_num = 128;
    MU_CHECK(i2s_new_channel(&chan, &tx, NULL) == ESP_OK, "I2S channel unavailable");
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(mu_wav.rate),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = { .mclk = BSP_I2S_MCLK, .bclk = BSP_I2S_SCLK,
                      .ws = BSP_I2S_LCLK, .dout = BSP_I2S_DOUT, .din = I2S_GPIO_UNUSED },
    };
    MU_CHECK(i2s_channel_init_std_mode(tx, &std) == ESP_OK, "I2S DMA unavailable");
    MU_CHECK(i2s_channel_enable(tx) == ESP_OK, "I2S enable failed");
    audio_codec_i2s_cfg_t dc = { .port = CONFIG_BSP_I2S_NUM, .tx_handle = tx };
    data = audio_codec_new_i2s_data(&dc);
    MU_CHECK(data, "I2S adapter unavailable");
    audio_codec_i2c_cfg_t cc = {
        .port = BSP_I2C_NUM, .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = bsp_i2c_get_handle(),
    };
    ctrl = audio_codec_new_i2c_ctrl(&cc);
    gpio = audio_codec_new_gpio();
    MU_CHECK(ctrl && gpio, "Codec control unavailable");
    /* Same ES8311 configuration and pins as the Waveshare BSP, DAC only. */
    es8311_codec_cfg_t ec = {
        .ctrl_if = ctrl, .gpio_if = gpio, .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = BSP_POWER_AMP_IO, .use_mclk = true,
        .hw_gain = { .pa_voltage = 5.0, .codec_dac_voltage = 3.3 },
    };
    codec = es8311_codec_new(&ec);
    MU_CHECK(codec, "ES8311 init failed");
    esp_codec_dev_cfg_t dev = { .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = codec, .data_if = data };
    spk = esp_codec_dev_new(&dev);
    MU_CHECK(spk, "Codec device unavailable");
    esp_codec_dev_sample_info_t fs = { .sample_rate = mu_wav.rate, .channel = 1, .bits_per_sample = 16 };
    MU_CHECK(esp_codec_dev_open(spk, &fs) == ESP_CODEC_DEV_OK, "Codec open failed");
    MU_CHECK(esp_codec_dev_set_out_mute(spk, true) == ESP_CODEC_DEV_OK, "Codec mute failed");
    int volume = atomic_load(&mu_volume);
    MU_CHECK(esp_codec_dev_set_out_vol(spk, volume) == ESP_CODEC_DEV_OK, "Codec volume failed");
    atomic_store(&mu_applied_volume, volume);
    atomic_store(&mu_state, MU_PAUSED);
    ESP_LOGI(MUSIC_TAG, "ready MUTED: %lu Hz, %lu bytes, DMA %u -> %u",
             (unsigned long)mu_wav.rate, (unsigned long)mu_wav.bytes, before, mu_dma_free());
    bool audible = false;
    for (;;) {
        int source = atomic_exchange(&mu_source_request, -1);
        if (source >= 0) {
            if (f) { fclose(f); f = NULL; }
            atomic_store(&mu_state, MU_LOADING);
            atomic_store(&mu_sent, 0);
            atomic_store(&mu_total_bytes, 0);
            if (source == 2)
                f = fmemopen((void *)frog_voice_start, frog_voice_end - frog_voice_start, "rb");
            else if (source < 2)
                f = fopen(mu_tracks[source].path, "rb");
            const char *problem = f ? music_wav_read(f, &mu_wav) : "Audio file unavailable";
            if (!problem && mu_wav.rate != 22050) problem = "Expected 22050 Hz mono WAV";
            if (problem) {
                if (f) { fclose(f); f = NULL; }
                snprintf(mu_error, sizeof(mu_error), "%s", problem);
                atomic_store(&mu_state, MU_ERROR);
                ESP_LOGE(MUSIC_TAG, "%s", problem);
            } else {
                atomic_store(&mu_total_bytes, mu_wav.bytes);
                atomic_store(&mu_sample_rate, mu_wav.rate);
                atomic_store(&mu_state, MU_PAUSED);
            }
        }
        if (atomic_load(&mu_exit)) {
            if (f) { fclose(f); f = NULL; }
            atomic_store(&mu_play, false);
            atomic_store(&mu_session_closed, true);
        }
        if (atomic_exchange(&mu_rewind, false) && f) {
            MU_CHECK(fseek(f, mu_wav.offset, SEEK_SET) == 0, "Reset seek failed");
            atomic_store(&mu_sent, 0);
            atomic_store(&mu_state, MU_PAUSED);
        }
        int request = atomic_exchange(&sfx_requested, -1);
        if (request >= 0 && request < 8 && !effect) {
            char name[32];
            snprintf(name, sizeof(name), "/spiffs/sfx%d.wav", request);
            effect = fopen(name, "rb");
            const char *problem = effect ? music_wav_read(effect, &effect_wav) : "Missing effect";
            if (problem || effect_wav.rate != 22050) {
                if (effect) fclose(effect);
                effect = NULL;
                atomic_store(&sfx_playing, false);
                ESP_LOGW("shake", "Effect unavailable");
            } else {
                effect_left = effect_wav.bytes;
                atomic_store(&sfx_playing, true);
                ESP_LOGI("shake", "Playing effect %d once", request);
            }
        }
        int volume = atomic_load(&mu_volume);
        if (volume != atomic_load(&mu_applied_volume)) {
            MU_CHECK(esp_codec_dev_set_out_vol(spk, volume) == ESP_CODEC_DEV_OK, "Volume change failed");
            atomic_store(&mu_applied_volume, volume);
        }
        bool base = f && atomic_load(&mu_play) && !atomic_load(&mu_exit);
        uint32_t sent = atomic_load(&mu_sent);
        if (base && sent == mu_wav.bytes) {
            MU_CHECK(fseek(f, mu_wav.offset, SEEK_SET) == 0, "Replay seek failed");
            atomic_store(&mu_sent, 0);
            sent = 0;
        }
        size_t base_bytes = base ? mu_wav.bytes - sent : 0;
        if (base_bytes > MUSIC_CHUNK) base_bytes = MUSIC_CHUNK;
        size_t effect_bytes = effect ? effect_left : 0;
        if (effect_bytes > MUSIC_CHUNK) effect_bytes = MUSIC_CHUNK;
        size_t bytes = base_bytes > effect_bytes ? base_bytes : effect_bytes;
        if (!bytes) {
            if (f && atomic_load(&mu_state) == MU_PLAYING)
                atomic_store(&mu_state, MU_PAUSED);
            if (audible) {
                vTaskDelay(pdMS_TO_TICKS(30));
                MU_CHECK(esp_codec_dev_set_out_mute(spk, true) == ESP_CODEC_DEV_OK, "Idle mute failed");
                audible = false;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        memset(buf, 0, MUSIC_CHUNK * 2);
        if (base_bytes) {
            MU_CHECK(fread(buf, 1, base_bytes, f) == base_bytes, "WAV read failed");
            atomic_store(&mu_state, MU_PLAYING);
        } else if (f && atomic_load(&mu_state) == MU_PLAYING) {
            atomic_store(&mu_state, MU_PAUSED);
        }
        if (effect_bytes) {
            if (fread(buf + MUSIC_CHUNK, 1, effect_bytes, effect) != effect_bytes) {
                effect_bytes = 0;
                effect_left = 0;
            } else effect_left -= effect_bytes;
            int16_t *pcm = (int16_t *)buf;
            const int16_t *fx = (const int16_t *)(buf + MUSIC_CHUNK);
            for (size_t i = 0; i < bytes / 2; i++) {
                int a = i < base_bytes / 2 ? pcm[i] : 0;
                int b = i < effect_bytes / 2 ? fx[i] : 0;
                /* Leave headroom only while overlaying, retaining both timelines. */
                int sample = base_bytes && effect_bytes ? a / 2 + b / 2 : a + b;
                if (sample > 32767) sample = 32767;
                if (sample < -32768) sample = -32768;
                pcm[i] = (int16_t)sample;
            }
        }
#if defined(MUSIC_SELFTEST) || defined(FROG_SELFTEST)
        MU_CHECK(esp_codec_dev_set_out_mute(spk, true) == ESP_CODEC_DEV_OK, "Test mute failed");
#else
        MU_CHECK(esp_codec_dev_set_out_mute(spk, volume == 0) == ESP_CODEC_DEV_OK, "Output mute failed");
#endif
        audible = true;
        MU_CHECK(esp_codec_dev_write(spk, buf, bytes) == ESP_CODEC_DEV_OK, "I2S write failed");
        if (base_bytes) {
            sent += base_bytes;
            atomic_store(&mu_sent, sent);
            if (sent == mu_wav.bytes) {
                atomic_store(&mu_play, false);
                atomic_store(&mu_state, MU_FINISHED);
            }
        }
        if (effect && !effect_left) {
            fclose(effect);
            effect = NULL;
            atomic_store(&sfx_playing, false);
        }
    }
cleanup:
    if (spk) {
        esp_codec_dev_set_out_mute(spk, true);
        esp_codec_dev_close(spk);
        esp_codec_dev_delete(spk);
    }
    if (codec) audio_codec_delete_codec_if(codec);
    if (ctrl) audio_codec_delete_ctrl_if(ctrl);
    if (gpio) audio_codec_delete_gpio_if(gpio);
    if (data) audio_codec_delete_data_if(data);
    if (tx) {
        i2s_chan_info_t info;
        if (i2s_channel_get_info(tx, &info) == ESP_OK && info.is_enabled) i2s_channel_disable(tx);
        i2s_del_channel(tx);
    }
    if (f) fclose(f);
    if (effect) fclose(effect);
    free(buf);
    if (mounted) esp_vfs_spiffs_unregister("storage");
    if (error) {
        snprintf(mu_error, sizeof(mu_error), "%s", error);
        atomic_store(&mu_state, MU_ERROR);
        ESP_LOGE(MUSIC_TAG, "%s", error);
    }
    atomic_store(&mu_session_closed, true);
    atomic_store(&sfx_playing, false);
    atomic_store(&mu_running, false);
    vTaskDelete(NULL);
#undef MU_CHECK
}

/* One always-on worker owns the codec; UI sessions and global SFX share it. */
static void audio_background_start(void)
{
    if (atomic_exchange(&mu_running, true)) return;
    if (xTaskCreate(music_task, "audio", 6144, NULL, 4, NULL) != pdPASS) {
        snprintf(mu_error, sizeof(mu_error), "Audio task unavailable");
        atomic_store(&mu_state, MU_ERROR);
        atomic_store(&mu_running, false);
        atomic_store(&mu_session_closed, true);
    }
}

static void music_session_start(bool memory_source)
{
    atomic_store(&mu_exit, false);
    atomic_store(&mu_play, false);
    atomic_store(&mu_rewind, false);
    atomic_store(&mu_sent, 0);
    atomic_store(&mu_state, MU_LOADING);
    atomic_store(&mu_session_closed, false);
    atomic_store(&mu_source_request, memory_source ? 2 : (int)mu_track_index);
    audio_background_start();
}

static bool music_active(void)
{
    return music_page != NULL;
}

static void mu_back_cb(lv_event_t *e)
{
    (void)e;
#ifndef MUSIC_SELFTEST
    if (!scr_on) return;
    atomic_store(&mu_play, false);
    atomic_store(&mu_exit, true);
    preferences_request_save();
#endif
}

static void mu_play_cb(lv_event_t *e)
{
    (void)e;
#ifndef MUSIC_SELFTEST
    int state = atomic_load(&mu_state);
    if (!scr_on || atomic_load(&mu_exit) || state == MU_LOADING || state == MU_ERROR) return;
    atomic_store(&mu_play, !atomic_load(&mu_play));
#endif
}

static void mu_volume_cb(lv_event_t *e)
{
    if (!scr_on || atomic_load(&mu_exit)) return;
    int volume = lv_slider_get_value(lv_event_get_target(e));
    preferences_volume(volume);
    lv_label_set_text_fmt(mu_volume_text, "%d", volume);
}

static lv_obj_t *mu_label(lv_obj_t *parent, const char *text, int y, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

static void music_ui_timer(lv_timer_t *timer)
{
    if (atomic_load(&mu_exit)) {
        lv_label_set_text(mu_status, "STOPPING...");
        if (atomic_load(&mu_session_closed)) {
            lv_obj_delete(music_page);
            music_page = NULL;
            scr_last_activity = lv_tick_get();
            lv_timer_delete(timer);
        }
        return;
    }
    int volume = atomic_load(&mu_volume);
    lv_slider_set_value(mu_volume_slider, volume, LV_ANIM_OFF);
    lv_label_set_text_fmt(mu_volume_text, "%d", volume);
    int state = atomic_load(&mu_state);
    const char *names[] = { "LOADING...", "PAUSED", "PLAYING", "FINISHED", "ERROR" };
    lv_label_set_text(mu_status, names[state]);
    lv_label_set_text(mu_button_text, state == MU_PLAYING ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    if (state == MU_ERROR) {
        lv_obj_add_flag(mu_artist, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(mu_info, LV_ALIGN_TOP_MID, 0, 287);
        lv_obj_set_width(mu_info, 330);
        lv_label_set_text(mu_info, mu_error);
    } else if (state != MU_LOADING) {
        uint32_t sent = atomic_load(&mu_sent), bps = atomic_load(&mu_sample_rate) * 2;
        uint32_t total_bytes = atomic_load(&mu_total_bytes);
        if (!bps || !total_bytes) return;
        uint32_t sec = sent / bps, total = total_bytes / bps;
        lv_label_set_text_fmt(mu_time, "%lu:%02lu / %lu:%02lu",
            (unsigned long)(sec/60), (unsigned long)(sec%60), (unsigned long)(total/60), (unsigned long)(total%60));
        lv_bar_set_value(mu_bar, (uint64_t)sent * 1000 / total_bytes, LV_ANIM_OFF);

    }
}

/* Local fallback artwork: no image download/decoder/large bitmap allocation. */
static lv_obj_t *mu_shape(lv_obj_t *parent, int size, uint32_t color, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, size, size);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(o);
    return o;
}

static void mu_build_cover(lv_obj_t *parent)
{
    lv_obj_t *cover = mu_shape(parent, 170, 0x263E30, 24);
    lv_obj_align(cover, LV_ALIGN_TOP_MID, 0, 76);
    lv_obj_set_style_bg_grad_color(cover, lv_color_hex(0x14241C), 0);
    lv_obj_set_style_bg_grad_dir(cover, LV_GRAD_DIR_VER, 0);
    if (mu_track.cover) {
        lv_obj_t *image = lv_image_create(cover);
        lv_image_set_src(image, mu_track.cover);
        unsigned side = mu_track.cover->header.w > mu_track.cover->header.h ?
                        mu_track.cover->header.w : mu_track.cover->header.h;
        if (side) lv_image_set_scale(image, 170 * 256 / side);
        lv_obj_center(image);
        return;
    }
    lv_obj_t *record = mu_shape(cover, 132, 0x0A1410, LV_RADIUS_CIRCLE);
    for (int size = 116; size >= 68; size -= 16) {
        lv_obj_t *ring = mu_shape(record, size, 0x0A1410, LV_RADIUS_CIRCLE);
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(ring, 1, 0);
        lv_obj_set_style_border_color(ring, lv_color_hex(0x2A3C31), 0);
    }
    lv_obj_t *center = mu_shape(record, 50, 0xB1C99B, LV_RADIUS_CIRCLE);
    lv_obj_t *note = mu_label(center, LV_SYMBOL_AUDIO, 0, 0x17291F);
    lv_obj_set_style_text_font(note, &lv_font_montserrat_24, 0);
    lv_obj_center(note);
}

static void mu_track_cb(lv_event_t *e)
{
    (void)e;
    if (!scr_on || atomic_load(&mu_exit)) return;
    bool was_playing = atomic_load(&mu_play);
    mu_track_index = (mu_track_index + 1) % 2;
    lv_label_set_text(mu_title, mu_track.title);
    lv_label_set_text(mu_artist, mu_track.artist);
    lv_obj_remove_flag(mu_artist, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(mu_info, "");
    music_session_start(false);
    atomic_store(&mu_play, was_playing);
}

static lv_obj_t *mu_control(lv_obj_t *parent, const char *symbol, int x, int size, bool primary)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, size, size);
    lv_obj_align(button, LV_ALIGN_TOP_MID, x, 350 + (64 - size) / 2);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(primary ? 0xB1C99B : 0x17291F), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x8DAF77), LV_STATE_PRESSED);
    lv_obj_t *label = mu_label(button, symbol, 0, primary ? 0x101C14 : 0x48604E);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_center(label);
    if (primary) {
        mu_button_text = label;
        lv_obj_add_event_cb(button, mu_play_cb, LV_EVENT_CLICKED, NULL);
    } else {
        lv_obj_add_event_cb(button, mu_track_cb, LV_EVENT_CLICKED, NULL);
    }
    return button;
}

/* Called only by LVGL, on the Music row's click; no resources allocated at boot. */
static void music_enter(void)
{
    if (music_page) return;
    music_page = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(music_page);
    lv_obj_set_size(music_page, 466, 466);
    lv_obj_set_style_bg_color(music_page, lv_color_hex(0x080E0A), 0);
    lv_obj_set_style_bg_opa(music_page, LV_OPA_COVER, 0);
    lv_obj_remove_flag(music_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(music_page, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *header = mu_label(music_page, "NOW PLAYING", 39, 0xB1C99B);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_14, 0);
    lv_obj_t *back = lv_button_create(music_page);
    lv_obj_remove_style_all(back);
    lv_obj_set_size(back, 44, 44);
    lv_obj_align(back, LV_ALIGN_TOP_MID, -105, 25);
    lv_obj_add_event_cb(back, mu_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = mu_label(back, LV_SYMBOL_LEFT, 0, 0xB1C99B);
    lv_obj_center(label);
    mu_build_cover(music_page);
    mu_title = mu_label(music_page, mu_track.title, 256, 0xEEF3E9);
    lv_obj_set_style_text_font(mu_title, &lv_font_montserrat_24, 0);
    mu_artist = mu_label(music_page, mu_track.artist, 287, 0x8CA18E);
    lv_obj_set_style_text_font(mu_artist, &lv_font_montserrat_14, 0);
    mu_bar = lv_bar_create(music_page);
    lv_obj_remove_style_all(mu_bar);
    lv_obj_set_size(mu_bar, 278, 5);
    lv_obj_align(mu_bar, LV_ALIGN_TOP_MID, 0, 315);
    lv_obj_set_style_radius(mu_bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(mu_bar, lv_color_hex(0x293D30), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(mu_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(mu_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(mu_bar, lv_color_hex(0xB1C99B), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(mu_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_bar_set_range(mu_bar, 0, 1000);
    lv_bar_set_value(mu_bar, 0, LV_ANIM_OFF);
    mu_time = mu_label(music_page, "0:00 / 1:00", 328, 0x8CA18E);
    lv_obj_set_style_text_font(mu_time, &lv_font_montserrat_14, 0);
    mu_status = mu_label(music_page, "LOADING...", 328, 0xB1C99B);
    lv_obj_set_style_text_font(mu_status, &lv_font_montserrat_14, 0);
    lv_obj_align(mu_time, LV_ALIGN_TOP_LEFT, 94, 328);
    lv_obj_align(mu_status, LV_ALIGN_TOP_RIGHT, -94, 328);
    mu_control(music_page, LV_SYMBOL_PREV, -84, 46, false);
    mu_control(music_page, LV_SYMBOL_PLAY, 0, 64, true);
    mu_control(music_page, LV_SYMBOL_NEXT, 84, 46, false);
    mu_info = mu_label(music_page, "", 287, 0x647D69);
    lv_obj_set_style_text_font(mu_info, &lv_font_montserrat_14, 0);
    /* Errors replace the artist line where the round screen has room for them. */
    lv_label_set_long_mode(mu_info, LV_LABEL_LONG_DOT);
    lv_obj_set_width(mu_info, 220);
    lv_obj_set_style_text_align(mu_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *speaker = mu_label(music_page, LV_SYMBOL_VOLUME_MID, 421, 0x8CA18E);
    lv_obj_align(speaker, LV_ALIGN_TOP_MID, -83, 421);
    mu_volume_text = mu_label(music_page, "30", 421, 0xB1C99B);
    lv_obj_set_style_text_font(mu_volume_text, &lv_font_montserrat_14, 0);
    lv_obj_align(mu_volume_text, LV_ALIGN_TOP_MID, 87, 421);
    lv_label_set_text_fmt(mu_volume_text, "%d", atomic_load(&mu_volume));
    mu_volume_slider = lv_slider_create(music_page);
    lv_obj_remove_style_all(mu_volume_slider);
    lv_obj_set_size(mu_volume_slider, 128, 6);
    lv_obj_align(mu_volume_slider, LV_ALIGN_TOP_MID, 0, 428);
    lv_slider_set_range(mu_volume_slider, 0, 100);
    lv_slider_set_value(mu_volume_slider, atomic_load(&mu_volume), LV_ANIM_OFF);
    lv_obj_set_ext_click_area(mu_volume_slider, 10);
    lv_obj_set_style_radius(mu_volume_slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(mu_volume_slider, lv_color_hex(0x293D30), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(mu_volume_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(mu_volume_slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(mu_volume_slider, lv_color_hex(0xB1C99B), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(mu_volume_slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(mu_volume_slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_bg_color(mu_volume_slider, lv_color_hex(0xD0E4BE), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(mu_volume_slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(mu_volume_slider, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(mu_volume_slider, mu_volume_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(mu_volume_slider, preferences_release_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(mu_volume_slider, preferences_release_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_timer_create(music_ui_timer, 100, NULL);
    music_session_start(false);
}
