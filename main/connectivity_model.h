#ifndef WS175_CONNECTIVITY_MODEL_H
#define WS175_CONNECTIVITY_MODEL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define NET_MAX_RESULTS 16
typedef struct {
    char name[33];       /* Display only: printable ASCII, never used as SSID. */
    char ssid[33];       /* Wi-Fi raw SSID; no passwords in snapshots. */
    uint8_t address[6], address_type, auth;
    int8_t rssi;
} net_device_t;
typedef struct {
    unsigned revision, count;
    int mode; /* 1=Wi-Fi, 2=BLE */
    bool busy, wifi_up;
    char status[96], wifi_name[33], ip[16];
    net_device_t devices[NET_MAX_RESULTS];
} net_snapshot_t;
bool net_password_valid(const char *password, bool open);
void net_display_name(char out[33], const uint8_t *bytes, size_t length);
void net_result_add(net_snapshot_t *s, const net_device_t *device);
void net_secret_clear(void *memory, size_t size);
#endif
