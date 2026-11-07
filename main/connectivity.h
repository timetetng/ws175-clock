#ifndef WS175_CONNECTIVITY_H
#define WS175_CONNECTIVITY_H
#include "connectivity_model.h"
bool connectivity_init(void);
void connectivity_snapshot(net_snapshot_t *out);
bool connectivity_scan(bool ble);
bool connectivity_connect(unsigned index, const char *password);
bool connectivity_disconnect(void);
void connectivity_cancel_scan(void);
/* Called by the existing ESP-IDF station event handler. */
void connectivity_wifi_down(void);
bool connectivity_wifi_retry(void);
void connectivity_wifi_up(const char *ip);
void connectivity_wifi_error(const char *stage, int code);
#endif
