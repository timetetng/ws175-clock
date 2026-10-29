#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "clock";

#define SCREEN_W 466
#define CX       (SCREEN_W / 2)
#define CY       (SCREEN_W / 2)

#include "face.inc"
#if 0
static lv_obj_t *hand_hour, *hand_min, *hand_sec;
static lv_obj_t *lbl_time, *lbl_sec, *lbl_date;

static lv_point_precise_t p_hour[2], p_min[2], p_sec[2];
static lv_point_precise_t tick_pts[60][2];

static void set_hand(lv_obj_t *line, lv_point_precise_t *pts, int len, float deg)
{
    // 0 度指向 12 点，顺时针
    float a = (deg - 90.0f) * (float)M_PI / 180.0f;
    pts[0].x = CX;
    pts[0].y = CY;
    pts[1].x = CX + (int)(cosf(a) * len);
    pts[1].y = CY + (int)(sinf(a) * len);
    lv_line_set_points(line, pts, 2);
}

static void build_ticks(lv_obj_t *scr)
{
    const int outer = 206;
    for (int i = 0; i < 60; i++) {
        bool major = (i % 5) == 0;
        int inner = outer - (major ? 18 : 9);
        float a = (i * 6.0f - 90.0f) * (float)M_PI / 180.0f;
        float sx = cosf(a), sy = sinf(a);
        tick_pts[i][0].x = CX + (int)(sx * inner);
        tick_pts[i][0].y = CY + (int)(sy * inner);
        tick_pts[i][1].x = CX + (int)(sx * outer);
        tick_pts[i][1].y = CY + (int)(sy * outer);

        lv_obj_t *l = lv_line_create(scr);
        lv_line_set_points(l, tick_pts[i], 2);
        lv_obj_set_style_line_color(l, lv_color_hex(major ? 0x9AA0A6 : 0x3C4043), 0);
        lv_obj_set_style_line_width(l, major ? 5 : 2, 0);
        lv_obj_set_style_line_rounded(l, true, 0);
    }
}

static lv_obj_t *make_hand(lv_obj_t *scr, lv_point_precise_t *pts, int w, uint32_t color)
{
    lv_obj_t *l = lv_line_create(scr);
    lv_line_set_points(l, pts, 2);
    lv_obj_set_style_line_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_line_width(l, w, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    return l;
}

static void update_clock(lv_timer_t *t)
{
    (void)t;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);

    if (tm.tm_year + 1900 < 2024) {
        lv_label_set_text(lbl_time, "--:--");
        lv_label_set_text(lbl_sec, "--");
        lv_label_set_text(lbl_date, "no time sync");
        return;
    }

    int sec = tm.tm_sec, min = tm.tm_min, hr = tm.tm_hour % 12;
    set_hand(hand_hour, p_hour, 104, (hr + min / 60.0f) * 30.0f);
    set_hand(hand_min,  p_min,  152, (min + sec / 60.0f) * 6.0f);
    set_hand(hand_sec,  p_sec,  174, sec * 6.0f);

    lv_label_set_text_fmt(lbl_time, "%02d:%02d", tm.tm_hour, tm.tm_min);
    lv_label_set_text_fmt(lbl_sec, "%02d", sec);

    static const char *wd[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    lv_label_set_text_fmt(lbl_date, "%04d-%02d-%02d  %s",
                          tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, wd[tm.tm_wday]);
}

#endif

/* ---------- WiFi + SNTP ---------- */
#define WIFI_SSID "CMCC-KkHC"
#define WIFI_PASS "eefshfbq"

static bool s_sntp_started;

static void on_sntp_sync(struct timeval *tv)
{
    time_t t = tv->tv_sec;
    struct tm tm;
    localtime_r(&t, &tm);
    ESP_LOGI(TAG, "SNTP synced: %04d-%02d-%02d %02d:%02d:%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
}

static void start_sntp(void)
{
    if (s_sntp_started) return;
    s_sntp_started = true;
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        2, ESP_SNTP_SERVER_LIST("ntp.aliyun.com", "ntp.tencent.com"));
    cfg.start = true;
    cfg.sync_cb = on_sntp_sync;
    ESP_ERROR_CHECK(esp_netif_sntp_init(&cfg));
    esp_sntp_set_sync_interval(3600 * 1000);  // 每小时再校一次
    ESP_LOGI(TAG, "sntp started");
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "wifi disconnected, retry");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got ip " IPSTR, IP2STR(&e->ip_info.ip));
        start_sntp();
    }
}

