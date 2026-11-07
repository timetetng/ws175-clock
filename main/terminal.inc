/* A local GUI toy; no system(), subprocess, network, filesystem or UART shell. */
#include "toy_shell.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_psram.h"
#include "esp_system.h"
#define TERMINAL_HISTORY_BYTES 6144
static lv_obj_t *terminal_page, *terminal_view, *terminal_output, *terminal_input, *terminal_keyboard;
static lv_obj_t *terminal_fast_button, *terminal_type_button, *terminal_back_button;
static char *terminal_history;

static void terminal_keyboard_show(bool show)
{
    scr_last_activity = lv_tick_get();
    if (show) {
        lv_obj_remove_flag(terminal_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(terminal_fast_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(terminal_type_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(terminal_view, 148);
        lv_obj_align(terminal_input, LV_ALIGN_TOP_MID, 0, 242);
    } else {
        lv_obj_add_flag(terminal_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(terminal_fast_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(terminal_type_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(terminal_view, 250);
        lv_obj_align(terminal_input, LV_ALIGN_TOP_MID, 0, 347);
        lv_obj_remove_state(terminal_input, LV_STATE_FOCUSED);
    }
    lv_obj_scroll_to_y(terminal_view, LV_COORD_MAX, LV_ANIM_OFF);
}

static void terminal_run(const char *line)
{
    if (!terminal_history || !scr_on) return;
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash = 0;
    esp_flash_get_size(NULL, &flash);
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char date[32], prompt[TOY_SHELL_COMMAND_MAX + 12], response[1024];
    strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", &tm);
    toy_shell_info_t info = {
        .idf = esp_get_idf_version(), .kernel = tskKERNEL_VERSION_NUMBER, .date = date,
        .uptime_seconds = esp_timer_get_time() / 1000000,
        .cores = chip.cores, .mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, .revision = chip.revision,
        .flash_mb = flash / (1024 * 1024), .psram_mb = esp_psram_get_size() / (1024 * 1024),
        .free_ram_kb = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024,
        .brightness = atomic_load(&scr_brightness), .volume = atomic_load(&mu_volume),
    };
    toy_shell_result_t result = toy_shell_run(line, &info, response, sizeof(response));
    if (result == TOY_SHELL_CLEAR) terminal_history[0] = '\0';
    else {
        snprintf(prompt, sizeof(prompt), "esp32$ %s\n", line);
        toy_shell_append(terminal_history, TERMINAL_HISTORY_BYTES, prompt);
        toy_shell_append(terminal_history, TERMINAL_HISTORY_BYTES, response);
        toy_shell_append(terminal_history, TERMINAL_HISTORY_BYTES, "\n");
    }
    lv_label_set_text(terminal_output, terminal_history);
    lv_textarea_set_text(terminal_input, "");
    terminal_keyboard_show(false);
    scr_last_activity = lv_tick_get();
}

static void terminal_activity_cb(lv_event_t *e)
{
    (void)e;
    scr_last_activity = lv_tick_get();
}

static void terminal_input_cb(lv_event_t *e)
{
    if (!scr_on) return;
    if (lv_event_get_code(e) == LV_EVENT_READY) terminal_run(lv_textarea_get_text(terminal_input));
    else terminal_keyboard_show(true);
}

static void terminal_cancel_cb(lv_event_t *e)
{
    (void)e;
    if (scr_on) terminal_keyboard_show(false);
}

static void terminal_fast_cb(lv_event_t *e)
{
    (void)e;
    terminal_run("fastfetch");
}

static void terminal_type_cb(lv_event_t *e)
{
    (void)e;
    if (scr_on) terminal_keyboard_show(true);
}

static void terminal_back_cb(lv_event_t *e)
{
    (void)e;
    if (!scr_on || !terminal_page) return;
    lv_obj_delete(terminal_page);
    free(terminal_history);
    terminal_history = NULL;
    terminal_page = NULL;
    scr_last_activity = lv_tick_get();
}

static lv_obj_t *terminal_button(const char *text, int x, int y, int width, lv_event_cb_t cb)
{
    lv_obj_t *button = lv_button_create(terminal_page);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, width, 36);
    lv_obj_align(button, LV_ALIGN_TOP_MID, x, y);
    lv_obj_set_style_radius(button, 18, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x17291F), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x294633), LV_STATE_PRESSED);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = mu_label(button, text, 0, 0xB1C99B);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_center(label);
    return button;
}

static void terminal_enter(void)
{
    if (terminal_page || settings_page || music_active() || frog_active()) return;
    terminal_history = heap_caps_calloc(1, TERMINAL_HISTORY_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!terminal_history) { ESP_LOGE("terminal", "History allocation failed"); return; }
    terminal_page = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(terminal_page);
    lv_obj_set_size(terminal_page, SCREEN_W, SCREEN_W);
    lv_obj_set_style_bg_color(terminal_page, lv_color_hex(0x080E0A), 0);
    lv_obj_set_style_bg_opa(terminal_page, LV_OPA_COVER, 0);
    lv_obj_remove_flag(terminal_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(terminal_page, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *title = mu_label(terminal_page, "SHELL / toy", 40, 0xB1C99B);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    terminal_back_button = terminal_button(LV_SYMBOL_LEFT, -110, 26, 36, terminal_back_cb);
    terminal_view = lv_obj_create(terminal_page);
    lv_obj_remove_style_all(terminal_view);
    lv_obj_set_size(terminal_view, 320, 250);
    lv_obj_align(terminal_view, LV_ALIGN_TOP_MID, 0, 84);
    lv_obj_set_style_bg_color(terminal_view, lv_color_hex(0x0C1811), 0);
    lv_obj_set_style_bg_opa(terminal_view, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(terminal_view, 12, 0);
    lv_obj_set_style_pad_all(terminal_view, 10, 0);
    lv_obj_set_scroll_dir(terminal_view, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(terminal_view, LV_SCROLLBAR_MODE_AUTO);
    terminal_output = lv_label_create(terminal_view);
    lv_obj_set_width(terminal_output, 298);
    lv_label_set_long_mode(terminal_output, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(terminal_output, &lv_font_unscii_16, 0);
    lv_obj_set_style_text_color(terminal_output, lv_color_hex(0xB1C99B), 0);
    lv_obj_set_style_text_line_space(terminal_output, 3, 0);
    lv_obj_remove_flag(terminal_output, LV_OBJ_FLAG_CLICKABLE);
    terminal_input = lv_textarea_create(terminal_page);
    lv_obj_set_size(terminal_input, 300, 36);
    lv_textarea_set_one_line(terminal_input, true);
    lv_textarea_set_max_length(terminal_input, TOY_SHELL_COMMAND_MAX);
    lv_textarea_set_placeholder_text(terminal_input, "esp32$ type a command");
    lv_obj_set_style_text_font(terminal_input, &lv_font_unscii_16, 0);
    lv_obj_set_style_text_color(terminal_input, lv_color_hex(0xEEF3E9), 0);
    lv_obj_set_style_bg_color(terminal_input, lv_color_hex(0x17291F), 0);
    lv_obj_set_style_border_color(terminal_input, lv_color_hex(0x35533D), 0);
    lv_obj_set_style_border_width(terminal_input, 1, 0);
    lv_obj_set_style_pad_all(terminal_input, 8, 0);
    lv_obj_add_event_cb(terminal_input, terminal_input_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(terminal_input, terminal_input_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(terminal_input, terminal_activity_cb, LV_EVENT_VALUE_CHANGED, NULL);
    terminal_fast_button = terminal_button("FASTFETCH", -70, 400, 120, terminal_fast_cb);
    terminal_type_button = terminal_button("TYPE", 70, 400, 120, terminal_type_cb);
    terminal_keyboard = lv_keyboard_create(terminal_page);
    lv_obj_set_size(terminal_keyboard, 300, 136);
    lv_obj_align(terminal_keyboard, LV_ALIGN_TOP_MID, 0, 284);
    lv_keyboard_set_textarea(terminal_keyboard, terminal_input);
    lv_keyboard_set_popovers(terminal_keyboard, false);
    lv_obj_set_style_bg_color(terminal_keyboard, lv_color_hex(0x080E0A), 0);
    lv_obj_set_style_border_width(terminal_keyboard, 0, 0);
    lv_obj_set_style_pad_all(terminal_keyboard, 2, 0);
    lv_obj_set_style_pad_row(terminal_keyboard, 3, 0);
    lv_obj_set_style_pad_column(terminal_keyboard, 3, 0);
    lv_obj_set_style_bg_color(terminal_keyboard, lv_color_hex(0x17291F), LV_PART_ITEMS);
    lv_obj_set_style_text_color(terminal_keyboard, lv_color_hex(0xB1C99B), LV_PART_ITEMS);
    lv_obj_set_style_text_font(terminal_keyboard, &lv_font_montserrat_14, LV_PART_ITEMS);
    lv_obj_set_style_radius(terminal_keyboard, 6, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(terminal_keyboard, 0, LV_PART_ITEMS);
    lv_obj_add_event_cb(terminal_keyboard, terminal_cancel_cb, LV_EVENT_CANCEL, NULL);
    toy_shell_append(terminal_history, TERMINAL_HISTORY_BYTES, "WS175 TOY SHELL\nType help. No real Unix.\n\n");
    terminal_run("fastfetch");
    ESP_LOGI("terminal", "opened (local toy)");
}
