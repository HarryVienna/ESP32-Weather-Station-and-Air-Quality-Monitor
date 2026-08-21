#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "sdkconfig.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_sntp.h"
#include "esp_timer.h"

#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_event.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "gui/status/gui_status.h"

#include "config/config.h"
#include "network.h"

#define MAC_STR_LEN 18

static const char* TAG = "WIFI";

/* Set once in wifi_init() from its time_synced_cb parameter, read from
 * sync_callback() further below (NTP time synchronization section). */
static wifi_time_synced_cb_t s_time_synced_cb = NULL;

char* get_mac_string(const uint8_t *mac_addr, char *macStrBuffer) {
    snprintf(macStrBuffer, MAC_STR_LEN, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    return macStrBuffer;
}


/* ============================================================================
 * WiFi manager: single-owner state machine
 *
 * network_task() is the ONLY code allowed to call esp_wifi_*. Every other
 * piece of the app - the setup screen's Scan/Connect buttons, boot-time
 * auto-connect, and WiFi's own async events (disconnected/got IP) - posts a
 * message into s_queue instead of touching the driver directly. Because
 * network_task() drains that queue one message at a time, "a scan is
 * running while a connect is being attempted" is structurally impossible,
 * not something to remember with a flag - there used to be three separate
 * places calling esp_wifi_start/stop/connect/disconnect (event_handler(),
 * a per-scan task, a per-connect task) coordinated only by static booleans,
 * which is what caused the repeated scan/connect races this replaces.
 *
 * event_handler() itself does no WiFi work anymore - it just translates an
 * ESP-IDF event into a message and returns immediately, so it never blocks
 * the shared "sys_evt" task (the old code did a vTaskDelay() in there).
 * ============================================================================ */

#define WIFI_CONNECT_MAX_RETRIES 10

typedef enum {
    WIFI_ST_IDLE,          // driver stopped, nothing happening
    WIFI_ST_CONNECTING,    // esp_wifi_connect() issued, bounded retries, done_cb pending
    WIFI_ST_CONNECTED,     // has an IP
    WIFI_ST_RECONNECTING,  // was CONNECTED, link dropped, unbounded retries, no done_cb
    WIFI_ST_SCANNING,      // scan in flight
    WIFI_ST_COUNT,
} wifi_mgr_state_t;

typedef enum {
    NET_MSG_CONNECT,
    NET_MSG_SCAN,
    NET_MSG_EV_STA_START,
    NET_MSG_EV_STA_DISCONNECTED,
    NET_MSG_EV_GOT_IP,
    NET_MSG_CONNECT_TIMEOUT,
    NET_MSG_COUNT,
} net_msg_type_t;

typedef struct {
    net_msg_type_t type;
    union {
        struct {
            char ssid[33];
            char password[65];
            wificonnect_done_cb_t done_cb;
        } connect;
        struct {
            wifiscan_done_cb_t done_cb;
        } scan;
        struct {
            uint8_t reason;
            int8_t rssi;
        } disconnected;
        struct {
            int8_t rssi;
        } got_ip;
    };
} net_msg_t;

static QueueHandle_t s_queue;

/* Safety net for on_connect()'s "already started - disconnect and let the
 * resulting event drive the real esp_wifi_connect()" strategy (and the
 * bounded retry loop that follows it, on_disconnected_connecting()):
 * esp_wifi is closed-source, so whether a disconnect issued against an
 * in-flight connect attempt is *guaranteed* to always produce a
 * WIFI_EVENT_STA_DISCONNECTED can't be confirmed from source. If no WiFi
 * event at all arrives within CONNECT_WATCHDOG_TIMEOUT_US while a
 * connect_done_cb is still waiting, give up cleanly instead of leaving the
 * UI spinner stuck forever - armed/disarmed wherever connect_done_cb
 * starts, keeps, or stops waiting (see arm_connect_watchdog()/
 * disarm_connect_watchdog()), so it makes the question moot regardless of
 * the actual driver behavior.*/
#define CONNECT_WATCHDOG_TIMEOUT_US (20 * 1000 * 1000)

static esp_timer_handle_t s_connect_watchdog;

/* Timer callback - runs on the esp_timer task, must not touch WiFi state
 * directly (same reasoning as event_handler() below: only network_task()
 * is allowed to). */
static void connect_watchdog_cb(void *arg)
{
    net_msg_t msg = { .type = NET_MSG_CONNECT_TIMEOUT };
    xQueueSend(s_queue, &msg, 0);
}

static void arm_connect_watchdog(void)
{
    esp_timer_stop(s_connect_watchdog); // harmless if not currently running
    esp_timer_start_once(s_connect_watchdog, CONNECT_WATCHDOG_TIMEOUT_US);
}

static void disarm_connect_watchdog(void)
{
    esp_timer_stop(s_connect_watchdog); // harmless if not currently running
}

/* Human-readable short description of the most common WIFI_REASON_* codes
 * (see esp_wifi_types_generic.h) - helps judge whether a slow/failing
 * connect is due to the password, the router, or poor reception, instead
 * of just seeing the bare number. */
static const char* wifi_disconnect_reason_str(uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_UNSPECIFIED: return "unspecified";
        case WIFI_REASON_AUTH_EXPIRE: return "auth expired";
        case WIFI_REASON_AUTH_LEAVE: return "auth leave";
        case WIFI_REASON_DISASSOC_DUE_TO_INACTIVITY: return "disassoc (inactivity)";
        case WIFI_REASON_ASSOC_LEAVE: return "assoc leave (STA disconnected)";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4-way handshake timeout (falsches Passwort?)";
        case WIFI_REASON_MIC_FAILURE: return "MIC failure";
        case WIFI_REASON_BEACON_TIMEOUT: return "beacon timeout (schlechter Empfang)";
        case WIFI_REASON_NO_AP_FOUND: return "AP nicht gefunden";
        case WIFI_REASON_AUTH_FAIL: return "Authentifizierung fehlgeschlagen (falsches Passwort?)";
        case WIFI_REASON_ASSOC_FAIL: return "Assoziation fehlgeschlagen";
        case WIFI_REASON_HANDSHAKE_TIMEOUT: return "Handshake-Timeout";
        case WIFI_REASON_CONNECTION_FAIL: return "Verbindung fehlgeschlagen";
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY: return "kein AP mit passender Sicherheit gefunden";
        case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD: return "AP-Signal zu schwach (RSSI-Schwelle)";
        default: return "unbekannt";
    }
}

