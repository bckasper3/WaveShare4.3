/* Wi-Fi station + send-only HTTP(S) upload. See net.h for the security model, docs/WIFI.md for
 * the behaviour. Decisions (URL rules, JSON, retry timing, status verdicts) are envlogger_core
 * el_uplink, host-tested; this file only drives the radio and the HTTP client. */
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "net.h"
#include "app_state.h"
#include "app_events.h"
#include "storage.h"
#include "rtc.h"
#include "fw_update.h"
#include "el_uplink.h"
#include "el_time.h"
#include "el_util.h"

static const char *TAG = "net";

#define NET_STACK          8192
#define NET_PRIO           3          /* below the measurement and UI tasks */
#define NET_IDLE_MS        2000u      /* housekeeping tick when nothing happens */
#define CONNECT_TIMEOUT_MS 20000u     /* no IP after this = one failed attempt */
#define HTTP_TIMEOUT_MS    10000
#define SCAN_MAX           20

/* task notification bits */
#define N_WAKE    (1u << 0)           /* settings, log, test, time: re-check everything */
#define N_WIFI    (1u << 1)           /* radio event (connected / lost / IP) */
#define N_SCAN    (1u << 2)           /* scan finished or requested */

static TaskHandle_t      s_task;
static SemaphoreHandle_t s_mtx;       /* s_scan*, s_ip, s_disc_reason */

/* radio */
static esp_netif_t *s_netif;
static bool     s_inited, s_started, s_connecting, s_connected;
static uint32_t s_connect_t0;
static uint8_t  s_disc_reason;
static char     s_ip[16];
static char     s_ap_ssid[33], s_ap_pass[EL_UPLINK_PASS_MAX + 1];   /* config in the driver */
static el_uplink_sched_t s_conn;

/* scan */
static bool     s_scan_req, s_scan_running;
static int      s_scan_n = -1;
static net_ap_t s_scan[SCAN_MAX];

/* upload */
static el_uplink_sched_t s_up;
static volatile bool s_test_req;
static char    *s_body;
static char     s_url_applied[EL_UPLINK_URL_MAX + 1], s_key_applied[EL_UPLINK_TOKEN_MAX + 1];
static bool     s_sntp_on;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static void wake(uint32_t bits) { if (s_task) xTaskNotify(s_task, bits, eSetBits); }

/* ---------------------------------------------------------------- app_state */

static void publish(net_state_t st, const char *err)
{
    wifi_ap_record_t ap;
    int8_t rssi = 0;
    if (s_connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) rssi = ap.rssi;
    char ip[16];
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    memcpy(ip, s_ip, sizeof ip);
    xSemaphoreGive(s_mtx);

    bool changed;
    app_state_lock();
    app_state_t *a = app_state_raw();
    changed = a->net_state != st || strcmp(a->net_ip, ip) != 0 || strcmp(a->net_ssid, s_ap_ssid) != 0 ||
              (err && strcmp(a->net_error, err) != 0) || (a->net_rssi / 5) != (rssi / 5);
    a->net_state = st;
    a->net_rssi = rssi;
    el_strlcpy(a->net_ip, ip, sizeof a->net_ip);
    el_strlcpy(a->net_ssid, s_ap_ssid, sizeof a->net_ssid);
    if (err) el_strlcpy(a->net_error, err, sizeof a->net_error);
    app_state_unlock();
    if (changed) app_events_post(EVT_NET_CHANGED, SRC_INTERNAL, 0);
}

static void publish_uplink(uint32_t pending, bool busy, bool rejected, int64_t ok_s, const char *err)
{
    bool changed;
    app_state_lock();
    app_state_t *a = app_state_raw();
    changed = a->uplink_pending != pending || a->uplink_busy != busy || a->uplink_rejected != rejected ||
              (ok_s && a->uplink_last_ok_s != ok_s) || (err && strcmp(a->net_error, err) != 0);
    a->uplink_pending = pending;
    a->uplink_busy = busy;
    a->uplink_rejected = rejected;
    if (ok_s) a->uplink_last_ok_s = ok_s;
    if (err) el_strlcpy(a->net_error, err, sizeof a->net_error);
    app_state_unlock();
    if (changed) app_events_post(EVT_NET_CHANGED, SRC_INTERNAL, 0);
}

