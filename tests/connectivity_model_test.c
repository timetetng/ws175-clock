#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../main/connectivity_model.h"
int main(void)
{
    assert(net_password_valid("", true));
    assert(!net_password_valid("password", true));
    assert(!net_password_valid("", false));
    assert(!net_password_valid("1234567", false));
    assert(net_password_valid("12345678", false));
    char password[100];
    for (unsigned i = 0; i < sizeof(password); ++i) password[i] = 'a';
    password[63] = 0;
    assert(net_password_valid(password, false));
    password[63] = 'a'; password[64] = 0; assert(net_password_valid(password, false));
    password[0] = 'g'; assert(!net_password_valid(password, false));
    password[64] = 'a'; password[65] = 0; assert(!net_password_valid(password, false));
    assert(!net_password_valid(NULL, false));
    char name[33], full[64]; memset(full, 'x', sizeof(full));
    net_display_name(name, (const uint8_t *)full, sizeof(full));
    assert(strlen(name) == 32 && name[31] == 'x');
    net_display_name(name, (const uint8_t *)"x\n\xff", 3); assert(!strcmp(name, "x??"));
    net_snapshot_t s = {0};
    net_device_t device = {.name = "test", .address = {1}, .rssi = -80};
    net_result_add(&s, &device); assert(s.count == 1);
    device.name[0] = 0; device.rssi = -65;
    net_result_add(&s, &device);
    assert(s.count == 1 && !strcmp(s.devices[0].name, "test") && s.devices[0].rssi == -65);
    strcpy(device.name, "old name");
    for (int i = 2; i <= 16; ++i) {
        device.address[0] = i; device.rssi = -90 + i;
        net_result_add(&s, &device);
    }
    assert(s.count == NET_MAX_RESULTS);
    device.address[0] = 17; device.rssi = -95; net_result_add(&s, &device);
    for (unsigned i = 0; i < s.count; ++i) assert(s.devices[i].address[0] != 17);
    device.name[0] = 0;
    device.address[0] = 18; device.rssi = -20; net_result_add(&s, &device);
    bool found = false;
    for (unsigned i = 0; i < s.count; ++i) {
        if (s.devices[i].address[0] == 18) { found = true; assert(!s.devices[i].name[0]); }
    }
    assert(found && s.count == NET_MAX_RESULTS);
    device.address[0] = 1; device.address_type = 1; device.rssi = -10;
    net_result_add(&s, &device); /* Same address, different type is a different device. */
    found = false;
    for (unsigned i = 0; i < s.count; ++i)
        if (s.devices[i].address[0] == 1 && s.devices[i].address_type == 1) {
            found = true; assert(!s.devices[i].name[0]); /* Never inherit another device's name. */
        }
    assert(found);
    for (unsigned i = 0; i < 100000; ++i) {
        device.address[0] = i; device.address[1] = i >> 8;
        device.rssi = -(int)(i % 100);
        net_result_add(&s, &device);
        assert(s.count <= NET_MAX_RESULTS);
    }
    net_secret_clear(password, sizeof(password));
    for (unsigned i = 0; i < sizeof(password); ++i) assert(password[i] == 0);
    puts("connectivity model: PASS");
}
