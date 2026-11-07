#include <assert.h>
#include <stdio.h>
#include "../main/settings_model.h"
int main(void)
{
    assert(settings_clamp(-100, 5) == 5);
    assert(settings_clamp(30, 5) == 30);
    assert(settings_clamp(101, 5) == 100);
    assert(settings_clamp(-1, 0) == 0);
    assert(settings_clamp(1000, 0) == 100);
    assert(settings_battery(false, 0xff, 0xff, 100) == 255);
    unsigned b = settings_battery(true, 0x20, 0, 100);
    assert((b & BAT_VALID) && (b & BAT_USB) && !(b & BAT_CONNECTED) && (b & 255) == 255);
    b = settings_battery(true, 0x28, 0x20, 56);
    assert((b & BAT_CONNECTED) && (b & BAT_USB) && (b & BAT_CHARGING) && (b & 255) == 56);
    b = settings_battery(true, 8, 0x40, 15);
    assert((b & BAT_CONNECTED) && !(b & BAT_USB) && !(b & BAT_CHARGING) && (b & 255) == 15);
    assert((settings_battery(true, 8, 0, 255) & 255) == 255);
    assert((settings_battery(true, 8, 0, 101) & 255) == 255);
    assert((settings_battery(true, 8, 0, 100) & 255) == 100);
    assert(!(settings_battery(true, 0x28, 8, 100) & BAT_USB));
    puts("settings model: PASS");
}
