# Wi-Fi: send-only result upload

Off by default. An Admin enables it, chooses a network and an upload server address; the logger
then POSTs every result (manual and automatic) to that server. Code: `main/net/net.c` (radio,
HTTPS), `el_uplink` (JSON, URL rules, retry timing; host-tested).

## 1. Security model
| Rule | How |
|---|---|
| Nothing can connect **to** the logger | station mode only; no HTTP server, no open port, no SoftAP, no mDNS |
| The server cannot control the logger | only the HTTP status of the reply is used; redirects are not followed |
| No firmware over Wi-Fi | updates only from the SD card |
| Verified TLS | HTTPS checked against the ESP-IDF certificate bundle; plain `http://` allowed for a local test server |
| Downgrade | with a password set, open / WEP networks of the same name are refused (WPA2 minimum) |
| Secrets | Wi-Fi password and access key are secret settings: never printed, logged or exported |

## 2. Settings (Admin)
`wifi_enabled`, `wifi_ssid`, `wifi_password` (8-63 characters or empty for an open network),
`upload_url` (`https://host/path`; empty = connect only), `upload_key` (sent as
`Authorization: Bearer <key>`; empty = none), `wifi_time_sync` (set the clock from SNTP).

## 3. Request
`POST <upload_url>`, `Content-Type: application/json`, `User-Agent: envlogger/<fw>`:

```json
{"device":{"type":"envlogger","serial":"EL-0001","location":"Archive B","fw":"0.1.0"},
 "test":false,
 "results":[{"record":1043,"timestamp":"2026-11-02T14:31:05Z","tag":"Archive B","operator":"",
   "temperature_c":21.4,"temperature_sd_c":0.03,"humidity_pct":48.6,"humidity_sd_pct":0.12,
   "dew_point_c":10.1,"repeats":3,"status":"NORMAL","alarms":"","limit_t_low_c":15.0,
   "limit_t_high_c":25.0,"limit_rh_low_pct":35.0,"limit_rh_high_pct":55.0,"flags":["AUTO"]}]}
```

- Up to 10 results per request, oldest first. `null` = no value. `record` is unique per logger:
  a receiver should de-duplicate on (serial, record).
- 2xx = delivered: the persisted upload watermark advances. Network errors, 408, 429 and 5xx
  retry with back-off (5 s doubling to 10 min). Other 3xx/4xx (wrong address or key) retry
  every 30 min.
- "Send test" on the Wi-Fi screen posts `"test":true` with no results.

## 4. Receivers
Anything that accepts an HTTPS POST and answers 2xx: a small script on a PC (Node-RED, Python),
a home-automation server, or a no-code webhook service.