/* Non-blocking on purpose - runs on the shared "sys_evt" task, so it must
 * never wait on anything. All the actual logic lives in network_task(). */
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    net_msg_t msg = {0};

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        msg.type = NET_MSG_EV_STA_START;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)event_data;
        msg.type = NET_MSG_EV_STA_DISCONNECTED;
        msg.disconnected.reason = d->reason;
        msg.disconnected.rssi = d->rssi;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifi_ap_record_t ap_info;
        int8_t rssi = 0;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            rssi = ap_info.rssi;
        }
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        msg.type = NET_MSG_EV_GOT_IP;
        msg.got_ip.rssi = rssi;
    } else {
        return;
    }

    xQueueSend(s_queue, &msg, 0);
}

/* Per-connection-attempt state, owned exclusively by network_task() and
 * passed to each transition function below - nothing outside this task
 * ever reads or writes it, which is what makes the whole thing race-free. */
typedef struct {
    int retry_num;
    wificonnect_done_cb_t connect_done_cb;
    wifiscan_done_cb_t scan_done_cb;
    bool resume_after_scan;
} wifi_ctx_t;

/* Every transition function has this shape, regardless of which (state,
 * event) cell(s) of s_transitions[][] it's plugged into below - some
 * genuinely need to know which state they were called from (on_connect(),
 * on_scan()), most don't. */
typedef wifi_mgr_state_t (*transition_fn_t)(wifi_ctx_t *ctx, const net_msg_t *msg, wifi_mgr_state_t state);

static wifi_mgr_state_t on_connect(wifi_ctx_t *ctx, const net_msg_t *msg, wifi_mgr_state_t state);
static wifi_mgr_state_t on_scan(wifi_ctx_t *ctx, const net_msg_t *msg, wifi_mgr_state_t state);
static wifi_mgr_state_t on_sta_start_connecting(wifi_ctx_t *ctx, const net_msg_t *msg, wifi_mgr_state_t state);
static wifi_mgr_state_t on_scan_ready(wifi_ctx_t *ctx, const net_msg_t *msg, wifi_mgr_state_t state);
static wifi_mgr_state_t on_disconnected_connecting(wifi_ctx_t *ctx, const net_msg_t *msg, wifi_mgr_state_t state);
static wifi_mgr_state_t on_disconnected_connected(wifi_ctx_t *ctx, const net_msg_t *msg, wifi_mgr_state_t state);
static wifi_mgr_state_t on_got_ip(wifi_ctx_t *ctx, const net_msg_t *msg, wifi_mgr_state_t state);
static wifi_mgr_state_t on_connect_timeout(wifi_ctx_t *ctx, const net_msg_t *msg, wifi_mgr_state_t state);

