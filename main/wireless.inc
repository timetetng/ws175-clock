/* Settings subpages; scans/connect/NVS work stays in connectivity.c tasks. */
static lv_obj_t *wireless_page, *wireless_list, *wireless_status, *wireless_current;
static lv_obj_t *wireless_scan_button, *wireless_disconnect_button;
static lv_obj_t *wireless_dialog, *wireless_password, *wireless_password_error;
static lv_timer_t *wireless_timer;
static net_snapshot_t *wireless_snapshot;
static bool wireless_ble;
static unsigned wireless_revision, wireless_selected_index;

static lv_obj_t *wireless_button(lv_obj_t *parent, const char *text, int x, int y,
                                 int width, lv_event_cb_t cb, void *data)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, width, 36);
    lv_obj_align(button, LV_ALIGN_TOP_MID, x, y);
    lv_obj_set_style_radius(button, 18, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x17291F), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x294633), LV_STATE_PRESSED);
    lv_obj_set_style_opa(button, LV_OPA_50, LV_STATE_DISABLED);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, data);
    lv_obj_t *label = settings_label(button, text, 0, 0, 0xB1C99B, false);
    lv_obj_center(label);
    return button;
}
static lv_obj_t *wireless_panel(lv_obj_t *parent)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, SCREEN_W, SCREEN_W);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x080E0A), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    return panel;
}
static void wireless_activity(lv_event_t *e)
{
    (void)e;
    scr_last_activity = lv_tick_get();
}
static void wireless_dialog_close(lv_event_t *e)
{
    (void)e;
    if (!scr_on || !wireless_dialog) return;
    lv_textarea_set_text(wireless_password, "");
    lv_obj_delete(wireless_dialog);
    wireless_dialog = NULL;
    scr_last_activity = lv_tick_get();
}
static void wireless_confirm(lv_event_t *e)
{
    (void)e;
    if (!scr_on || !wireless_dialog) return;
    scr_last_activity = lv_tick_get();
    bool open = wireless_snapshot->devices[wireless_selected_index].auth == WIFI_AUTH_OPEN;
    const char *password = lv_textarea_get_text(wireless_password);
    if (!net_password_valid(password, open)) {
        lv_label_set_text(wireless_password_error, "Use 8-63 characters or 64 hex digits");
        return;
    }
    if (!connectivity_connect(wireless_selected_index, password)) {
        lv_label_set_text(wireless_password_error, "Cannot connect yet / check status and retry");
        return;
    }
    wireless_dialog_close(NULL);
}
static void wireless_select(lv_event_t *e)
{
    if (!scr_on || wireless_dialog || wireless_ble || wireless_snapshot->busy) return;
    unsigned index = (unsigned)(uintptr_t)lv_event_get_user_data(e);
    if (index >= wireless_snapshot->count) return;
    scr_last_activity = lv_tick_get();
    wireless_selected_index = index;
    const net_device_t *device = &wireless_snapshot->devices[index];
    wireless_dialog = wireless_panel(wireless_page);
    wireless_button(wireless_dialog, LV_SYMBOL_LEFT, -108, 26, 36, wireless_dialog_close, NULL);
    settings_label(wireless_dialog, "CONNECT WI-FI", 0, 40, 0xB1C99B, false);
    lv_obj_t *name = settings_label(wireless_dialog, device->name[0] ? device->name : "Hidden network",
                                     0, 88, 0xEEF3E9, true);
    lv_obj_set_width(name, 300);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    bool open = device->auth == WIFI_AUTH_OPEN;
    settings_label(wireless_dialog, open ? "Open network / no password" : "Password (hidden)",
                   0, 124, 0x8CA18E, false);
    wireless_password = lv_textarea_create(wireless_dialog);
    lv_obj_set_size(wireless_password, 300, 38);
    lv_obj_align(wireless_password, LV_ALIGN_TOP_MID, 0, 152);
    lv_textarea_set_one_line(wireless_password, true);
    lv_textarea_set_max_length(wireless_password, 64);
    lv_textarea_set_password_mode(wireless_password, true);
    lv_obj_set_style_bg_color(wireless_password, lv_color_hex(0x17291F), 0);
    lv_obj_set_style_text_color(wireless_password, lv_color_hex(0xEEF3E9), 0);
    lv_obj_set_style_border_color(wireless_password, lv_color_hex(0x35533D), 0);
    lv_obj_add_event_cb(wireless_password, wireless_confirm, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(wireless_password, wireless_activity, LV_EVENT_VALUE_CHANGED, NULL);
    wireless_password_error = settings_label(wireless_dialog, "Saved only after successful connection",
                                              0, 202, 0x8CA18E, false);
    lv_obj_set_width(wireless_password_error, 300);
    lv_obj_set_style_text_align(wireless_password_error, LV_TEXT_ALIGN_CENTER, 0);
    wireless_button(wireless_dialog, "CONNECT", 0, 245, 140, wireless_confirm, NULL);
    lv_obj_t *keyboard = lv_keyboard_create(wireless_dialog);
    lv_obj_set_size(keyboard, 300, 130);
    lv_obj_align(keyboard, LV_ALIGN_TOP_MID, 0, 291);
    lv_keyboard_set_textarea(keyboard, wireless_password);
    lv_keyboard_set_popovers(keyboard, false);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(0x080E0A), 0);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(0x17291F), LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard, lv_color_hex(0xB1C99B), LV_PART_ITEMS);
    lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_14, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(keyboard, 0, LV_PART_ITEMS);
    lv_obj_add_event_cb(keyboard, wireless_dialog_close, LV_EVENT_CANCEL, NULL);
    if (open) {
        lv_obj_add_flag(wireless_password, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}
static void wireless_scan_cb(lv_event_t *e)
{
    (void)e;
    if (scr_on) {
        scr_last_activity = lv_tick_get();
        connectivity_scan(wireless_ble);
    }
}
static void wireless_disconnect_cb(lv_event_t *e)
{
    (void)e;
    if (scr_on) {
        scr_last_activity = lv_tick_get();
        connectivity_disconnect();
    }
}
static void wireless_refresh(lv_timer_t *timer)
{
    (void)timer;
    connectivity_snapshot(wireless_snapshot);
    const net_snapshot_t *s = wireless_snapshot;
    if (wireless_ble) lv_label_set_text(wireless_current, "Passive scan / no pairing or connection");
    else if (s->wifi_up) lv_label_set_text_fmt(wireless_current, "%s\nIP: %s", s->wifi_name, s->ip);
    else lv_label_set_text(wireless_current, "Wi-Fi not connected / 2.4 GHz only");
    lv_label_set_text(wireless_status, s->mode == (wireless_ble ? 2 : 1) || s->busy ?
                      s->status : "Tap SCAN to find devices");
    if (s->busy) lv_obj_add_state(wireless_scan_button, LV_STATE_DISABLED);
    else lv_obj_remove_state(wireless_scan_button, LV_STATE_DISABLED);
    if (wireless_disconnect_button) {
        if (s->busy) lv_obj_add_state(wireless_disconnect_button, LV_STATE_DISABLED);
        else lv_obj_remove_state(wireless_disconnect_button, LV_STATE_DISABLED);
    }
    if (s->busy || wireless_dialog || wireless_revision == s->revision) return;
    wireless_revision = s->revision;
    lv_obj_clean(wireless_list);
    if (s->mode != (wireless_ble ? 2 : 1)) return;
    for (unsigned i = 0; i < s->count; ++i) {
        const net_device_t *device = &s->devices[i];
        lv_obj_t *row = lv_button_create(wireless_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 296, 58);
        lv_obj_set_pos(row, 2, i * 64);
        lv_obj_set_style_radius(row, 12, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x17291F), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x294633), LV_STATE_PRESSED);
        if (!wireless_ble) lv_obj_add_event_cb(row, wireless_select, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        else lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *name = settings_label(row, device->name[0] ? device->name :
                                         wireless_ble ? "Unnamed BLE device" : "Hidden network", 0, 8, 0xEEF3E9, false);
        lv_obj_set_width(name, 272);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        char detail[64];
        if (wireless_ble) snprintf(detail, sizeof(detail), "%02X:%02X:%02X:%02X:%02X:%02X / %d dBm",
                                  device->address[5], device->address[4], device->address[3],
                                  device->address[2], device->address[1], device->address[0], device->rssi);
        else snprintf(detail, sizeof(detail), "%d dBm / %s", device->rssi,
                      device->auth == WIFI_AUTH_OPEN ? "OPEN" : "PROTECTED");
        settings_label(row, detail, 0, 34, 0x8CA18E, false);
    }
}
static void wireless_back(lv_event_t *e)
{
    (void)e;
    if (!scr_on || !wireless_page) return;
    connectivity_cancel_scan();
    lv_timer_delete(wireless_timer);
    lv_obj_delete(wireless_page);
    wireless_page = wireless_dialog = NULL;
    free(wireless_snapshot);
    wireless_snapshot = NULL;
    scr_last_activity = lv_tick_get();
}
static void wireless_enter(bool ble)
{
    if (wireless_page || !settings_page || !connectivity_init()) return;
    wireless_snapshot = heap_caps_calloc(1, sizeof(*wireless_snapshot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!wireless_snapshot) return;
    wireless_ble = ble;
    wireless_revision = UINT32_MAX;
    wireless_disconnect_button = NULL;
    wireless_page = wireless_panel(settings_page);
    wireless_button(wireless_page, LV_SYMBOL_LEFT, -108, 26, 36, wireless_back, NULL);
    settings_label(wireless_page, ble ? "BLUETOOTH / BLE" : "WI-FI", 0, 40, 0xB1C99B, false);
    wireless_current = settings_label(wireless_page, "", 0, 80, 0x8CA18E, false);
    lv_obj_set_width(wireless_current, 310);
    lv_obj_set_height(wireless_current, 32);
    lv_obj_set_style_text_align(wireless_current, LV_TEXT_ALIGN_CENTER, 0);
    wireless_status = settings_label(wireless_page, "", 0, 120, 0xB1C99B, false);
    lv_obj_set_width(wireless_status, 310);
    lv_obj_set_height(wireless_status, 34);
    lv_obj_set_style_text_align(wireless_status, LV_TEXT_ALIGN_CENTER, 0);
    wireless_list = lv_obj_create(wireless_page);
    lv_obj_remove_style_all(wireless_list);
    lv_obj_set_size(wireless_list, 308, 198);
    lv_obj_align(wireless_list, LV_ALIGN_TOP_MID, 0, 165);
    lv_obj_set_scroll_dir(wireless_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(wireless_list, LV_SCROLLBAR_MODE_AUTO);
    wireless_scan_button = wireless_button(wireless_page, "SCAN", ble ? 0 : -65, 380, 116, wireless_scan_cb, NULL);
    if (!ble) wireless_disconnect_button = wireless_button(wireless_page, "DISCONNECT", 65, 380, 116,
                                                           wireless_disconnect_cb, NULL);
    settings_label(wireless_page, ble ? "BLE ONLY / NO PAIRING" : "2.4 GHz / personal networks", 0, 429, 0x647D69, false);
    wireless_timer = lv_timer_create(wireless_refresh, 300, NULL);
    wireless_refresh(NULL);
    scr_last_activity = lv_tick_get();
}
