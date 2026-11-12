#pragma once
/* Wi-Fi + send-only result upload (docs/WIFI.md).
 *
 * Security model: the logger is only ever a CLIENT. It joins one Wi-Fi network as a station and
 * POSTs results (JSON, el_uplink) to one configured server address. There is no HTTP server, no
 * open port, no SoftAP / provisioning portal, no remote command and no firmware update over
 * Wi-Fi: the server's reply is used only for its HTTP status code. HTTPS is verified against the
 * ESP-IDF certificate bundle. Optional SNTP (Admin setting) is the only other traffic.
 *
 * Off by default (setting wifi_enabled); the radio is not even started until an Admin turns it on
 * or asks for a network scan. Results reach the server in order; the delivered position is a
 * persisted watermark (storage_uplink_wm), so nothing is lost or skipped across power cuts and a
 * result may at worst be sent twice (same "record" number: the server can de-duplicate).
 * State for the UI is in app_state (net_*, uplink_*), with EVT_NET_CHANGED on changes. */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Start the net task (normal boot only). It follows the settings from then on. */
esp_err_t net_start(void);

/* Scan for networks (Admin, Wi-Fi screen). Starts the radio briefly if Wi-Fi is off.
 * false = a scan is already running or the radio could not start. */
bool net_scan_start(void);
typedef struct {
    char   ssid[33];
    int8_t rssi;
    bool   secure;               /* needs a password */
} net_ap_t;
/* -1 while scanning, else the number of networks copied (strongest first, duplicates merged). */
int  net_scan_results(net_ap_t *out, int max);

/* Queue a connection test: POST {"test":true, "results":[]} as soon as connected. */
void net_send_test(void);