/*                          CONNECT      SCAN      STA_START               STA_DISCONNECTED          GOT_IP     CONNECT_TIMEOUT
 *   IDLE                  on_connect   on_scan        -                         -                       -              -
 *   CONNECTING            on_connect   on_scan   on_sta_start_connecting  on_disconnected_connecting  on_got_ip  on_connect_timeout
 *   CONNECTED             on_connect   on_scan        -                   on_disconnected_connected   on_got_ip        -
 *   RECONNECTING          on_connect   on_scan        -                   on_disconnected_connected   on_got_ip        -
 *   SCANNING              on_connect   on_scan   on_scan_ready            on_scan_ready                -              -
 *
 * "-" cells are NULL (zero-initialized) - the event is a no-op in that
 * state (e.g. a stray STA_START while already CONNECTED). This table *is*
 * the state machine: to know what happens for a given (state, event), look
 * it up here, not by re-deriving it from a chain of flags. */
static const transition_fn_t s_transitions[WIFI_ST_COUNT][NET_MSG_COUNT] = {
    [WIFI_ST_IDLE] = {
        [NET_MSG_CONNECT] = on_connect,
        [NET_MSG_SCAN]    = on_scan,
    },
    [WIFI_ST_CONNECTING] = {
        [NET_MSG_CONNECT]             = on_connect,
        [NET_MSG_SCAN]                = on_scan,
        [NET_MSG_EV_STA_START]        = on_sta_start_connecting,
        [NET_MSG_EV_STA_DISCONNECTED] = on_disconnected_connecting,
        [NET_MSG_EV_GOT_IP]           = on_got_ip,
        [NET_MSG_CONNECT_TIMEOUT]     = on_connect_timeout,
    },
    [WIFI_ST_CONNECTED] = {
        [NET_MSG_CONNECT]             = on_connect,
        [NET_MSG_SCAN]                = on_scan,
        [NET_MSG_EV_STA_DISCONNECTED] = on_disconnected_connected,
        [NET_MSG_EV_GOT_IP]           = on_got_ip,
    },
    [WIFI_ST_RECONNECTING] = {
        [NET_MSG_CONNECT]             = on_connect,
        [NET_MSG_SCAN]                = on_scan,
        [NET_MSG_EV_STA_DISCONNECTED] = on_disconnected_connected,
        [NET_MSG_EV_GOT_IP]           = on_got_ip,
    },
    [WIFI_ST_SCANNING] = {
        [NET_MSG_CONNECT]             = on_connect,
        [NET_MSG_SCAN]                = on_scan,
        [NET_MSG_EV_STA_START]        = on_scan_ready,
        [NET_MSG_EV_STA_DISCONNECTED] = on_scan_ready,
    },
};

/* CONNECT column - every state routes here, since "handle a connect
 * request" always needs the same fresh-credentials bookkeeping, just
 * shaped a little differently depending on where it's coming from. */