static void wifi_start(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    ESP_LOGI(TAG, "DMA before wifi: free=%u largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&ic));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        wifi_event_handler, NULL, NULL));

    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid, WIFI_SSID, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, WIFI_PASS, sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "DMA after wifi: free=%u largest=%u; display buffer=8 rows",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "wifi connecting to %s", WIFI_SSID);
}

static void seed_time_if_needed(void)
{
    // 没有 WiFi/SNTP 时，用编译时刻 + 运行时长做基准，至少让表走起来
    if (time(NULL) > 1700000000LL) {
        return;
    }
    struct timeval tv;
    tv.tv_sec = (time_t)BUILD_EPOCH + (time_t)(esp_timer_get_time() / 1000000LL);
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "no RTC value, seeded from build epoch");
}

/* POWER 接 AXP2101；只处理短按标志，不改长按关机及电源轨。 */
static i2c_master_dev_handle_t power_pmu;

static esp_err_t pmu_read(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(power_pmu, &reg, 1, value, 1, 100);
}

static esp_err_t pmu_write(uint8_t reg, uint8_t value)
{
    uint8_t data[] = {reg, value};
    return i2c_master_transmit(power_pmu, data, sizeof(data), 100);
}

static void power_key_task(void *arg)
{
    (void)arg;
    bool screen_on = true;
    int saved_brightness = 100;
    for (;;) {
        uint8_t status;
        if (pmu_read(0x49, &status) == ESP_OK && (status & 0x08)) {
            /* 写一清除；清除成功才切换，避免一次按键重复触发。 */
            if (pmu_write(0x49, 0x08) == ESP_OK) {
                bsp_display_lock(-1);
                if (screen_on) saved_brightness = bsp_display_brightness_get();
                esp_err_t err = bsp_display_brightness_set(screen_on ? 0 : saved_brightness);
                if (err == ESP_OK) screen_on = !screen_on;
                bsp_display_unlock();
                ESP_LOGI(TAG, "power short press: screen %s (%s)",
                         screen_on ? "on" : "off", esp_err_to_name(err));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void power_key_start(void)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x34,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bsp_i2c_get_handle(), &cfg, &power_pmu));
    uint8_t id, enabled;
    if (pmu_read(0x03, &id) != ESP_OK || (id & 0xCF) != 0x4A) {
        ESP_LOGE(TAG, "AXP2101 identification failed; power key disabled");
        return;
    }
    ESP_ERROR_CHECK(pmu_read(0x41, &enabled));
    ESP_ERROR_CHECK(pmu_write(0x49, 0x08));
    ESP_ERROR_CHECK(pmu_write(0x41, enabled | 0x08));
    if (xTaskCreate(power_key_task, "power_key", 3072, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "power key task allocation failed");
        return;
    }
    ESP_LOGI(TAG, "power key ready: short press toggles screen");
}

void app_main(void)
{
    setenv("TZ", "CST-8", 1);
    tzset();
    seed_time_if_needed();

    lv_display_t *disp = bsp_display_start();
    if (disp == NULL) {
        ESP_LOGE(TAG, "bsp_display_start failed");
        return;
    }
    bsp_display_brightness_set(100);

    // LVGL 任务已在跑，所有 UI 操作必须持锁
    bsp_display_lock(-1);
    lv_obj_t *scr = lv_screen_active();
    build_face(scr);
#if 0
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    build_ticks(scr);

    hand_hour = make_hand(scr, p_hour, 12, 0xF2F2F2);
    hand_min  = make_hand(scr, p_min,  8,  0xF2F2F2);
    hand_sec  = make_hand(scr, p_sec,  3,  0xFF6B4A);

    lv_obj_t *dot = lv_obj_create(scr);
    lv_obj_set_size(dot, 14, 14);
    lv_obj_align(dot, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0xFF6B4A), 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

    lbl_time = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_time, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(lbl_time, "--:--");
    lv_obj_align(lbl_time, LV_ALIGN_CENTER, 0, 78);

    lbl_sec = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_sec, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_sec, lv_color_hex(0xFF6B4A), 0);
    lv_label_set_text(lbl_sec, "--");
    lv_obj_align(lbl_sec, LV_ALIGN_CENTER, 74, 52);

    lbl_date = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_date, lv_color_hex(0x8A8F98), 0);
    lv_label_set_text(lbl_date, "----.--.--");
    lv_obj_align(lbl_date, LV_ALIGN_CENTER, 0, 128);

    update_clock(NULL);
    lv_timer_create(update_clock, 250, NULL);
#endif
    bsp_display_unlock();
    ESP_LOGI(TAG, "clock started");
    power_key_start();

    // 屏幕初始化占住了内部 DMA 内存，WiFi 放后面启才不抢它的 SPI 缓冲
    wifi_start();
}
