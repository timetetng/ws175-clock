#include "connectivity_model.h"
#include <string.h>
bool net_password_valid(const char *password, bool open)
{
    if (!password) return false;
    size_t n = 0;
    while (n <= 64 && password[n]) ++n;
    if (open) return n == 0;
    if (n >= 8 && n <= 63) return true;
    if (n != 64) return false;
    for (size_t i = 0; i < n; ++i)
        if (!((password[i] >= '0' && password[i] <= '9') ||
              (password[i] >= 'a' && password[i] <= 'f') ||
              (password[i] >= 'A' && password[i] <= 'F'))) return false;
    return true;
}
void net_display_name(char out[33], const uint8_t *bytes, size_t length)
{
    size_t n = 0;
    if (length > 32) length = 32;
    while (n < length && bytes[n]) {
        out[n] = bytes[n] >= 32 && bytes[n] <= 126 ? (char)bytes[n] : '?';
        ++n;
    }
    out[n] = 0;
}
void net_result_add(net_snapshot_t *s, const net_device_t *device)
{
    unsigned pos = s->count;
    bool existing = false;
    for (unsigned i = 0; i < s->count; ++i)
        if (!memcmp(s->devices[i].address, device->address, 6) &&
            s->devices[i].address_type == device->address_type) { pos = i; existing = true; break; }
    if (pos == s->count && s->count >= NET_MAX_RESULTS) {
        pos = 0;
        for (unsigned i = 1; i < s->count; ++i)
            if (s->devices[i].rssi < s->devices[pos].rssi) pos = i;
        if (device->rssi <= s->devices[pos].rssi) return;
    } else if (pos == s->count) ++s->count;
    net_device_t value = *device;
    if (existing && !value.name[0] && s->devices[pos].name[0])
        memcpy(value.name, s->devices[pos].name, sizeof(value.name));
    s->devices[pos] = value;
    ++s->revision;
}
void net_secret_clear(void *memory, size_t size)
{
    volatile uint8_t *p = memory;
    while (size--) *p++ = 0;
}