static wifi_mgr_state_t on_connect(wifi_ctx_t *ctx, const net_msg_t *msg, wifi_mgr_state_t state)
{
    if (state == WIFI_ST_SCANNING) {
        // Exceedingly unlikely (Connect pressed mid-scan on the same
        // screen) - reject rather than corrupt the pending scan.
        ESP_LOGW(TAG, "Ignoring connect request while scanning");
        if (msg->connect.done_cb) {
            msg->connect.done_cb(false);
        }
        return state;
    }

    if (state == WIFI_ST_CONNECTING && ctx->connect_done_cb) {
        // A new connect attempt supersedes whatever was pending.
        wificonnect_done_cb_t old_cb = ctx->connect_done_cb;
        ctx->connect_done_cb = NULL;
        old_cb(false);
    }

    ESP_LOGI(TAG, "connecting to ap SSID: >%s<", msg->connect.ssid);

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, msg->connect.ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, msg->connect.password, sizeof(wifi_config.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ctx->connect_done_cb = msg->connect.done_cb;
    ctx->retry_num = 0;

    if (state == WIFI_ST_IDLE) {
        ESP_ERROR_CHECK(esp_wifi_start());
        // esp_wifi_connect() happens once STA_START confirms the driver is
        // actually ready - see on_sta_start_connecting().
    } else {
        // Already started (CONNECTING/CONNECTED/RECONNECTING) - disconnect
        // and let the resulting STA_DISCONNECTED drive the real
        // esp_wifi_connect() via on_disconnected_connecting(), rather than
        // also connecting here and doubling up with that.
        esp_wifi_disconnect();
    }
    arm_connect_watchdog();
    return WIFI_ST_CONNECTING;
}

/* SCAN column - every state routes here too, for the same reason: "start a
 * scan" always means the same bookkeeping (remember whether to resume the
 * connection afterwards), just a different way of freeing up the radio
 * depending on what it was doing before. */
static wifi_mgr_state_t on_scan(wifi_ctx_t *ctx, const net_msg_t *msg, wifi_mgr_state_t state)
{
    if (state == WIFI_ST_SCANNING) {
        ESP_LOGW(TAG, "Scan already in progress, ignoring");
        return state;
    }

    if (state == WIFI_ST_CONNECTING && ctx->connect_done_cb) {
        // Scanning cancels an in-flight connect attempt rather than leaving
        // its done_cb (and the UI spinner waiting on it) hanging forever.
        disarm_connect_watchdog();
        wificonnect_done_cb_t old_cb = ctx->connect_done_cb;
        ctx->connect_done_cb = NULL;
        old_cb(false);
    }

    ctx->scan_done_cb = msg->scan.done_cb;
    ctx->resume_after_scan = (state == WIFI_ST_CONNECTED || state == WIFI_ST_RECONNECTING);

    if (state == WIFI_ST_IDLE) {
        ESP_ERROR_CHECK(esp_wifi_start());
        // esp_wifi_scan_start() happens once STA_START confirms the driver
        // is ready - see on_scan_ready().
    } else {
        // The radio was doing something (connecting/connected/reconnecting)
        // - show that as disconnected for as long as the scan borrows the
        // radio, instead of leaving the old status up while nothing is
        // actually connected.
        disp_wifi_status(false, 0);
        esp_wifi_disconnect();
        // esp_wifi_scan_start() happens once the disconnect actually lands
        // - see on_scan_ready().
    }
    return WIFI_ST_SCANNING;
}

static wifi_mgr_state_t on_sta_start_connecting(wifi_ctx_t *ctx, const net_msg_t *msg, wifi_mgr_state_t state)
{
    esp_wifi_connect();
    return WIFI_ST_CONNECTING;
}

/* Reached from either STA_START (radio was IDLE, just started) or
 * STA_DISCONNECTED (radio was already running, just disconnected) - both
 * simply mean "the radio has settled, it's safe to scan now". Doing the
 * actual scan here, synchronously, replaces the old code's blind
 * 1-second guess at how long that settling takes. */
static wifi_mgr_state_t on_scan_ready(wifi_ctx_t *ctx, const net_msg_t *msg, wifi_mgr_state_t state)
{
    // esp_wifi_scan_start(..., true) blocks internally until the scan is
    // done, so the whole scan+results+resume happens right here - no
    // separate "scan done" message needed.
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 1000,
        .scan_time.active.max = 5000,
    };

    uint16_t ap_count = 16;
    esp_wifi_scan_start(&scan_config, true);
    esp_wifi_scan_get_ap_records(&ap_count, NULL);

    wifi_ap_record_t *ap_records = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
    esp_wifi_scan_get_ap_records(&ap_count, ap_records);

    char allNetworks[4096] = {0};
    for (uint16_t i = 0; i < ap_count; i++) {
        if (strlen((const char *)ap_records[i].ssid) > 0) {
            char item[128];
            snprintf(item, sizeof(item), "%s (%d) %s", (const char *)ap_records[i].ssid,
                     ap_records[i].rssi, (ap_records[i].authmode == WIFI_AUTH_OPEN) ? "" : "*");
            ESP_LOGI(TAG, "%s", item);

            strlcat(allNetworks, (const char *)ap_records[i].ssid, sizeof(allNetworks));
            if (i != ap_count - 1) {
                strlcat(allNetworks, "\n", sizeof(allNetworks));
            }
        }
    }
    free(ap_records);

    wifi_mgr_state_t next_state;
    if (ctx->resume_after_scan) {
        esp_wifi_connect();
        next_state = WIFI_ST_RECONNECTING;
    } else {
        esp_wifi_stop();
        next_state = WIFI_ST_IDLE;
    }

    if (ctx->scan_done_cb) {
        wifiscan_done_cb_t cb = ctx->scan_done_cb;
        ctx->scan_done_cb = NULL;
        cb(allNetworks);
    }
    return next_state;
}

