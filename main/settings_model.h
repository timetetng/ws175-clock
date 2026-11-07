#ifndef WS175_SETTINGS_MODEL_H
#define WS175_SETTINGS_MODEL_H
#include <stdbool.h>
#include <stdint.h>
#define SETTINGS_DEFAULT 30
#define BAT_VALID (1u << 8)
#define BAT_CONNECTED (1u << 9)
#define BAT_USB (1u << 10)
#define BAT_CHARGING (1u << 11)
static inline int settings_clamp(int value, int minimum)
{
    return value < minimum ? minimum : value > 100 ? 100 : value;
}
/* Status bits/registers verified against lewisxhe/XPowersLib AXP2101 driver.
 * 0xff represents unknown percentage, never a fabricated measurement. */
static inline unsigned settings_battery(bool valid, uint8_t status0, uint8_t status1, uint8_t percent)
{
    if (!valid) return 255;
    bool connected = (status0 & 8) != 0;
    unsigned result = BAT_VALID | (connected ? BAT_CONNECTED : 0);
    if ((status0 & 0x20) && !(status1 & 8)) result |= BAT_USB;
    if (connected && (status1 >> 5) == 1) result |= BAT_CHARGING;
    return result | (connected && percent <= 100 ? percent : 255);
}
#endif
