/* Modal settings page. Battery polling and flash writes never run on LVGL. */
static lv_obj_t *settings_page, *settings_light_slider, *settings_volume_slider;
static lv_obj_t *settings_light_value, *settings_volume_value, *settings_battery_value;
static lv_obj_t *settings_battery_detail, *settings_save_label, *settings_back_button;
static lv_timer_t *settings_timer;
static bool settings_light_error;
static void wireless_enter(bool ble);

static void settings_wireless_cb(lv_event_t *e)
{
    if (scr_on) wireless_enter((uintptr_t)lv_event_get_user_data(e) != 0);
}

static void settings_refresh(lv_timer_t *timer)
{
    (void)timer;
    int light = atomic_load(&scr_brightness), volume = atomic_load(&mu_volume);
    lv_slider_set_value(settings_light_slider, light, LV_ANIM_OFF);
    lv_slider_set_value(settings_volume_slider, volume, LV_ANIM_OFF);
    lv_label_set_text_fmt(settings_light_value, "%d%%", light);
    lv_label_set_text_fmt(settings_volume_value, "%d%%", volume);
    unsigned battery = atomic_load(&preferences_battery), percent = battery & 255;
    if (!(battery & BAT_VALID)) {
        lv_label_set_text(settings_battery_value, "--");
        lv_label_set_text(settings_battery_detail, "Battery data unavailable");
    } else if (!(battery & BAT_CONNECTED)) {
        lv_label_set_text(settings_battery_value, "NO BATTERY");
        lv_label_set_text(settings_battery_detail, battery & BAT_USB ? "USB powered" : "No battery detected");
    } else {
        if (percent <= 100) lv_label_set_text_fmt(settings_battery_value, "%u%%", percent);
        else lv_label_set_text(settings_battery_value, "--%");
        lv_label_set_text(settings_battery_detail, battery & BAT_CHARGING ? "Charging" :
                         battery & BAT_USB ? "USB powered" : "On battery");
    }
    const char *message = "SAVED ON DEVICE";
    if (!preferences_storage_ok) message = "STORAGE UNAVAILABLE";
    else if (settings_light_error) message = "BRIGHTNESS CHANGE FAILED";
    else if (atomic_load(&preferences_save_error)) message = "SAVE FAILED / RETRYING";
    else if (atomic_load(&preferences_revision) != atomic_load(&preferences_saved_revision))
        message = atomic_load(&preferences_save_armed) ? "SAVING..." : "RELEASE TO SAVE";
    lv_label_set_text(settings_save_label, message);
}

static void settings_light_cb(lv_event_t *e)
{
    if (!scr_on) return;
    settings_light_error = !preferences_brightness(lv_slider_get_value(lv_event_get_target(e)));
    settings_refresh(NULL);
}

static void settings_volume_cb(lv_event_t *e)
{
    if (!scr_on) return;
    preferences_volume(lv_slider_get_value(lv_event_get_target(e)));
    settings_refresh(NULL);
}

static void settings_back_cb(lv_event_t *e)
{
    (void)e;
    if (!scr_on || !settings_page) return;
    preferences_request_save();
    lv_timer_delete(settings_timer);
    lv_obj_delete(settings_page);
    settings_page = NULL;
    settings_timer = NULL;
    scr_last_activity = lv_tick_get();
}

static lv_obj_t *settings_label(lv_obj_t *parent, const char *text, int x, int y, uint32_t color, bool large)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, large ? &lv_font_montserrat_24 : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, x, y);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

static lv_obj_t *settings_slider(int y, int minimum, lv_event_cb_t cb)
{
    lv_obj_t *slider = lv_slider_create(settings_page);
    lv_obj_remove_style_all(slider);
    lv_obj_set_size(slider, 270, 8);
    lv_obj_align(slider, LV_ALIGN_TOP_MID, 0, y);
    lv_slider_set_range(slider, minimum, 100);
    lv_obj_set_ext_click_area(slider, 15);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x293D30), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xB1C99B), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xD0E4BE), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 7, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, preferences_release_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(slider, preferences_release_cb, LV_EVENT_PRESS_LOST, NULL);
    return slider;
}

static void settings_enter(void)
{
    if (settings_page || music_active() || frog_active()) return;
    settings_light_error = false;
    settings_page = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(settings_page);
    lv_obj_set_size(settings_page, SCREEN_W, SCREEN_W);
    lv_obj_set_style_bg_color(settings_page, lv_color_hex(0x080E0A), 0);
    lv_obj_set_style_bg_opa(settings_page, LV_OPA_COVER, 0);
    lv_obj_remove_flag(settings_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(settings_page, LV_OBJ_FLAG_CLICKABLE);
    settings_label(settings_page, "SETTINGS", 0, 40, 0xB1C99B, false);
    settings_back_button = lv_button_create(settings_page);
    lv_obj_remove_style_all(settings_back_button);
    lv_obj_set_size(settings_back_button, 44, 44);
    lv_obj_align(settings_back_button, LV_ALIGN_TOP_MID, -108, 25);
    lv_obj_add_event_cb(settings_back_button, settings_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back = settings_label(settings_back_button, LV_SYMBOL_LEFT, 0, 0, 0xB1C99B, false);
    lv_obj_center(back);
    lv_obj_t *card = lv_obj_create(settings_page);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 298, 101);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 90);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x17291F), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    settings_label(card, "BATTERY", 0, 11, 0x8CA18E, false);
    settings_battery_value = settings_label(card, "--", 0, 32, 0xEEF3E9, true);
    settings_battery_detail = settings_label(card, "Reading...", 0, 72, 0x8CA18E, false);
    settings_label(settings_page, "BRIGHTNESS", -75, 212, 0x8CA18E, false);
    settings_light_value = settings_label(settings_page, "30%", 108, 208, 0xB1C99B, true);
    settings_light_slider = settings_slider(251, 5, settings_light_cb);
    settings_label(settings_page, "MASTER VOLUME", -60, 289, 0x8CA18E, false);
    settings_volume_value = settings_label(settings_page, "30%", 108, 285, 0xB1C99B, true);
    settings_volume_slider = settings_slider(328, 0, settings_volume_cb);
    for (int i = 0; i < 2; ++i) {
        lv_obj_t *button = lv_button_create(settings_page);
        lv_obj_remove_style_all(button);
        lv_obj_set_size(button, 122, 36);
        lv_obj_align(button, LV_ALIGN_TOP_MID, i ? 67 : -67, 371);
        lv_obj_set_style_radius(button, 18, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x17291F), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x294633), LV_STATE_PRESSED);
        lv_obj_add_event_cb(button, settings_wireless_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_t *label = settings_label(button, i ? LV_SYMBOL_BLUETOOTH " BLE" : LV_SYMBOL_WIFI " WI-FI",
                                         0, 0, 0xB1C99B, false);
        lv_obj_center(label);
    }
    settings_save_label = settings_label(settings_page, "", 0, 424, 0x647D69, false);
    settings_timer = lv_timer_create(settings_refresh, 200, NULL);
    settings_refresh(NULL);
    scr_last_activity = lv_tick_get();
}