/* Bounded retries - this is the only state where "give up" is a valid
 * outcome, because it's the only one with a done_cb waiting on a result. */
static wifi_mgr_state_t on_disconnected_connecting(wifi_ctx_t *ctx, const net_msg_t *msg, wifi_mgr_state_t state)
{
    ESP_LOGW(TAG, "WiFi disconnected: reason=%d (%s), rssi=%d",
             msg->disconnected.reason, wifi_disconnect_reason_str(msg->disconnected.reason),
             msg->disconnected.rssi);

    ctx->retry_num++;
    if (ctx->retry_num < WIFI_CONNECT_MAX_RETRIES) {
        ESP_LOGI(TAG, "retry to connect to the AP (%d/%d)", ctx->retry_num, WIFI_CONNECT_MAX_RETRIES);
        // Brief pause before retrying: some routers/APs (band steering,
        // mesh) haven't cleared the old association entry for this station
        // yet if connect() is called again immediately - this shows up as
        // auth-expired/4-way-handshake-timeout despite good RSSI.
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_wifi_connect();
        arm_connect_watchdog();
        return WIFI_ST_CONNECTING;
    }

    ESP_LOGI(TAG, "connect to the AP fail");
    disarm_connect_watchdog();
    disp_wifi_status(false, 0);
    esp_wifi_stop();
    if (ctx->connect_done_cb) {
        wificonnect_done_cb_t cb = ctx->connect_done_cb;
        ctx->connect_done_cb = NULL;
        cb(false);
    }
    return WIFI_ST_IDLE;
}

/* Unbounded retries - once we've ever been connected, a later drop (router
 * reboot, brief interference) must never be treated as "give up". Serves
 * both CONNECTED and RECONNECTING: the action is identical either way. */
static wifi_mgr_state_t on_disconnected_connected(wifi_ctx_t *ctx, const net_msg_t *msg, wifi_mgr_state_t state)
{
    ESP_LOGW(TAG, "WiFi disconnected: reason=%d (%s), rssi=%d",
             msg->disconnected.reason, wifi_disconnect_reason_str(msg->disconnected.reason),
             msg->disconnected.rssi);

    disp_wifi_status(false, 0);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_wifi_connect();
    return WIFI_ST_RECONNECTING;
}

/* Serves CONNECTING (the initial connect succeeded - fire done_cb),
 * CONNECTED (a DHCP renewal regot an IP - just refresh the status icon,
 * done_cb is already NULL) and RECONNECTING (the background reconnect
 * succeeded - same as CONNECTED, done_cb already NULL) alike. */
static wifi_mgr_state_t on_got_ip(wifi_ctx_t *ctx, const net_msg_t *msg, wifi_mgr_state_t state)
{
    disarm_connect_watchdog();
    disp_wifi_status(true, msg->got_ip.rssi);
    ctx->retry_num = 0;
    if (ctx->connect_done_cb) {
        wificonnect_done_cb_t cb = ctx->connect_done_cb;
        ctx->connect_done_cb = NULL;
        cb(true);
    }
    return WIFI_ST_CONNECTED;
}

/* Fires CONNECT_WATCHDOG_TIMEOUT_US after arm_connect_watchdog() if nothing
 * disarmed or re-armed it in the meantime - see the comment on
 * CONNECT_WATCHDOG_TIMEOUT_US above for what this guards against. */
static wifi_mgr_state_t on_connect_timeout(wifi_ctx_t *ctx, const net_msg_t *msg, wifi_mgr_state_t state)
{
    if (!ctx->connect_done_cb) {
        // Already resolved through the normal path in the meantime - a
        // stale timer racing the queue, nothing to do.
        return state;
    }

    ESP_LOGW(TAG, "No WiFi event within %ds of a connect attempt - giving up",
             CONNECT_WATCHDOG_TIMEOUT_US / 1000000);
    esp_wifi_stop();
    wificonnect_done_cb_t cb = ctx->connect_done_cb;
    ctx->connect_done_cb = NULL;
    cb(false);
    return WIFI_ST_IDLE;
}