/* ---------------------------------------------------------------- radio */

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *d = data;
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        s_disc_reason = d ? d->reason : 0;
        s_ip[0] = '\0';
        xSemaphoreGive(s_mtx);
        wake(N_WIFI);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *g = data;
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        snprintf(s_ip, sizeof s_ip, IPSTR, IP2STR(&g->ip_info.ip));
        xSemaphoreGive(s_mtx);
        wake(N_WIFI);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        uint16_t n = SCAN_MAX;
        static wifi_ap_record_t rec[SCAN_MAX];
        if (esp_wifi_scan_get_ap_records(&n, rec) != ESP_OK) n = 0;
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        int k = 0;
        for (uint16_t i = 0; i < n; i++) {                      /* records come strongest first */
            const char *ssid = (const char *)rec[i].ssid;
            if (!ssid[0]) continue;                               /* hidden network */
            bool dup = false;
            for (int j = 0; j < k; j++) if (strcmp(s_scan[j].ssid, ssid) == 0) dup = true;
            if (dup) continue;
            el_strlcpy(s_scan[k].ssid, ssid, sizeof s_scan[k].ssid);
            s_scan[k].rssi = rec[i].rssi;
            s_scan[k].secure = rec[i].authmode != WIFI_AUTH_OPEN;
            k++;
        }
        s_scan_n = k;
        s_scan_running = false;
        xSemaphoreGive(s_mtx);
        wake(N_SCAN);
    }
}

static esp_err_t radio_init(void)
{
    if (s_inited) return ESP_OK;
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    esp_err_t e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;
    s_netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "storage");   /* our settings own the credentials */
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "mode");           /* station only: no SoftAP, ever */
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL, NULL), TAG, "ev");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL, NULL), TAG, "ip");
    s_inited = true;
    return ESP_OK;
}

