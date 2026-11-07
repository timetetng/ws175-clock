/* QMI8658 register layout follows Waveshare's SensorLib QMI8658 implementation.
 * Accelerometer only: +/-8g, 125Hz; sample 50Hz. No UI/display dependency.
 * Six alternating vertical acceleration peaks represent three up/down shakes. */
#include "esp_random.h"
static i2c_master_dev_handle_t shake_imu;

static bool shake_write(uint8_t reg, uint8_t value)
{
    uint8_t data[] = {reg, value};
    return i2c_master_transmit(shake_imu, data, 2, 100) == ESP_OK;
}

static void shake_task(void *arg)
{
    (void)arg;
    float gravity[3] = {0};
    unsigned samples = 0, peaks = 0;
    int previous_sign = 0;
    uint32_t first_peak = 0, last_peak = 0, last_trigger = 0;
    const uint8_t choices[] = {0,0,0,1,1,1,2,2,2,3,4,5,6,7};
    for (;;) {
        uint8_t reg = 0x35, raw[6];
        if (i2c_master_transmit_receive(shake_imu, &reg, 1, raw, sizeof(raw), 100) == ESP_OK) {
            int a[3];
            for (int i=0;i<3;i++) {
                a[i] = (int16_t)((uint16_t)raw[i*2] | (uint16_t)raw[i*2+1] << 8);
                if (!samples) gravity[i] = a[i];
                else gravity[i] += 0.03f * (a[i] - gravity[i]);
            }
            if (samples < 50) samples++;
            int axis = 0;
            for (int i=1;i<3;i++)
                if (fabsf(gravity[i]) > fabsf(gravity[axis])) axis = i;
            float dynamic = a[axis] - gravity[axis];
            int sign = dynamic > 2800 ? 1 : dynamic < -2800 ? -1 : 0;
            uint32_t now = (uint32_t)(esp_timer_get_time()/1000);
            if (peaks && (now-last_peak > 550 || now-first_peak > 2200)) {
                peaks = 0; previous_sign = 0;
            }
            if (samples >= 25 && sign && sign != previous_sign &&
                now-last_peak >= 70 && now-last_trigger > 1500 &&
                !atomic_load(&sfx_playing)) {
                if (!peaks) first_peak = now;
                previous_sign = sign;
                last_peak = now;
                if (++peaks >= 6) {
                    int choice = choices[esp_random() % sizeof(choices)];
                    atomic_store(&sfx_requested, choice);
                    ESP_LOGI("shake", "Three shakes: effect %d", choice);
                    last_trigger = now;
                    peaks = 0; previous_sign = 0;
                }
            }
        } else { peaks = 0; previous_sign = 0; }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void shake_start(void)
{
    for (int address=0x6a; address<=0x6b; address++) {
        i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = address, .scl_speed_hz = 100000,
        };
        if (i2c_master_bus_add_device(bsp_i2c_get_handle(), &cfg, &shake_imu) != ESP_OK)
            continue;
        uint8_t reg = 0, id = 0;
        if (i2c_master_transmit_receive(shake_imu, &reg, 1, &id, 1, 100) == ESP_OK && id == 0x05) {
            if (!shake_write(0x08, 0x00) || !shake_write(0x02, 0x60) ||
                !shake_write(0x03, 0x26) || !shake_write(0x08, 0x01)) {
                ESP_LOGE("shake", "QMI8658 configuration failed");
                i2c_master_bus_rm_device(shake_imu); shake_imu = NULL; return;
            }
            if (xTaskCreate(shake_task, "shake", 3072, NULL, 2, NULL) != pdPASS)
                ESP_LOGE("shake", "Task unavailable");
            else ESP_LOGI("shake", "Global gesture active, QMI8658 at 0x%02x", address);
            return;
        }
        i2c_master_bus_rm_device(shake_imu);
        shake_imu = NULL;
    }
    ESP_LOGE("shake", "QMI8658 not detected; clock and music remain usable");
}