static void network_task(void *arg)
{
    wifi_ctx_t ctx = {0};
    wifi_mgr_state_t state = WIFI_ST_IDLE;

    net_msg_t msg;
    while (1) {
        if (xQueueReceive(s_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        transition_fn_t fn = s_transitions[state][msg.type];
        if (fn) {
            state = fn(&ctx, &msg, state);
        } else {
            ESP_LOGD(TAG, "Ignoring message %d in state %d", msg.type, state);
        }
    }
}


/**
 * @brief Initialize WIFI
 *
 * This function initializes the WIFI driver.
 */
esp_err_t wifi_init(wifi_time_synced_cb_t time_synced_cb) {
    ESP_LOGI(TAG, "Install WIFI driver");

    s_time_synced_cb = time_synced_cb;

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "Failed to erase nvs flash");
        ret = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to init nvs flash");

    // Initialize the underlying TCP/IP stack
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Failed to init netif");

    // Create default event loop
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Failed to create event loop");

    // Creates default WIFI STA
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    ESP_RETURN_ON_FALSE(sta_netif, ESP_FAIL, TAG, "Failed to create default WiFi STA netif");

    // Set hostname
    ESP_RETURN_ON_ERROR(esp_netif_set_hostname(sta_netif, HOST_NAME), TAG, "Failed to set hostname");

    //  Init WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "Failed to init wifi");

    // Set the WiFi operating mode
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Failed to set mode");

    // Set storage to RAM
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "Failed to set storage");

    s_queue = xQueueCreate(8, sizeof(net_msg_t));
    ESP_RETURN_ON_FALSE(s_queue, ESP_ERR_NO_MEM, TAG, "Failed to create WiFi manager queue");

    const esp_timer_create_args_t watchdog_args = {
        .callback = &connect_watchdog_cb,
        .name = "wifi_connect_wd",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&watchdog_args, &s_connect_watchdog),
                        TAG, "Failed to create connect watchdog timer");

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT,
                                                              ESP_EVENT_ANY_ID,
                                                              &event_handler,
                                                              NULL,
                                                              NULL),
                         TAG, "Failed to register WIFI_EVENT handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT,
                                                              IP_EVENT_STA_GOT_IP,
                                                              &event_handler,
                                                              NULL,
                                                              NULL),
                         TAG, "Failed to register IP_EVENT handler");

    xTaskCreatePinnedToCore(
        network_task,
        "WiFi Manager Task",
        16000,   // covers the scan path's char allNetworks[4096] local buffer
        NULL,
        16,
        NULL,
        0
    );

    return ESP_OK;
}

void wifi_connect_start(const char *ssid, const char *password, wificonnect_done_cb_t on_done) {
    net_msg_t msg = {
        .type = NET_MSG_CONNECT,
        .connect.done_cb = on_done,
    };
    strlcpy(msg.connect.ssid, ssid, sizeof(msg.connect.ssid));
    strlcpy(msg.connect.password, password, sizeof(msg.connect.password));
    xQueueSend(s_queue, &msg, portMAX_DELAY);
}

void wifiscan_start(wifiscan_done_cb_t on_done) {
    net_msg_t msg = {
        .type = NET_MSG_SCAN,
        .scan.done_cb = on_done,
    };
    xQueueSend(s_queue, &msg, portMAX_DELAY);
}


/* ============================================================================
 * NTP time synchronization (after a successful WiFi connect)
 * ============================================================================ */

static SemaphoreHandle_t sync_semaphore;
static bool sntp_started = false;

/**
 * @brief Callback function from time sync
 *
 * Fires once for the initial sync (see wifi_sync_time()) and again on
 * every periodic resync afterwards - SNTP_OPMODE_POLL keeps this running
 * in the background for the device's entire uptime, not just at startup.
 */
static void sync_callback(struct timeval *tv) {
    ESP_LOGI(TAG, "Syncing date/time: %s", ctime(&tv->tv_sec));
    xSemaphoreGive(sync_semaphore);

    if (s_time_synced_cb) {
        s_time_synced_cb();
    }
}

void wifi_sync_time(void)
{
    if (sntp_started) {
        // esp_sntp_init() may only be called once - if "Connect" is
        // clicked multiple times (e.g. wrong password first, then the
        // right one), the time is already synchronized from the first
        // successful call.
        return;
    }
    sntp_started = true;

    sync_semaphore = xSemaphoreCreateBinary();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SERVER);
    sntp_set_time_sync_notification_cb(&sync_callback);
    esp_sntp_init();

    // Wait for the sync_callback to give the semaphore (bounded: an unreachable
    // NTP server must not block the caller forever)
    if (xSemaphoreTake(sync_semaphore, pdMS_TO_TICKS(10000)) == pdTRUE) {
        ESP_LOGI(TAG, "Time synchronization successful");
    } else {
        ESP_LOGW(TAG, "Time synchronization timeout");
    }
}