static esp_err_t radio_start(void)
{
    if (s_started) return ESP_OK;
    ESP_RETURN_ON_ERROR(radio_init(), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");
    s_started = true;
    storage_event_log("Wi-Fi radio on");
    return ESP_OK;
}

static void radio_stop(void)
{
    if (!s_started) return;
    if (s_sntp_on) { esp_netif_sntp_deinit(); s_sntp_on = false; }
    esp_wifi_disconnect();
    esp_wifi_stop();
    s_started = s_connecting = s_connected = false;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_ip[0] = '\0';
    xSemaphoreGive(s_mtx);
    storage_event_log("Wi-Fi radio off");
}

/* Load ssid/password into the driver when they changed. Returns true if they changed. */
static bool apply_credentials(const el_settings_t *set)
{
    if (strcmp(set->wifi_ssid, s_ap_ssid) == 0 && strcmp(set->wifi_password, s_ap_pass) == 0) return false;
    el_strlcpy(s_ap_ssid, set->wifi_ssid, sizeof s_ap_ssid);
    el_strlcpy(s_ap_pass, set->wifi_password, sizeof s_ap_pass);
    if (s_connecting || s_connected) esp_wifi_disconnect();
    s_connecting = s_connected = false;
    wifi_config_t wc;
    memset(&wc, 0, sizeof wc);
    memcpy(wc.sta.ssid, s_ap_ssid, strlen(s_ap_ssid));
    memcpy(wc.sta.password, s_ap_pass, strlen(s_ap_pass));
    /* With a password: refuse open / WEP / WPA1 networks posing under the same name. */
    wc.sta.threshold.authmode = s_ap_pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;
    wc.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wc.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    if (s_inited) esp_wifi_set_config(WIFI_IF_STA, &wc);
    el_uplink_sched_reset(&s_conn);
    return true;
}

static const char *reason_text(uint8_t r)
{
    switch (r) {
    case WIFI_REASON_NO_AP_FOUND:          return "Network not found";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:    return "Wrong Wi-Fi password";
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_AUTH_EXPIRE:          return "Network refused the logger";
    case WIFI_REASON_BEACON_TIMEOUT:       return "Wi-Fi signal lost";
    default:                               return "Wi-Fi connection lost";
    }
}

/* ---------------------------------------------------------------- SNTP (optional) */

static void sntp_synced(struct timeval *tv)
{
    time_t t = tv ? tv->tv_sec : time(NULL);
    if (t < 1767225600) return;                    /* before 2026-01-01: not plausible */
    if (rtc_set(t) == ESP_OK) {
        app_state_lock();
        app_state_raw()->rtc_valid = true;
        app_state_unlock();
        storage_event_log("clock set from internet time (SNTP)");
        app_events_post(EVT_TIME_SET, SRC_INTERNAL, 0);
    }
}

static void sntp_follow(bool want)
{
    if (want && !s_sntp_on) {
        esp_sntp_config_t c = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        c.sync_cb = sntp_synced;
        s_sntp_on = esp_netif_sntp_init(&c) == ESP_OK;
    } else if (!want && s_sntp_on) {
        esp_netif_sntp_deinit();
        s_sntp_on = false;
    }
}

/* ---------------------------------------------------------------- upload */

static void device_info(el_uplink_device_t *d, const el_settings_t *set)
{
    d->serial = set->device_serial;
    d->location = set->location;
    d->fw = fw_update_running_version();
    d->decimals = set->decimals;
}

/* POST body to the server. Returns the HTTP status, or -1 when there was no HTTP answer. */
static int post(const el_settings_t *set, const char *body, size_t len, char *err, size_t err_cap)
{
    char ua[48];
    snprintf(ua, sizeof ua, "envlogger/%s", fw_update_running_version());
    esp_http_client_config_t c = {
        .url = set->upload_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,     /* HTTPS: verify the server certificate */
        .disable_auto_redirect = true,                   /* never follow the server elsewhere */
        .keep_alive_enable = false,
        .user_agent = ua,
    };
    esp_http_client_handle_t h = esp_http_client_init(&c);
    if (!h) { snprintf(err, err_cap, "Upload: out of memory"); return -1; }
    esp_http_client_set_header(h, "Content-Type", "application/json");
    if (set->upload_key[0]) {
        char auth[16 + EL_UPLINK_TOKEN_MAX];
        snprintf(auth, sizeof auth, "Bearer %s", set->upload_key);
        esp_http_client_set_header(h, "Authorization", auth);
    }
    esp_http_client_set_post_field(h, body, (int)len);
    esp_err_t e = esp_http_client_perform(h);            /* the reply body is never read or acted on */
    int status = e == ESP_OK ? esp_http_client_get_status_code(h) : -1;
    esp_http_client_cleanup(h);
    if (status < 0) snprintf(err, err_cap, "Server not reachable (%s)", esp_err_to_name(e));
    else if (status < 200 || status > 299) snprintf(err, err_cap, "Server answered HTTP %d", status);
    else err[0] = '\0';
    return status;
}

static void uplink_step(const el_settings_t *set)
{
    if (strcmp(set->upload_url, s_url_applied) != 0 || strcmp(set->upload_key, s_key_applied) != 0) {
        el_strlcpy(s_url_applied, set->upload_url, sizeof s_url_applied);
        el_strlcpy(s_key_applied, set->upload_key, sizeof s_key_applied);
        el_uplink_sched_reset(&s_up);                    /* new address / key: try straight away */
    }
    uint32_t wm = storage_uplink_wm(), idx = 0, count = 0, base = 0;
    if (storage_log_first_after(wm, &idx, &count, &base) != EL_OK) return;
    uint32_t pending = count - idx;
    if (!set->upload_url[0] || !s_connected || !s_body) {
        publish_uplink(pending, false, false, 0, NULL);
        return;
    }
    if (!s_test_req && (pending == 0 || !el_uplink_sched_due(&s_up, now_ms()))) {
        publish_uplink(pending, false, s_up.last == EL_UPLINK_REJECTED, 0, NULL);
        return;
    }

    el_uplink_device_t dev;
    device_info(&dev, set);
    el_uplink_body_t b;
    bool test = s_test_req;
    s_test_req = false;
    el_uplink_body_begin(&b, s_body, EL_UPLINK_BODY_MAX, &dev, test);
    uint32_t last_seq = wm;
    if (!test) {
        for (uint32_t i = idx; i < count && b.n < EL_UPLINK_BATCH_MAX; i++) {
            el_result_t r;
            if (storage_log_get(i, &r) != EL_OK) {       /* damaged record: skip it, never stall */
                if (b.n == 0) last_seq = base + i;
                storage_event_log("upload: record %lu unreadable, skipped", (unsigned long)(base + i));
                continue;
            }
            if (el_uplink_body_add(&b, &r, &dev) != EL_OK) break;
            last_seq = r.seq;
        }
    }
    size_t len = 0;
    if (el_uplink_body_end(&b, &len) != EL_OK) { ESP_LOGE(TAG, "body overflow"); return; }
    if (!test && b.n == 0) {                             /* only unreadable records: move past them */
        if (last_seq != wm) storage_uplink_wm_set(last_seq);
        return;
    }

    publish_uplink(pending, true, false, 0, NULL);
    char err[48];
    int status = post(set, s_body, len, err, sizeof err);
    el_uplink_verdict_t v = el_uplink_classify(status);
    el_uplink_sched_done(&s_up, v, now_ms());
    int64_t ok_s = 0;
    if (v == EL_UPLINK_SENT) {
        ok_s = (int64_t)time(NULL);
        if (!test) {
            storage_uplink_wm_set(last_seq);
            pending -= (uint32_t)b.n;
        }
        if (test) storage_event_log("upload test: server accepted (HTTP %d)", status);
    } else {
        storage_event_log("upload %s: %s", el_uplink_verdict_name(v), err);
    }
    ESP_LOGI(TAG, "POST %u result(s)%s -> %d %s", (unsigned)b.n, test ? " (test)" : "", status, el_uplink_verdict_name(v));
    publish_uplink(pending, false, v == EL_UPLINK_REJECTED, ok_s, v == EL_UPLINK_SENT ? "" : err);
    if (v == EL_UPLINK_SENT && pending) wake(N_WAKE);     /* more to send: go again now */
}

/* ---------------------------------------------------------------- task */

static uint32_t loop_once(void)
{
    el_settings_t set;
    storage_settings_get(&set);
    bool want = set.wifi_enabled;
    bool scan_req;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    scan_req = s_scan_req;
    s_scan_req = false;
    bool scanning = s_scan_running;
    uint8_t reason = s_disc_reason;
    s_disc_reason = 0;
    bool have_ip = s_ip[0] != '\0';
    xSemaphoreGive(s_mtx);

    /* Scan on request, even with Wi-Fi off (radio up only for the scan). */
    if (scan_req && !scanning) {
        if (radio_start() == ESP_OK) {
            if (s_connecting) { esp_wifi_disconnect(); s_connecting = false; }
            wifi_scan_config_t sc = { .show_hidden = false };
            bool ok = esp_wifi_scan_start(&sc, false) == ESP_OK;
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            s_scan_running = ok;
            if (!ok) s_scan_n = 0;
            xSemaphoreGive(s_mtx);
            scanning = ok;
        }
    }

    if (!want) {
        if (!scanning) radio_stop();
        s_conn.failures = 0;
        publish(NET_OFF, "");
        uplink_step(&set);
        return NET_IDLE_MS;
    }
    if (!set.wifi_ssid[0]) {
        if (!scanning) radio_stop();
        publish(NET_NOT_SET_UP, "Choose a Wi-Fi network");
        return NET_IDLE_MS;
    }
    if (radio_start() != ESP_OK) {
        publish(NET_FAILED, "Wi-Fi radio did not start");
        return 10000;
    }
    apply_credentials(&set);

    /* connection state machine */
    uint32_t now = now_ms();
    if (have_ip && !s_connected) {
        s_connected = true;
        s_connecting = false;
        el_uplink_sched_done(&s_conn, EL_UPLINK_SENT, now);
        storage_event_log("Wi-Fi connected to \"%s\"", s_ap_ssid);
    }
    if (reason && (s_connected || s_connecting)) {
        const char *why = reason_text(reason);
        if (s_connected) storage_event_log("Wi-Fi lost: %s (%u)", why, reason);
        s_connected = s_connecting = false;
        el_uplink_sched_done(&s_conn, EL_UPLINK_RETRY, now);
        publish(NET_FAILED, why);
    }
    if (s_connecting && now - s_connect_t0 > CONNECT_TIMEOUT_MS) {
        esp_wifi_disconnect();
        s_connecting = false;
        el_uplink_sched_done(&s_conn, EL_UPLINK_RETRY, now);
        publish(NET_FAILED, "No answer from the network");
    }
    if (!s_connected && !s_connecting && !scanning && el_uplink_sched_due(&s_conn, now)) {
        if (esp_wifi_connect() == ESP_OK) {
            s_connecting = true;
            s_connect_t0 = now;
        } else {
            el_uplink_sched_done(&s_conn, EL_UPLINK_RETRY, now);
        }
    }
    if (s_connected) publish(NET_CONNECTED, NULL);
    else if (s_connecting) publish(NET_CONNECTING, NULL);

    sntp_follow(s_connected && set.wifi_time_sync);
    uplink_step(&set);

    uint32_t wait = NET_IDLE_MS;
    if (!s_connected && !s_connecting) {
        uint32_t w = el_uplink_sched_wait_ms(&s_conn, now_ms());
        if (w && w < wait) wait = w;
        if (!w) wait = 100;
    }
    if (s_connected && s_up.waiting) {
        uint32_t w = el_uplink_sched_wait_ms(&s_up, now_ms());
        if (w < wait) wait = w ? w : 100;
    }
    return wait;
}

static void net_task(void *arg)
{
    (void)arg;
    uint32_t wait = 0;
    for (;;) {
        uint32_t bits = 0;
        xTaskNotifyWait(0, UINT32_MAX, &bits, pdMS_TO_TICKS(wait));
        wait = loop_once();
    }
}

static void listener(const app_event_t *e, void *ctx)
{
    (void)ctx;
    switch (e->id) {
    case EVT_SETTINGS_CHANGED:
    case EVT_LOG_CHANGED:
        wake(N_WAKE);
        break;
    default:
        break;
    }
}

esp_err_t net_start(void)
{
    if (s_task) return ESP_OK;
    s_mtx = xSemaphoreCreateMutex();
    if (!s_mtx) return ESP_ERR_NO_MEM;
    s_body = heap_caps_malloc(EL_UPLINK_BODY_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_body) s_body = malloc(EL_UPLINK_BODY_MAX);
    el_uplink_sched_reset(&s_conn);
    el_uplink_sched_reset(&s_up);
    app_events_add_listener(listener, NULL);
    if (xTaskCreatePinnedToCore(net_task, "net", NET_STACK, NULL, NET_PRIO, &s_task, 0) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

bool net_scan_start(void)
{
    if (!s_task) return false;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    bool busy = s_scan_running || s_scan_req;
    if (!busy) { s_scan_req = true; s_scan_n = -1; }
    xSemaphoreGive(s_mtx);
    if (!busy) wake(N_SCAN);
    return !busy;
}

int net_scan_results(net_ap_t *out, int max)
{
    if (!s_mtx) return 0;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int n = s_scan_n;
    if (n > 0 && out) {
        if (n > max) n = max;
        memcpy(out, s_scan, (size_t)n * sizeof *out);
    }
    xSemaphoreGive(s_mtx);
    return n;
}

void net_send_test(void)
{
    s_test_req = true;
    wake(N_WAKE);
}
