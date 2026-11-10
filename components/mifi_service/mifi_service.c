// SPDX-License-Identifier: Apache-2.0

#include "mifi_service.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/esp_mosaico.h"
#include "bsp/power.h"
#include "dhcpserver/dhcpserver.h"
#include "dhcpserver/dhcpserver_options.h"
#include "lwip/def.h"
#include "lwip/ip4_addr.h"
#include "driver/temperature_sensor.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "iot_eth.h"
#include "iot_eth_netif_glue.h"
#include "iot_usbh_cdc.h"
#include "iot_usbh_rndis.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "usb/usb_host.h"

static const char *TAG = "mifi_service";

#define MIFI_NVS_NAMESPACE "mifi_cfg"
#define MIFI_MAX_BLOCKED   16
#define MIFI_DHCP_LEASES   16
#define DHCPS_OFFER_DNS    0x02
#define MIFI_DIAL_RETRY_US (15000000LL)

typedef struct {
    bool active;
    uint8_t mac[6];
    uint16_t aid;
    esp_ip4_addr_t ip;
    char hostname[32];
    int64_t connect_us;
    uint64_t used_bytes;
} station_slot_t;

/* Hostname and IP last reported for one MAC's own DHCP lease. */
typedef struct {
    bool used;
    uint8_t mac[6];
    esp_ip4_addr_t ip;
    char hostname[32];
} dhcp_lease_t;

typedef struct {
    SemaphoreHandle_t lock;
    app_mifi_settings_t settings;
    app_mifi_telemetry_t telemetry;
    bool settings_dirty;
    bool redial_requested;
    bool probe_requested;
    bool display_sleeping;
    int64_t last_activity_us;

    esp_netif_t *ap_netif;
    esp_netif_t *rndis_netif;
    iot_eth_driver_t *rndis_drv;
    iot_eth_handle_t eth_handle;
    iot_eth_netif_glue_handle_t eth_glue;

    usbh_cdc_port_handle_t at_port;
    bool at_ready;
    uint8_t modem_dev_addr;
    uint16_t modem_vid;
    uint16_t modem_pid;
    int64_t last_dial_us;
    uint32_t dial_attempts;
    char at_rx_buf[256];
    size_t at_rx_len;

    temperature_sensor_handle_t tsens;
    station_slot_t stations[APP_MIFI_MAX_STATIONS];
    dhcp_lease_t leases[MIFI_DHCP_LEASES];
    char blocked_macs[MIFI_MAX_BLOCKED][18];
    size_t blocked_count;

    esp_err_t (*orig_rndis_transmit)(iot_eth_driver_t *driver, uint8_t *data, size_t len);
    iot_eth_mediator_t *orig_rndis_mediator;
    iot_eth_mediator_t wrapped_mediator;
    atomic_uint_least32_t rx_bytes_accum;
    atomic_uint_least32_t tx_bytes_accum;
    uint64_t total_wan_bytes;
    int64_t last_rate_us;
} mifi_service_t;

static mifi_service_t s_svc;

static void format_mac_str(const uint8_t mac[6], char out[18])
{
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static bool is_mac_blocked_locked(const char *mac_str)
{
    for (size_t i = 0; i < s_svc.blocked_count; ++i) {
        if (strcasecmp(s_svc.blocked_macs[i], mac_str) == 0) {
            return true;
        }
    }
    return false;
}

static uint8_t guess_device_kind(const char *hostname, const uint8_t mac[6])
{
    if (hostname && hostname[0]) {
        if (strcasestr(hostname, "MacBook") || strcasestr(hostname, "PC") ||
            strcasestr(hostname, "Desktop") || strcasestr(hostname, "Laptop")) {
            return 1; /* laptop */
        }
        if (strcasestr(hostname, "iPad") || strcasestr(hostname, "Tab")) {
            return 2; /* tablet */
        }
        if (strcasestr(hostname, "Watch")) {
            return 3; /* watch */
        }
        if (strcasestr(hostname, "Cam") || strcasestr(hostname, "IPC")) {
            return 4; /* camera */
        }
    }
    (void)mac;
    return 0; /* phone */
}

static wifi_auth_mode_t map_wifi_auth(uint8_t ui_auth)
{
    switch (ui_auth) {
    case 0:
        return WIFI_AUTH_OPEN;
    case 1:
        return WIFI_AUTH_WPA2_PSK;
    case 2:
        return WIFI_AUTH_WPA3_PSK;
    case 3:
        return WIFI_AUTH_WPA_WPA2_PSK;
    case 4:
        return WIFI_AUTH_WPA2_WPA3_PSK;
    default:
        return WIFI_AUTH_WPA2_PSK;
    }
}

static uint8_t map_max_conn(uint8_t mode)
{
    if (mode == 0) {
        return 4;
    }
    if (mode == 2) {
        return 10;
    }
    return 8;
}

static uint32_t sleep_timeout_sec(uint8_t sleep_mode)
{
    switch (sleep_mode) {
    case 0:
        return 15;
    case 1:
        return 30;
    case 2:
        return 60;
    case 3:
        return 300;
    default:
        return 0; /* Never */
    }
}

static void default_settings(app_mifi_settings_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->ssid, sizeof(cfg->ssid), "Mosaico_4G");
    snprintf(cfg->password, sizeof(cfg->password), "mifi2026");
    cfg->auth_mode = 4;      /* WPA2/WPA3-Personal */
    cfg->channel_mode = 6;   /* Channel 6 */
    cfg->actual_channel = 6;
    cfg->max_conn_mode = 1;  /* 8 stations */
    cfg->hotspot_enabled = true;
    cfg->all_paused = false;
    cfg->power_save = false;
    cfg->roaming = false;
    cfg->brightness = 70;
    cfg->sleep_mode = 4;     /* Never (Always on by default) */
    cfg->schedule_mode = 0;
}

static void format_wan_usage(uint64_t bytes, char *out, size_t cap)
{
    if (bytes == 0) {
        snprintf(out, cap, "0 KB");
    } else if (bytes < 1024ULL * 1024ULL) {
        uint32_t kb = (uint32_t)((bytes + 1023ULL) / 1024ULL);
        snprintf(out, cap, "%u KB", (unsigned)kb);
    } else if (bytes < 1024ULL * 1024ULL * 1024ULL) {
        uint64_t mb_tenths = (bytes * 10ULL) / (1024ULL * 1024ULL);
        snprintf(out, cap, "%u.%u MB",
                 (unsigned)(mb_tenths / 10ULL),
                 (unsigned)(mb_tenths % 10ULL));
    } else {
        uint64_t gb_hundredths =
            (bytes * 100ULL) / (1024ULL * 1024ULL * 1024ULL);
        snprintf(out, cap, "%u.%02u GB",
                 (unsigned)(gb_hundredths / 100ULL),
                 (unsigned)(gb_hundredths % 100ULL));
    }
}

static void load_settings_from_nvs(app_mifi_settings_t *cfg)
{
    default_settings(cfg);
    nvs_handle_t nvs = 0;
    if (nvs_open(MIFI_NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return;
    }
    size_t len = sizeof(*cfg);
    if (nvs_get_blob(nvs, "settings_v2", cfg, &len) != ESP_OK ||
        len != sizeof(*cfg)) {
        default_settings(cfg);
    }
    uint64_t saved_bytes = 0;
    if (nvs_get_u64(nvs, "wan_bytes_v2", &saved_bytes) == ESP_OK) {
        s_svc.total_wan_bytes = saved_bytes;
    }
    nvs_close(nvs);
}

static void save_wan_bytes_to_nvs(uint64_t bytes)
{
    nvs_handle_t nvs = 0;
    if (nvs_open(MIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    nvs_set_u64(nvs, "wan_bytes_v2", bytes);
    nvs_commit(nvs);
    nvs_close(nvs);
}

static void save_settings_to_nvs(const app_mifi_settings_t *cfg)
{
    nvs_handle_t nvs = 0;
    if (nvs_open(MIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    nvs_set_blob(nvs, "settings_v2", cfg, sizeof(*cfg));
    nvs_commit(nvs);
    nvs_close(nvs);
}

/* Automatic Channel Selection (ACS) across channels 1, 6, 11 */
static uint8_t select_cleanest_2g_channel(void)
{
    wifi_mode_t prev_mode = WIFI_MODE_AP;
    esp_wifi_get_mode(&prev_mode);
    if (prev_mode == WIFI_MODE_AP) {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
    }
    wifi_scan_config_t scan_cfg = {
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 20,
        .scan_time.active.max = 50,
    };
    uint8_t best_channel = 6;
    if (esp_wifi_scan_start(&scan_cfg, true) == ESP_OK) {
        uint16_t ap_num = 24;
        wifi_ap_record_t *records = calloc(ap_num, sizeof(wifi_ap_record_t));
        if (records &&
            esp_wifi_scan_get_ap_records(&ap_num, records) == ESP_OK) {
            const uint8_t candidates[3] = {1, 6, 11};
            int32_t best_score = 0x7FFFFFFF;
            for (int c = 0; c < 3; ++c) {
                int32_t score = 0;
                for (uint16_t i = 0; i < ap_num; ++i) {
                    int diff = abs((int)records[i].primary - (int)candidates[c]);
                    if (diff <= 2) {
                        int weight = (3 - diff) * (100 + records[i].rssi);
                        if (weight > 0) {
                            score += weight;
                        }
                    }
                }
                if (score < best_score) {
                    best_score = score;
                    best_channel = candidates[c];
                }
            }
        }
        free(records);
    }
    if (prev_mode == WIFI_MODE_AP) {
        esp_wifi_set_mode(WIFI_MODE_AP);
    }
    ESP_LOGI(TAG, "ACS selected channel %u", (unsigned)best_channel);
    return best_channel;
}

static void apply_softap_config(const app_mifi_settings_t *cfg,
                                uint8_t *out_actual_channel)
{
    if (!s_svc.ap_netif) {
        return;
    }
    if (!cfg->hotspot_enabled) {
        esp_wifi_stop();
        ESP_LOGI(TAG, "SoftAP stopped by user");
        return;
    }

    esp_wifi_start();
    uint8_t channel = cfg->channel_mode;
    if (channel == 0 || channel > 13) {
        channel = select_cleanest_2g_channel();
    }
    if (out_actual_channel) {
        *out_actual_channel = channel;
    }

    wifi_config_t wifi_cfg = {0};
    size_t ssid_len = strnlen(cfg->ssid, sizeof(wifi_cfg.ap.ssid));
    memcpy(wifi_cfg.ap.ssid, cfg->ssid, ssid_len);
    wifi_cfg.ap.ssid_len = (uint8_t)ssid_len;
    snprintf((char *)wifi_cfg.ap.password, sizeof(wifi_cfg.ap.password),
             "%.63s", cfg->password);
    wifi_cfg.ap.channel = channel;
    wifi_cfg.ap.max_connection = map_max_conn(cfg->max_conn_mode);
    wifi_cfg.ap.authmode = map_wifi_auth(cfg->auth_mode);
    if (strlen(cfg->password) < 8) {
        wifi_cfg.ap.authmode = WIFI_AUTH_OPEN;
    }
    wifi_cfg.ap.pmf_cfg.required = (wifi_cfg.ap.authmode == WIFI_AUTH_WPA3_PSK);
    wifi_cfg.ap.pmf_cfg.capable = true;
    wifi_cfg.ap.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    wifi_cfg.ap.csa_count = 3;
    wifi_cfg.ap.beacon_interval = cfg->power_save ? 200 : 100;

    esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(err));
    } else {
        wifi_config_t applied = {0};
        uint8_t effective_max = wifi_cfg.ap.max_connection;
        if (esp_wifi_get_config(WIFI_IF_AP, &applied) == ESP_OK) {
            effective_max = applied.ap.max_connection;
        }
        ESP_LOGI(TAG, "SoftAP updated: SSID=%s ch=%u auth=%d max_conn=%u",
                 cfg->ssid, (unsigned)channel, (int)wifi_cfg.ap.authmode,
                 (unsigned)effective_max);
        if (effective_max < wifi_cfg.ap.max_connection) {
            ESP_LOGW(TAG, "SoftAP max_connection clamped %u -> %u "
                     "(encryption keys shared with ESP-NOW)",
                     (unsigned)wifi_cfg.ap.max_connection,
                     (unsigned)effective_max);
        }
    }

    esp_wifi_set_ps(cfg->power_save ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);

    if (cfg->all_paused) {
        esp_netif_napt_disable(s_svc.ap_netif);
    } else {
        esp_err_t napt_err = esp_netif_napt_enable(s_svc.ap_netif);
        if (napt_err != ESP_OK) {
            ESP_LOGW(TAG, "NAPT enable failed: %s", esp_err_to_name(napt_err));
        }
    }

}

static uint32_t s_softap_dns_main;
static uint32_t s_softap_dns_backup;

static void configure_ap_dhcp_pool(esp_netif_t *ap_netif)
{
    dhcps_lease_t lease = {
        .enable = true,
    };
    /* 192.168.4.1 is the SoftAP itself. .2-.17 is one lease per station. */
    IP4_ADDR(&lease.start_ip, 192, 168, 4, 2);
    IP4_ADDR(&lease.end_ip, 192, 168, 4, 17);
    esp_err_t err = esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET,
                                           ESP_NETIF_REQUESTED_IP_ADDRESS,
                                           &lease, sizeof(lease));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DHCP pool set failed: %s", esp_err_to_name(err));
    }
}

static void forward_dns_to_softap(esp_netif_t *ap_netif, esp_netif_t *wan_netif)
{
    if (!ap_netif || !wan_netif) {
        return;
    }
    esp_netif_dns_info_t dns = {0};
    if (esp_netif_get_dns_info(wan_netif, ESP_NETIF_DNS_MAIN, &dns) != ESP_OK ||
        dns.ip.u_addr.ip4.addr == 0) {
        return;
    }
    /* Public resolver, so a station still has DNS if the modem resolver
     * only answers the first client. Restarting DHCP wipes every lease,
     * which leaves later phones associated with no address. */
    const uint32_t backup_addr = PP_HTONL(LWIP_MAKEU32(223, 5, 5, 5));
    if (s_softap_dns_main == dns.ip.u_addr.ip4.addr &&
        s_softap_dns_backup == backup_addr) {
        return;
    }
    esp_netif_dns_info_t backup = {0};
    backup.ip.type = ESP_IPADDR_TYPE_V4;
    backup.ip.u_addr.ip4.addr = backup_addr;

    uint8_t offer = DHCPS_OFFER_DNS;
    esp_netif_dhcps_stop(ap_netif);
    configure_ap_dhcp_pool(ap_netif);
    esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET,
                           ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer));
    esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns);
    esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_BACKUP, &backup);
    esp_err_t err = esp_netif_dhcps_start(ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
        ESP_LOGW(TAG, "SoftAP DHCP restart failed: %s", esp_err_to_name(err));
        return;
    }
    s_softap_dns_main = dns.ip.u_addr.ip4.addr;
    s_softap_dns_backup = backup_addr;
    ESP_LOGI(TAG, "Forwarded WAN DNS " IPSTR " + backup 223.5.5.5 to SoftAP DHCP",
             IP2STR(&dns.ip.u_addr.ip4));
}

static void push_log_event_locked(const char *zh, const char *en)
{
    uint32_t sec = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    uint32_t m = (sec / 60u) % 100u;
    uint32_t s = sec % 60u;
    for (int i = 3; i > 0; --i) {
        memcpy(s_svc.telemetry.logs_zh[i], s_svc.telemetry.logs_zh[i - 1],
               sizeof(s_svc.telemetry.logs_zh[i]));
        memcpy(s_svc.telemetry.logs_en[i], s_svc.telemetry.logs_en[i - 1],
               sizeof(s_svc.telemetry.logs_en[i]));
    }
    snprintf(s_svc.telemetry.logs_zh[0], sizeof(s_svc.telemetry.logs_zh[0]),
             "[%02u:%02u] %s", (unsigned)m, (unsigned)s, zh ? zh : "");
    snprintf(s_svc.telemetry.logs_en[0], sizeof(s_svc.telemetry.logs_en[0]),
             "[%02u:%02u] %s", (unsigned)m, (unsigned)s, en ? en : "");
}

/* Parse AT responses from ML307 CDC port */
static void parse_at_line_locked(const char *line)
{
    ESP_LOGI(TAG, "AT<< %s", line);
    if (strstr(line, "+CPIN: READY") || strstr(line, "+CPIN:READY")) {
        if (!s_svc.telemetry.sim_ready) {
            s_svc.telemetry.sim_ready = true;
            push_log_event_locked("SIM 卡已就绪", "SIM card ready");
        }
    } else if (strstr(line, "+CME ERROR") ||
               strstr(line, "+CPIN: NOT") ||
               strstr(line, "SIM not inserted")) {
        if (s_svc.telemetry.sim_ready) {
            push_log_event_locked("SIM 卡未插入或已拔出", "SIM card removed");
        }
        s_svc.telemetry.sim_ready = false;
        s_svc.telemetry.registered = false;
        s_svc.telemetry.rsrp_dbm = 0;
        s_svc.telemetry.sinr_db = 0;
        s_svc.telemetry.operator_zh[0] = '\0';
        s_svc.telemetry.operator_en[0] = '\0';
    } else if (strstr(line, "+CEREG: 0,1") || strstr(line, "+CEREG: 0,5") ||
               strstr(line, "+CGREG: 0,1") || strstr(line, "+CGREG: 0,5") ||
               strstr(line, "+CEREG: 1") || strstr(line, "+CEREG: 5")) {
        s_svc.telemetry.sim_ready = true;
        if (!s_svc.telemetry.registered) {
            s_svc.telemetry.registered = true;
            push_log_event_locked("4G 蜂窝网络已注册", "4G cellular registered");
        }
    } else if (strstr(line, "+CEREG: 0,0") || strstr(line, "+CEREG: 0,2") ||
               strstr(line, "+CEREG: 0,3") || strstr(line, "+CEREG: 0,4")) {
        s_svc.telemetry.registered = false;
    } else if (strncmp(line, "+CSQ:", 5) == 0) {
        int rssi = 99, ber = 99;
        if (sscanf(line + 5, "%d,%d", &rssi, &ber) >= 1) {
            if (rssi >= 0 && rssi <= 31 && s_svc.telemetry.sim_ready) {
                s_svc.telemetry.rsrp_dbm = (int16_t)(-113 + rssi * 2);
                if (s_svc.telemetry.sinr_db == 0) {
                    int est = (rssi - 10) / 2 + 5;
                    if (est < 1) {
                        est = 1;
                    } else if (est > 25) {
                        est = 25;
                    }
                    s_svc.telemetry.sinr_db = (int16_t)est;
                }
            } else if (rssi == 99) {
                s_svc.telemetry.rsrp_dbm = 0;
                s_svc.telemetry.sinr_db = 0;
            }
        }
    } else if (strncmp(line, "+CESQ:", 6) == 0) {
        int rxlev = 99, ber = 99, rscp = 255, ecno = 255, rsrq = 255, rsrp = 255;
        if (sscanf(line + 6, "%d,%d,%d,%d,%d,%d",
                   &rxlev, &ber, &rscp, &ecno, &rsrq, &rsrp) == 6) {
            if (rsrp >= 0 && rsrp <= 97 && s_svc.telemetry.sim_ready) {
                s_svc.telemetry.rsrp_dbm = (int16_t)(-140 + rsrp);
            } else if (rsrp == 255 && !s_svc.telemetry.sim_ready) {
                s_svc.telemetry.rsrp_dbm = 0;
            }
            if (rsrq >= 0 && rsrq <= 34 && s_svc.telemetry.sim_ready) {
                int est_sinr = (rsrq * 2) / 3 - 2;
                if (est_sinr < 0) {
                    est_sinr = 0;
                }
                s_svc.telemetry.sinr_db = (int16_t)est_sinr;
            }
        }
    } else if (strncmp(line, "+COPS:", 6) == 0) {
        if (!s_svc.telemetry.sim_ready) {
            s_svc.telemetry.operator_zh[0] = '\0';
            s_svc.telemetry.operator_en[0] = '\0';
        } else if (strstr(line, "46000") || strstr(line, "46002") ||
                   strstr(line, "46007") || strcasestr(line, "CMCC") ||
                   strcasestr(line, "MOBILE")) {
            snprintf(s_svc.telemetry.operator_zh,
                     sizeof(s_svc.telemetry.operator_zh), "中国移动 4G");
            snprintf(s_svc.telemetry.operator_en,
                     sizeof(s_svc.telemetry.operator_en), "China Mobile 4G");
        } else if (strstr(line, "46001") || strstr(line, "46006") ||
                   strcasestr(line, "UNICOM") || strcasestr(line, "CHN-UNICOM")) {
            snprintf(s_svc.telemetry.operator_zh,
                     sizeof(s_svc.telemetry.operator_zh), "中国联通 4G");
            snprintf(s_svc.telemetry.operator_en,
                     sizeof(s_svc.telemetry.operator_en), "China Unicom 4G");
        } else if (strstr(line, "46003") || strstr(line, "46011") ||
                   strcasestr(line, "TELECOM") || strcasestr(line, "CT")) {
            snprintf(s_svc.telemetry.operator_zh,
                     sizeof(s_svc.telemetry.operator_zh), "中国电信 4G");
            snprintf(s_svc.telemetry.operator_en,
                     sizeof(s_svc.telemetry.operator_en), "China Telecom 4G");
        } else if (strchr(line, '"') == NULL) {
            /* +COPS: 0 with no operator string means not registered */
            s_svc.telemetry.operator_zh[0] = '\0';
            s_svc.telemetry.operator_en[0] = '\0';
        }
    } else if (strncmp(line, "ML307", 5) == 0 || strncmp(line, "MBRH", 4) == 0 ||
               strncmp(line, "+CGMR:", 6) == 0) {
        const char *fw = (strncmp(line, "+CGMR:", 6) == 0) ? (line + 6) : line;
        while (*fw == ' ') {
            ++fw;
        }
        if (*fw) {
            snprintf(s_svc.telemetry.modem_fw,
                     sizeof(s_svc.telemetry.modem_fw), "%.20s", fw);
        }
    }
}

static void at_recv_cb(usbh_cdc_port_handle_t port, void *arg)
{
    (void)arg;
    size_t len = 0;
    if (usbh_cdc_get_rx_buffer_size(port, &len) != ESP_OK || len == 0) {
        return;
    }
    uint8_t temp[512];
    while (len > 0) {
        size_t chunk = len > sizeof(temp) ? sizeof(temp) : len;
        if (usbh_cdc_read_bytes(port, temp, &chunk, 0) != ESP_OK || chunk == 0) {
            break;
        }
        len -= chunk;
        xSemaphoreTake(s_svc.lock, portMAX_DELAY);
        for (size_t i = 0; i < chunk; ++i) {
            char c = (char)temp[i];
            if (c == '\r' || c == '\n') {
                if (s_svc.at_rx_len > 0) {
                    s_svc.at_rx_buf[s_svc.at_rx_len] = '\0';
                    parse_at_line_locked(s_svc.at_rx_buf);
                    s_svc.at_rx_len = 0;
                }
            } else if (s_svc.at_rx_len + 1 < sizeof(s_svc.at_rx_buf)) {
                s_svc.at_rx_buf[s_svc.at_rx_len++] = c;
            }
        }
        xSemaphoreGive(s_svc.lock);
    }
}

static void at_closed_cb(usbh_cdc_port_handle_t port, void *arg)
{
    (void)port;
    (void)arg;
    xSemaphoreTake(s_svc.lock, portMAX_DELAY);
    s_svc.at_port = NULL;
    s_svc.at_ready = false;
    xSemaphoreGive(s_svc.lock);
    ESP_LOGW(TAG, "ML307 USB AT port closed");
}

static void send_at_cmd(const char *cmd)
{
    xSemaphoreTake(s_svc.lock, portMAX_DELAY);
    usbh_cdc_port_handle_t port = s_svc.at_port;
    xSemaphoreGive(s_svc.lock);
    if (!port || !cmd) {
        return;
    }
    /* Keep AT writes short so the service worker does not stall USB/UI long. */
    esp_err_t err = usbh_cdc_write_bytes(port, (const uint8_t *)cmd, strlen(cmd),
                                         pdMS_TO_TICKS(200));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "AT write failed (%s): %s", esp_err_to_name(err), cmd);
    } else {
        ESP_LOGI(TAG, "AT>> %s", cmd);
    }
}

static void request_modem_probe_locked(void)
{
    s_svc.probe_requested = true;
}

/* Re-power USIM interface via CFUN=0 -> CFUN=1 so a hot-plugged SIM card
 * is detected without rebooting the board or dropping USB RNDIS. */
static void modem_repower_sim(void)
{
    ESP_LOGI(TAG, "Re-powering SIM slot via AT+CFUN=0 -> AT+CFUN=1");
    send_at_cmd("AT+CFUN=0\r\n");
    vTaskDelay(pdMS_TO_TICKS(600));
    send_at_cmd("AT+CFUN=1\r\n");
    vTaskDelay(pdMS_TO_TICKS(900));
}

static void modem_probe_status(bool ensure_dial)
{
    ESP_LOGI(TAG, "Probing modem SIM/network status (ensure_dial=%d)",
             (int)ensure_dial);
    send_at_cmd("ATE0\r\n");
    vTaskDelay(pdMS_TO_TICKS(60));
    send_at_cmd("AT+CGMR\r\n");
    vTaskDelay(pdMS_TO_TICKS(60));
    send_at_cmd("AT+CPIN?\r\n");
    vTaskDelay(pdMS_TO_TICKS(60));
    send_at_cmd("AT+CEREG?\r\n");
    vTaskDelay(pdMS_TO_TICKS(60));
    send_at_cmd("AT+COPS=3,2;+COPS?\r\n");
    vTaskDelay(pdMS_TO_TICKS(60));
    send_at_cmd("AT+CSQ\r\n");
    vTaskDelay(pdMS_TO_TICKS(60));
    send_at_cmd("AT+CESQ\r\n");
    if (ensure_dial) {
        vTaskDelay(pdMS_TO_TICKS(60));
        /* Start RNDIS data call without resetting USB mode (do not send
         * AT+MDIALUPCFG="mode",0 or AT+MDIALUP=1,0 while RNDIS is active). */
        send_at_cmd("AT+MDIALUP=1,1\r\n");
    }
    xSemaphoreTake(s_svc.lock, portMAX_DELAY);
    s_svc.last_dial_us = esp_timer_get_time();
    ++s_svc.dial_attempts;
    xSemaphoreGive(s_svc.lock);
}

static bool try_open_modem_at_port_addr(uint8_t dev_addr)
{
    if (dev_addr == 0) {
        return false;
    }
    xSemaphoreTake(s_svc.lock, portMAX_DELAY);
    bool already = (s_svc.at_ready && s_svc.at_port != NULL);
    xSemaphoreGive(s_svc.lock);
    if (already) {
        return true;
    }

    /* Probe ML307 AT interface numbers (2, 3); skip itf 4 (debug trace).
     * Disable notification pipe to save USB Host DMA channels. */
    const uint8_t candidate_itfs[] = {2, 3};
    for (size_t i = 0; i < sizeof(candidate_itfs); ++i) {
        usbh_cdc_port_config_t cfg = {
            .dev_addr = dev_addr,
            .itf_num = candidate_itfs[i],
            .in_transfer_buffer_size = 512,
            .out_transfer_buffer_size = 512,
            .in_ringbuf_size = 1024,
            .cbs = {
                .recv_data = at_recv_cb,
                .closed = at_closed_cb,
                .user_data = NULL,
            },
            .flags = USBH_CDC_FLAGS_DISABLE_NOTIFICATION,
        };
        usbh_cdc_port_handle_t at_port = NULL;
        esp_err_t err = usbh_cdc_port_open(&cfg, &at_port);
        if (err == ESP_OK && at_port != NULL) {
            xSemaphoreTake(s_svc.lock, portMAX_DELAY);
            s_svc.at_port = at_port;
            s_svc.at_ready = true;
            s_svc.modem_dev_addr = dev_addr;
            xSemaphoreGive(s_svc.lock);
            ESP_LOGI(TAG, "Opened ML307 AT port addr=%u itf=%u",
                     (unsigned)dev_addr, (unsigned)candidate_itfs[i]);
            return true;
        }
        ESP_LOGD(TAG, "AT itf %u open failed: %s",
                 (unsigned)candidate_itfs[i], esp_err_to_name(err));
    }
    ESP_LOGW(TAG, "Failed to open ML307 AT port on addr=%u", (unsigned)dev_addr);
    return false;
}

static void try_open_modem_at_port(void)
{
    uint8_t addr = 0;
    xSemaphoreTake(s_svc.lock, portMAX_DELAY);
    addr = s_svc.modem_dev_addr;
    xSemaphoreGive(s_svc.lock);

    if (addr == 0 && s_svc.rndis_drv) {
        usbh_cdc_port_handle_t rndis_port =
            usb_rndis_get_cdc_port_handle(s_svc.rndis_drv);
        usb_device_handle_t dev_hdl = NULL;
        if (rndis_port &&
            usbh_cdc_get_dev_handle(rndis_port, &dev_hdl) == ESP_OK &&
            dev_hdl) {
            usb_device_info_t dev_info;
            if (usb_host_device_info(dev_hdl, &dev_info) == ESP_OK) {
                addr = dev_info.dev_addr;
            }
        }
    }
    try_open_modem_at_port_addr(addr);
}

static void cdc_device_event_cb(usbh_cdc_device_event_t event,
                               usbh_cdc_device_event_data_t *event_data,
                               void *user_ctx)
{
    (void)user_ctx;
    if (!event_data) {
        return;
    }
    if (event == CDC_HOST_DEVICE_EVENT_CONNECTED) {
        const usb_device_desc_t *desc = event_data->new_dev.device_desc;
        uint16_t vid = desc ? desc->idVendor : 0;
        uint16_t pid = desc ? desc->idProduct : 0;
        uint8_t addr = event_data->new_dev.dev_addr;
        ESP_LOGI(TAG,
                 "USB modem enumerated: addr=%u VID=0x%04X PID=0x%04X matched_itf=%d",
                 (unsigned)addr, vid, pid, event_data->new_dev.matched_intf_num);
        xSemaphoreTake(s_svc.lock, portMAX_DELAY);
        s_svc.modem_dev_addr = addr;
        s_svc.modem_vid = vid;
        s_svc.modem_pid = pid;
        s_svc.telemetry.usb_connected = true;
        s_svc.telemetry.valid = true;
        s_svc.last_dial_us = esp_timer_get_time();
        push_log_event_locked("4G 模组已接入 (ML307C)", "4G modem connected (ML307C)");
        request_modem_probe_locked();
        xSemaphoreGive(s_svc.lock);
        /* Do NOT open CDC port or write AT commands inside the USB event
         * callback; let RNDIS finish enumeration first. */
    } else if (event == CDC_HOST_DEVICE_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "USB modem disconnected: addr=%u",
                 (unsigned)event_data->dev_gone.dev_addr);
        xSemaphoreTake(s_svc.lock, portMAX_DELAY);
        if (s_svc.modem_dev_addr == event_data->dev_gone.dev_addr) {
            s_svc.modem_dev_addr = 0;
            s_svc.modem_vid = 0;
            s_svc.modem_pid = 0;
            s_svc.telemetry.usb_connected = false;
            s_svc.telemetry.link_up = false;
            s_svc.telemetry.sim_ready = false;
            s_svc.telemetry.registered = false;
            s_svc.telemetry.rsrp_dbm = 0;
            s_svc.telemetry.sinr_db = 0;
            s_svc.telemetry.operator_zh[0] = '\0';
            s_svc.telemetry.operator_en[0] = '\0';
            ++s_svc.telemetry.drop_count;
            snprintf(s_svc.telemetry.wan_ip, sizeof(s_svc.telemetry.wan_ip),
                     "0.0.0.0");
            push_log_event_locked("4G 模组已断开", "4G modem disconnected");
        }
        xSemaphoreGive(s_svc.lock);
    }
}

static dhcp_lease_t *find_dhcp_lease_locked(const uint8_t mac[6])
{
    for (int i = 0; i < MIFI_DHCP_LEASES; ++i) {
        if (s_svc.leases[i].used &&
            memcmp(s_svc.leases[i].mac, mac, 6) == 0) {
            return &s_svc.leases[i];
        }
    }
    return NULL;
}

static bool lease_mac_is_online_locked(const uint8_t mac[6])
{
    for (int i = 0; i < APP_MIFI_MAX_STATIONS; ++i) {
        if (s_svc.stations[i].active &&
            memcmp(s_svc.stations[i].mac, mac, 6) == 0) {
            return true;
        }
    }
    return false;
}

static dhcp_lease_t *alloc_dhcp_lease_locked(const uint8_t mac[6])
{
    dhcp_lease_t *lease = find_dhcp_lease_locked(mac);
    if (lease) {
        return lease;
    }
    for (int i = 0; i < MIFI_DHCP_LEASES; ++i) {
        if (!s_svc.leases[i].used) {
            return &s_svc.leases[i];
        }
    }
    for (int i = 0; i < MIFI_DHCP_LEASES; ++i) {
        if (!lease_mac_is_online_locked(s_svc.leases[i].mac)) {
            return &s_svc.leases[i];
        }
    }
    return &s_svc.leases[0];
}

/* The event hostname is already this MAC's lease name. */
static void remember_dhcp_lease_locked(const ip_event_assigned_ip_to_client_t *e)
{
    dhcp_lease_t *lease = alloc_dhcp_lease_locked(e->mac);
    memset(lease, 0, sizeof(*lease));
    lease->used = true;
    memcpy(lease->mac, e->mac, 6);
    lease->ip = e->ip;
    if (e->hostname[0]) {
        snprintf(lease->hostname, sizeof(lease->hostname), "%.31s", e->hostname);
    }
}

static void wifi_and_net_event_handler(void *arg, esp_event_base_t base,
                                       int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *e = data;
        char mac_str[18];
        format_mac_str(e->mac, mac_str);
        xSemaphoreTake(s_svc.lock, portMAX_DELAY);
        bool blocked = is_mac_blocked_locked(mac_str);
        if (!blocked) {
            for (int i = 0; i < APP_MIFI_MAX_STATIONS; ++i) {
                if (!s_svc.stations[i].active ||
                    memcmp(s_svc.stations[i].mac, e->mac, 6) == 0) {
                    if (!s_svc.stations[i].active) {
                        memset(&s_svc.stations[i], 0, sizeof(s_svc.stations[i]));
                    }
                    s_svc.stations[i].active = true;
                    memcpy(s_svc.stations[i].mac, e->mac, 6);
                    s_svc.stations[i].aid = e->aid;
                    s_svc.stations[i].connect_us = esp_timer_get_time();
                    break;
                }
            }
            char zh_ev[44], en_ev[44];
            snprintf(zh_ev, sizeof(zh_ev), "热点设备接入 %02X:%02X",
                     e->mac[4], e->mac[5]);
            snprintf(en_ev, sizeof(en_ev), "Client joined %02X:%02X",
                     e->mac[4], e->mac[5]);
            push_log_event_locked(zh_ev, en_ev);
        }
        xSemaphoreGive(s_svc.lock);
        if (blocked) {
            ESP_LOGW(TAG, "Rejected blocked station %s (AID=%u)", mac_str,
                     (unsigned)e->aid);
            esp_wifi_deauth_sta(e->aid);
        } else {
            ESP_LOGI(TAG, "Station connected: %s (AID=%u)", mac_str,
                     (unsigned)e->aid);
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        const wifi_event_ap_stadisconnected_t *e = data;
        xSemaphoreTake(s_svc.lock, portMAX_DELAY);
        for (int i = 0; i < APP_MIFI_MAX_STATIONS; ++i) {
            if (s_svc.stations[i].active &&
                memcmp(s_svc.stations[i].mac, e->mac, 6) == 0) {
                memset(&s_svc.stations[i], 0, sizeof(s_svc.stations[i]));
                break;
            }
        }
        char zh_ev[44], en_ev[44];
        snprintf(zh_ev, sizeof(zh_ev), "热点设备离开 %02X:%02X",
                 e->mac[4], e->mac[5]);
        snprintf(en_ev, sizeof(en_ev), "Client left %02X:%02X",
                 e->mac[4], e->mac[5]);
        push_log_event_locked(zh_ev, en_ev);
        xSemaphoreGive(s_svc.lock);
    } else if (base == IP_EVENT && id == IP_EVENT_ASSIGNED_IP_TO_CLIENT) {
        const ip_event_assigned_ip_to_client_t *e = data;
        xSemaphoreTake(s_svc.lock, portMAX_DELAY);
        remember_dhcp_lease_locked(e);
        xSemaphoreGive(s_svc.lock);
    } else if (base == IOT_ETH_EVENT) {
        if (id == IOT_ETH_EVENT_START) {
            ESP_LOGI(TAG, "USB RNDIS eth start");
        } else if (id == IOT_ETH_EVENT_CONNECTED) {
            ESP_LOGI(TAG, "USB RNDIS 4G link connected");
            xSemaphoreTake(s_svc.lock, portMAX_DELAY);
            s_svc.telemetry.usb_connected = true;
            s_svc.telemetry.valid = true;
            request_modem_probe_locked();
            xSemaphoreGive(s_svc.lock);
        } else if (id == IOT_ETH_EVENT_DISCONNECTED) {
            ESP_LOGW(TAG, "USB RNDIS 4G link disconnected");
            xSemaphoreTake(s_svc.lock, portMAX_DELAY);
            /* Keep usb_connected if the CDC device is still present. */
            if (s_svc.telemetry.link_up) {
                ++s_svc.telemetry.drop_count;
            }
            s_svc.telemetry.link_up = false;
            snprintf(s_svc.telemetry.wan_ip, sizeof(s_svc.telemetry.wan_ip),
                     "0.0.0.0");
            push_log_event_locked("RNDIS 数据链路断开", "RNDIS link down");
            xSemaphoreGive(s_svc.lock);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_ETH_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "USB RNDIS Got WAN IP: " IPSTR, IP2STR(&e->ip_info.ip));
        xSemaphoreTake(s_svc.lock, portMAX_DELAY);
        /* Do NOT force sim_ready=true or registered=true here: ML307 RNDIS
         * assigns a local 192.168.x.x IP via DHCP even when no SIM card is
         * inserted. SIM/registration state is determined strictly by AT. */
        s_svc.telemetry.link_up = true;
        s_svc.telemetry.valid = true;
        snprintf(s_svc.telemetry.wan_ip, sizeof(s_svc.telemetry.wan_ip), IPSTR,
                 IP2STR(&e->ip_info.ip));
        char zh_ev[44], en_ev[44];
        snprintf(zh_ev, sizeof(zh_ev), "RNDIS 获取地址 %s",
                 s_svc.telemetry.wan_ip);
        snprintf(en_ev, sizeof(en_ev), "RNDIS IP %s",
                 s_svc.telemetry.wan_ip);
        push_log_event_locked(zh_ev, en_ev);
        request_modem_probe_locked();
        xSemaphoreGive(s_svc.lock);
        esp_netif_set_default_netif(s_svc.rndis_netif);
        forward_dns_to_softap(s_svc.ap_netif, s_svc.rndis_netif);
        if (s_svc.ap_netif && !s_svc.settings.all_paused) {
            esp_err_t napt_err = esp_netif_napt_enable(s_svc.ap_netif);
            if (napt_err != ESP_OK) {
                ESP_LOGW(TAG, "NAPT enable on WAN-up failed: %s",
                         esp_err_to_name(napt_err));
            }
        }
    }
}

static void refresh_stations_snapshot_locked(void)
{
    int64_t now_us = esp_timer_get_time();
    uint8_t out_idx = 0;
    for (int i = 0; i < APP_MIFI_MAX_STATIONS; ++i) {
        if (!s_svc.stations[i].active) {
            continue;
        }
        app_mifi_station_info_t *dst = &s_svc.telemetry.stations[out_idx++];
        memset(dst, 0, sizeof(*dst));
        format_mac_str(s_svc.stations[i].mac, dst->mac);
        const dhcp_lease_t *lease = find_dhcp_lease_locked(s_svc.stations[i].mac);
        char lease_name[32];
        esp_ip4_addr_t lease_ip = {0};
        lease_name[0] = '\0';
        if (lease) {
            lease_ip = lease->ip;
            if (lease->hostname[0]) {
                memcpy(lease_name, lease->hostname, sizeof(lease_name));
                lease_name[sizeof(lease_name) - 1] = '\0';
            }
        }
        if (lease_name[0]) {
            snprintf(s_svc.stations[i].hostname,
                     sizeof(s_svc.stations[i].hostname), "%s", lease_name);
            snprintf(dst->name, sizeof(dst->name), "%s", lease_name);
        } else {
            s_svc.stations[i].hostname[0] = '\0';
        }
        if (lease_ip.addr != 0) {
            s_svc.stations[i].ip = lease_ip;
            snprintf(dst->ip, sizeof(dst->ip), IPSTR, IP2STR(&lease_ip));
        } else {
            s_svc.stations[i].ip.addr = 0;
            snprintf(dst->ip, sizeof(dst->ip), "--");
        }
        int64_t sec = (now_us - s_svc.stations[i].connect_us) / 1000000;
        if (sec < 60) {
            snprintf(dst->online, sizeof(dst->online), "%d sec", (int)sec);
        } else if (sec < 3600) {
            snprintf(dst->online, sizeof(dst->online), "%d min", (int)(sec / 60));
        } else {
            snprintf(dst->online, sizeof(dst->online), "%dh %dmin",
                     (int)(sec / 3600), (int)((sec % 3600) / 60));
        }
        format_wan_usage(s_svc.stations[i].used_bytes, dst->usage,
                         sizeof(dst->usage));
        dst->kind = guess_device_kind(s_svc.stations[i].hostname,
                                      s_svc.stations[i].mac);
    }
    s_svc.telemetry.station_count = out_idx;
}

static void cb_user_activity(void *ctx);

static void poll_battery_and_temp(void)
{
    bsp_battery_status_t bat = {0};
    bool has_gauge = (bsp_battery_read(&bat) == ESP_OK && bat.voltage_mv > 2500);
    float chip_celsius = 0.0f;
    bool has_tsens = (s_svc.tsens != NULL &&
                      temperature_sensor_get_celsius(s_svc.tsens, &chip_celsius) == ESP_OK);
    float imu_celsius = 0.0f;
    bool has_imu_temp = false;
    struct bmi2_dev *bmi = bsp_imu_get_handle();
    if (bmi != NULL) {
        int16_t raw_temp = 0;
        if (bmi2_get_temperature_data(&raw_temp, bmi) == BMI2_OK &&
            raw_temp != (int16_t)0x8000) {
            imu_celsius = ((float)raw_temp / 512.0f) + 23.0f;
            if (imu_celsius > -20.0f && imu_celsius < 90.0f) {
                has_imu_temp = true;
            }
        }
    }

    /* Estimate Mosaico internal board temperature by fusing the 3 distributed
     * sensors (ESP32-S31 SoC, BMI270 IMU in center, BQ27220 gauge). */
    float temp_sum = 0.0f;
    int temp_cnt = 0;
    if (has_tsens && chip_celsius > -20.0f && chip_celsius < 90.0f) {
        temp_sum += chip_celsius;
        ++temp_cnt;
    }
    if (has_imu_temp) {
        temp_sum += imu_celsius;
        ++temp_cnt;
    }
    if (has_gauge && bat.temperature_c > -20.0f && bat.temperature_c < 90.0f) {
        temp_sum += bat.temperature_c;
        ++temp_cnt;
    }

    xSemaphoreTake(s_svc.lock, portMAX_DELAY);
    if (has_gauge) {
        s_svc.telemetry.battery_percent = bat.state_of_charge <= 100
                                              ? bat.state_of_charge
                                              : 100;
        s_svc.telemetry.battery_mv = bat.voltage_mv;
        s_svc.telemetry.battery_current_ma = bat.current_ma;
        /* BQ27220 status_flags bit 0 is DSG (1 = discharging); bit 9 is FC (full).
         * If external power is present and battery is not discharging (DSG==0
         * or current >= -15mA), treat as powered/charging. */
        bool dsg = (bat.status_flags & 0x0001u) != 0;
        s_svc.telemetry.charging =
            (bat.current_ma > 5 || bat.average_current_ma > 5 || !dsg);
        s_svc.telemetry.time_to_empty_min = bat.time_to_empty_min;
        s_svc.telemetry.time_to_full_min = bat.time_to_full_min;
    } else {
        /* Powered directly via USB without battery gauge */
        s_svc.telemetry.battery_percent = 100;
        s_svc.telemetry.battery_mv = 0;
        s_svc.telemetry.battery_current_ma = 0;
        s_svc.telemetry.charging = true;
        s_svc.telemetry.time_to_empty_min = 0;
        s_svc.telemetry.time_to_full_min = 0;
    }
    if (temp_cnt > 0) {
        s_svc.telemetry.temperature_c =
            (int16_t)((temp_sum / (float)temp_cnt) + 0.5f);
    }
    xSemaphoreGive(s_svc.lock);
}

static int sta_list_count(const wifi_sta_list_t *list)
{
    if (!list || list->num < 0) {
        return 0;
    }
    if (list->num > ESP_WIFI_MAX_CONN_NUM) {
        return ESP_WIFI_MAX_CONN_NUM;
    }
    return list->num;
}

static bool mac_in_sta_list(const wifi_sta_list_t *list, int count,
                            const uint8_t mac[6])
{
    for (int i = 0; i < count; ++i) {
        if (memcmp(list->sta[i].mac, mac, 6) == 0) {
            return true;
        }
    }
    return false;
}

/* SoftAP driver list is the source of truth. Query it before taking
 * s_svc.lock: the Wi-Fi event handler holds the driver lock and then
 * this lock. */
static void reconcile_stations_with_driver(void)
{
    wifi_sta_list_t live = {0};
    if (esp_wifi_ap_get_sta_list(&live) != ESP_OK) {
        return;
    }
    const int count = sta_list_count(&live);
    uint16_t aids[ESP_WIFI_MAX_CONN_NUM] = {0};
    for (int i = 0; i < count; ++i) {
        uint16_t aid = 0;
        if (esp_wifi_ap_get_sta_aid(live.sta[i].mac, &aid) == ESP_OK) {
            aids[i] = aid;
        }
    }

    uint16_t kick[ESP_WIFI_MAX_CONN_NUM] = {0};
    int kick_count = 0;

    xSemaphoreTake(s_svc.lock, portMAX_DELAY);
    for (int i = 0; i < APP_MIFI_MAX_STATIONS; ++i) {
        if (!s_svc.stations[i].active ||
            mac_in_sta_list(&live, count, s_svc.stations[i].mac)) {
            continue;
        }
        char mac_str[18];
        format_mac_str(s_svc.stations[i].mac, mac_str);
        char zh_ev[44], en_ev[44];
        snprintf(zh_ev, sizeof(zh_ev), "热点设备离开 %02X:%02X",
                 s_svc.stations[i].mac[4], s_svc.stations[i].mac[5]);
        snprintf(en_ev, sizeof(en_ev), "Client left %02X:%02X",
                 s_svc.stations[i].mac[4], s_svc.stations[i].mac[5]);
        push_log_event_locked(zh_ev, en_ev);
        ESP_LOGI(TAG, "Dropped stale station %s", mac_str);
        memset(&s_svc.stations[i], 0, sizeof(s_svc.stations[i]));
    }
    for (int s = 0; s < count; ++s) {
        const uint8_t *mac = live.sta[s].mac;
        if ((mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) == 0) {
            continue;
        }
        char mac_str[18];
        format_mac_str(mac, mac_str);
        const bool blocked = is_mac_blocked_locked(mac_str);
        int slot = -1;
        int free_slot = -1;
        for (int i = 0; i < APP_MIFI_MAX_STATIONS; ++i) {
            if (s_svc.stations[i].active &&
                memcmp(s_svc.stations[i].mac, mac, 6) == 0) {
                slot = i;
                break;
            }
            if (free_slot < 0 && !s_svc.stations[i].active) {
                free_slot = i;
            }
        }
        if (blocked) {
            if (slot >= 0) {
                memset(&s_svc.stations[slot], 0, sizeof(s_svc.stations[slot]));
            }
            if (aids[s] != 0 && kick_count < ESP_WIFI_MAX_CONN_NUM) {
                kick[kick_count++] = aids[s];
            }
            continue;
        }
        if (slot >= 0) {
            if (aids[s] != 0) {
                s_svc.stations[slot].aid = aids[s];
            }
            continue;
        }
        if (free_slot < 0) {
            continue;
        }
        station_slot_t *dst = &s_svc.stations[free_slot];
        memset(dst, 0, sizeof(*dst));
        dst->active = true;
        memcpy(dst->mac, mac, 6);
        dst->aid = aids[s];
        dst->connect_us = esp_timer_get_time();
        char zh_ev[44], en_ev[44];
        snprintf(zh_ev, sizeof(zh_ev), "热点设备接入 %02X:%02X", mac[4], mac[5]);
        snprintf(en_ev, sizeof(en_ev), "Client joined %02X:%02X", mac[4], mac[5]);
        push_log_event_locked(zh_ev, en_ev);
        ESP_LOGI(TAG, "Station reconciled from driver: %s (AID=%u)",
                 mac_str, (unsigned)aids[s]);
    }
    xSemaphoreGive(s_svc.lock);

    for (int i = 0; i < kick_count; ++i) {
        esp_wifi_deauth_sta(kick[i]);
    }
}

static void service_worker_task(void *arg)
{
    (void)arg;
    uint32_t tick = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        ++tick;

        bool apply_cfg = false;
        bool do_redial = false;
        bool do_probe = false;
        bool do_auto_dial = false;
        app_mifi_settings_t cfg_copy;

        xSemaphoreTake(s_svc.lock, portMAX_DELAY);
        if (s_svc.settings_dirty) {
            s_svc.settings_dirty = false;
            apply_cfg = true;
            cfg_copy = s_svc.settings;
        }
        if (s_svc.redial_requested) {
            s_svc.redial_requested = false;
            do_redial = true;
        }
        if (s_svc.probe_requested) {
            s_svc.probe_requested = false;
            do_probe = true;
        }
        if (!do_redial && !do_probe &&
            s_svc.telemetry.usb_connected && !s_svc.telemetry.link_up &&
            s_svc.last_dial_us != 0 &&
            (esp_timer_get_time() - s_svc.last_dial_us) > MIFI_DIAL_RETRY_US) {
            do_auto_dial = true;
        }
        xSemaphoreGive(s_svc.lock);

        if (apply_cfg) {
            uint8_t actual_ch = cfg_copy.actual_channel;
            bsp_display_brightness_set(cfg_copy.brightness);
            apply_softap_config(&cfg_copy, &actual_ch);
            xSemaphoreTake(s_svc.lock, portMAX_DELAY);
            s_svc.settings.actual_channel = actual_ch;
            cfg_copy.actual_channel = actual_ch;
            xSemaphoreGive(s_svc.lock);
            save_settings_to_nvs(&cfg_copy);
        }

        if (do_redial) {
            if (!s_svc.at_ready) {
                try_open_modem_at_port();
            }
            if (s_svc.at_ready) {
                ESP_LOGI(TAG, "Triggering 4G RNDIS redial via AT");
                if (s_svc.rndis_netif) {
                    esp_netif_dhcpc_stop(s_svc.rndis_netif);
                }
                modem_repower_sim();
                modem_probe_status(true);
                if (s_svc.rndis_netif) {
                    esp_netif_dhcpc_start(s_svc.rndis_netif);
                }
            } else {
                ESP_LOGW(TAG, "Redial deferred; AT port not ready");
            }
        } else if (do_probe || do_auto_dial) {
            if (!s_svc.at_ready) {
                try_open_modem_at_port();
            }
            if (s_svc.at_ready) {
                modem_probe_status(do_auto_dial);
            }
        }

        /* Hardware touch INT level check + auto screen sleep check */
        if (gpio_get_level(BSP_LCD_TOUCH_INT) == 0) {
            cb_user_activity(NULL);
        }
        xSemaphoreTake(s_svc.lock, portMAX_DELAY);
        uint32_t timeout_s = sleep_timeout_sec(s_svc.settings.sleep_mode);
        int64_t idle_s = (esp_timer_get_time() - s_svc.last_activity_us) / 1000000;
        bool should_sleep = (timeout_s > 0 && idle_s >= (int64_t)timeout_s);
        bool state_changed = (should_sleep != s_svc.display_sleeping);
        s_svc.display_sleeping = should_sleep;
        uint8_t target_brightness = s_svc.settings.brightness;
        xSemaphoreGive(s_svc.lock);

        if (state_changed) {
            if (should_sleep) {
                bsp_display_brightness_set(0);
            } else {
                bsp_display_on();
                bsp_display_brightness_set(target_brightness);
            }
        }

        /* Poll AT modem status every 5 seconds when connected */
        if ((tick % 5) == 0 && s_svc.at_ready) {
            xSemaphoreTake(s_svc.lock, portMAX_DELAY);
            bool sim_missing =
                s_svc.telemetry.usb_connected && !s_svc.telemetry.sim_ready;
            xSemaphoreGive(s_svc.lock);
            /* If no SIM is detected, periodically cycle CFUN=0 -> CFUN=1 every
             * 15 seconds so a newly inserted SIM card is powered up and read
             * automatically without needing a manual reset. */
            if (sim_missing && (tick % 15) == 0) {
                modem_repower_sim();
            }
            send_at_cmd("AT+CPIN?\r\n");
            vTaskDelay(pdMS_TO_TICKS(60));
            send_at_cmd("AT+CEREG?\r\n");
            vTaskDelay(pdMS_TO_TICKS(60));
            send_at_cmd("AT+CSQ\r\n");
            vTaskDelay(pdMS_TO_TICKS(60));
            send_at_cmd("AT+CESQ\r\n");
            vTaskDelay(pdMS_TO_TICKS(60));
            send_at_cmd("AT+COPS=3,2;+COPS?\r\n");
        }

        /* Poll battery gauge & SoC temperature every 2 seconds */
        if ((tick % 2) == 0) {
            poll_battery_and_temp();
        }

        reconcile_stations_with_driver();

        uint32_t rx_delta = atomic_exchange(&s_svc.rx_bytes_accum, 0);
        uint32_t tx_delta = atomic_exchange(&s_svc.tx_bytes_accum, 0);
        int64_t now_rate_us = esp_timer_get_time();

        xSemaphoreTake(s_svc.lock, portMAX_DELAY);
        int64_t dt_us = s_svc.last_rate_us > 0
                            ? (now_rate_us - s_svc.last_rate_us)
                            : 1000000LL;
        if (dt_us < 100000LL) {
            dt_us = 1000000LL;
        }
        s_svc.last_rate_us = now_rate_us;
        /* Only count real cellular WAN traffic when SIM is ready, registered,
         * and RNDIS link is up (ignore local probe traffic to an unprovisioned
         * ML307 router when no SIM card is inserted). */
        if (!s_svc.telemetry.sim_ready || !s_svc.telemetry.registered ||
            !s_svc.telemetry.link_up) {
            rx_delta = 0;
            tx_delta = 0;
        }
        /* Convert bytes per dt_us to 0.1 Mbps units:
         * tenths_mbps = (bytes * 8 * 10) / (dt_us). */
        uint32_t dl_tenths = (uint32_t)(((uint64_t)rx_delta * 80ULL + (uint64_t)(dt_us / 2)) / (uint64_t)dt_us);
        uint32_t ul_tenths = (uint32_t)(((uint64_t)tx_delta * 80ULL + (uint64_t)(dt_us / 2)) / (uint64_t)dt_us);
        if (rx_delta > 0 && dl_tenths == 0) {
            dl_tenths = 1; /* Show 0.1 Mbps for light active traffic */
        }
        if (tx_delta > 0 && ul_tenths == 0) {
            ul_tenths = 1;
        }
        s_svc.telemetry.download_tenths_mbps =
            dl_tenths > 9999u ? 9999u : (uint16_t)dl_tenths;
        s_svc.telemetry.upload_tenths_mbps =
            ul_tenths > 9999u ? 9999u : (uint16_t)ul_tenths;
        uint64_t delta_total = (uint64_t)rx_delta + (uint64_t)tx_delta;
        s_svc.total_wan_bytes += delta_total;
        if (delta_total > 0) {
            uint32_t active_cnt = 0;
            for (int i = 0; i < APP_MIFI_MAX_STATIONS; ++i) {
                if (s_svc.stations[i].active) {
                    ++active_cnt;
                }
            }
            if (active_cnt > 0) {
                uint64_t share = delta_total / active_cnt;
                for (int i = 0; i < APP_MIFI_MAX_STATIONS; ++i) {
                    if (s_svc.stations[i].active) {
                        s_svc.stations[i].used_bytes += share;
                    }
                }
            }
        }
        format_wan_usage(s_svc.total_wan_bytes, s_svc.telemetry.monthly_usage,
                         sizeof(s_svc.telemetry.monthly_usage));
        s_svc.telemetry.uptime_sec = (uint32_t)(now_rate_us / 1000000LL);
        uint64_t snapshot_bytes = s_svc.total_wan_bytes;
        refresh_stations_snapshot_locked();
        xSemaphoreGive(s_svc.lock);

        if ((tick % 10) == 0 && (rx_delta > 0 || tx_delta > 0)) {
            save_wan_bytes_to_nvs(snapshot_bytes);
        }
    }
}

/* Backend callbacks invoked by app_mifi UI controller */
static void cb_apply_settings(void *ctx, const app_mifi_settings_t *settings)
{
    (void)ctx;
    if (!settings) {
        return;
    }
    xSemaphoreTake(s_svc.lock, portMAX_DELAY);
    s_svc.settings = *settings;
    s_svc.settings_dirty = true;
    xSemaphoreGive(s_svc.lock);
}

static void cb_redial(void *ctx)
{
    (void)ctx;
    xSemaphoreTake(s_svc.lock, portMAX_DELAY);
    s_svc.redial_requested = true;
    xSemaphoreGive(s_svc.lock);
}

static void cb_block_mac(void *ctx, const char *mac, bool blocked)
{
    (void)ctx;
    if (!mac || !mac[0]) {
        return;
    }
    xSemaphoreTake(s_svc.lock, portMAX_DELAY);
    if (blocked) {
        if (!is_mac_blocked_locked(mac) && s_svc.blocked_count < MIFI_MAX_BLOCKED) {
            snprintf(s_svc.blocked_macs[s_svc.blocked_count++], 18, "%s", mac);
        }
        /* Kick station immediately if currently connected */
        for (int i = 0; i < APP_MIFI_MAX_STATIONS; ++i) {
            if (!s_svc.stations[i].active) {
                continue;
            }
            char m[18];
            format_mac_str(s_svc.stations[i].mac, m);
            if (strcasecmp(m, mac) == 0) {
                uint16_t aid = s_svc.stations[i].aid;
                memset(&s_svc.stations[i], 0, sizeof(s_svc.stations[i]));
                esp_wifi_deauth_sta(aid);
                break;
            }
        }
    } else {
        for (size_t i = 0; i < s_svc.blocked_count; ++i) {
            if (strcasecmp(s_svc.blocked_macs[i], mac) == 0) {
                memmove(&s_svc.blocked_macs[i], &s_svc.blocked_macs[i + 1],
                        (s_svc.blocked_count - i - 1) * 18);
                --s_svc.blocked_count;
                break;
            }
        }
    }
    xSemaphoreGive(s_svc.lock);
}

static void cb_factory_reset(void *ctx, app_mifi_settings_t *out_defaults)
{
    (void)ctx;
    xSemaphoreTake(s_svc.lock, portMAX_DELAY);
    default_settings(&s_svc.settings);
    s_svc.blocked_count = 0;
    s_svc.total_wan_bytes = 0;
    format_wan_usage(0, s_svc.telemetry.monthly_usage,
                     sizeof(s_svc.telemetry.monthly_usage));
    s_svc.settings_dirty = true;
    if (out_defaults) {
        *out_defaults = s_svc.settings;
    }
    xSemaphoreGive(s_svc.lock);
    save_wan_bytes_to_nvs(0);
}

static void cb_user_activity(void *ctx)
{
    (void)ctx;
    bool wake = false;
    uint8_t target_brightness = 70;
    xSemaphoreTake(s_svc.lock, portMAX_DELAY);
    s_svc.last_activity_us = esp_timer_get_time();
    target_brightness = s_svc.settings.brightness;
    if (s_svc.display_sleeping) {
        s_svc.display_sleeping = false;
        wake = true;
    }
    xSemaphoreGive(s_svc.lock);
    if (wake) {
        bsp_display_on();
        bsp_display_brightness_set(target_brightness);
    }
}

static bool cb_poll_telemetry(void *ctx, app_mifi_telemetry_t *out_telemetry,
                              uint8_t *out_actual_channel)
{
    (void)ctx;
    /* Never block the GSP/UI task on AT RX / worker lock contention. */
    if (xSemaphoreTake(s_svc.lock, pdMS_TO_TICKS(20)) != pdTRUE) {
        return false;
    }
    if (out_actual_channel) {
        *out_actual_channel = s_svc.settings.actual_channel;
    }
    bool valid = s_svc.telemetry.valid;
    if (valid && out_telemetry) {
        *out_telemetry = s_svc.telemetry;
    }
    xSemaphoreGive(s_svc.lock);
    return valid;
}

static esp_err_t wrapped_rndis_transmit(iot_eth_driver_t *driver,
                                        uint8_t *data, size_t len)
{
    esp_err_t ret = s_svc.orig_rndis_transmit
                        ? s_svc.orig_rndis_transmit(driver, data, len)
                        : ESP_FAIL;
    if (ret == ESP_OK) {
        atomic_fetch_add(&s_svc.tx_bytes_accum, (uint32_t)len);
    }
    return ret;
}

static esp_err_t wrapped_mediator_stack_input(iot_eth_mediator_t *mediator,
                                              uint8_t *data, size_t len)
{
    (void)mediator;
    atomic_fetch_add(&s_svc.rx_bytes_accum, (uint32_t)len);
    if (s_svc.orig_rndis_mediator && s_svc.orig_rndis_mediator->stack_input) {
        return s_svc.orig_rndis_mediator->stack_input(
            s_svc.orig_rndis_mediator, data, len);
    }
    free(data);
    return ESP_OK;
}

static esp_err_t wrapped_mediator_stack_input_info(
    iot_eth_mediator_t *mediator, uint8_t *data, size_t len, void *info)
{
    (void)mediator;
    atomic_fetch_add(&s_svc.rx_bytes_accum, (uint32_t)len);
    if (s_svc.orig_rndis_mediator &&
        s_svc.orig_rndis_mediator->stack_input_info) {
        return s_svc.orig_rndis_mediator->stack_input_info(
            s_svc.orig_rndis_mediator, data, len, info);
    }
    if (s_svc.orig_rndis_mediator && s_svc.orig_rndis_mediator->stack_input) {
        return s_svc.orig_rndis_mediator->stack_input(
            s_svc.orig_rndis_mediator, data, len);
    }
    free(data);
    return ESP_OK;
}

static esp_err_t wrapped_mediator_on_stage_changed(
    iot_eth_mediator_t *mediator, iot_eth_stage_t stage, void *arg)
{
    (void)mediator;
    if (s_svc.orig_rndis_mediator &&
        s_svc.orig_rndis_mediator->on_stage_changed) {
        return s_svc.orig_rndis_mediator->on_stage_changed(
            s_svc.orig_rndis_mediator, stage, arg);
    }
    return ESP_OK;
}

static esp_err_t init_usb_rndis_host(void)
{
    /* Above GSP render (prio 4) and USBH base (5), but below max so dialing
     * AT traffic cannot starve touch/UI when internal SRAM is tight. */
    usbh_cdc_driver_config_t cdc_cfg = {
        .task_stack_size = 4096,
        .task_priority = 12,
        .task_coreid = 0,
        .skip_init_usb_host_driver = false,
    };
    esp_err_t err = usbh_cdc_driver_install(&cdc_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "usbh_cdc_driver_install failed (%s)", esp_err_to_name(err));
        return err;
    }

    usb_device_match_id_t *cdc_match = calloc(2, sizeof(usb_device_match_id_t));
    if (!cdc_match) {
        return ESP_ERR_NO_MEM;
    }
    cdc_match[0].match_flags = USB_DEVICE_ID_MATCH_VID_PID;
    cdc_match[0].idVendor = USB_DEVICE_VENDOR_ANY;
    cdc_match[0].idProduct = USB_DEVICE_PRODUCT_ANY;
    err = usbh_cdc_register_dev_event_cb(cdc_match, cdc_device_event_cb, NULL);
    if (err != ESP_OK) {
        free(cdc_match);
        ESP_LOGW(TAG, "usbh_cdc_register_dev_event_cb failed (%s)",
                 esp_err_to_name(err));
        return err;
    }
    /* Callback keeps the match list pointer for the driver lifetime. */

    usb_device_match_id_t *match_list = calloc(2, sizeof(usb_device_match_id_t));
    if (!match_list) {
        return ESP_ERR_NO_MEM;
    }
    match_list[0].match_flags = USB_DEVICE_ID_MATCH_VID_PID;
    match_list[0].idVendor = USB_DEVICE_VENDOR_ANY;
    match_list[0].idProduct = USB_DEVICE_PRODUCT_ANY;

    iot_usbh_rndis_config_t rndis_cfg = {
        .match_id_list = match_list,
    };
    err = iot_eth_new_usb_rndis(&rndis_cfg, &s_svc.rndis_drv);
    if (err != ESP_OK || !s_svc.rndis_drv) {
        free(match_list);
        ESP_LOGW(TAG, "iot_eth_new_usb_rndis failed (%s)", esp_err_to_name(err));
        return err;
    }

    s_svc.orig_rndis_transmit = s_svc.rndis_drv->transmit;
    s_svc.rndis_drv->transmit = wrapped_rndis_transmit;

    iot_eth_config_t eth_cfg = {
        .driver = s_svc.rndis_drv,
        .stack_input = NULL,
    };
    err = iot_eth_install(&eth_cfg, &s_svc.eth_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "iot_eth_install failed (%s)", esp_err_to_name(err));
        return err;
    }

    /* Wrap mediator after iot_eth_install so RX frames are counted for live speed. */
    if (s_svc.rndis_drv->set_mediator) {
        typedef struct {
            iot_eth_driver_t *driver;
            iot_eth_mediator_t mediator;
        } iot_eth_prefix_t;
        iot_eth_prefix_t *eth_priv = (iot_eth_prefix_t *)s_svc.eth_handle;
        s_svc.orig_rndis_mediator = &eth_priv->mediator;
        s_svc.wrapped_mediator.stack_input = wrapped_mediator_stack_input;
        s_svc.wrapped_mediator.stack_input_info =
            wrapped_mediator_stack_input_info;
        s_svc.wrapped_mediator.on_stage_changed =
            wrapped_mediator_on_stage_changed;
        s_svc.rndis_drv->set_mediator(s_svc.rndis_drv, &s_svc.wrapped_mediator);
    }

    esp_netif_inherent_config_t base_cfg = ESP_NETIF_INHERENT_DEFAULT_ETH();
    base_cfg.if_key = "USB_RNDIS_4G";
    base_cfg.if_desc = "usb_rndis_4g";
    base_cfg.route_prio = 50;
    esp_netif_config_t netif_cfg = {
        .base = &base_cfg,
        .driver = NULL,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
    };
    s_svc.rndis_netif = esp_netif_new(&netif_cfg);
    if (!s_svc.rndis_netif) {
        return ESP_ERR_NO_MEM;
    }
    s_svc.eth_glue = iot_eth_new_netif_glue(s_svc.eth_handle);
    if (!s_svc.eth_glue) {
        return ESP_ERR_NO_MEM;
    }
    esp_netif_attach(s_svc.rndis_netif, s_svc.eth_glue);
    err = iot_eth_start(s_svc.eth_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "USB Host RNDIS started (waiting for ML307)");
    }
    return err;
}

esp_err_t mifi_service_start(app_mifi_handle_t ui_controller)
{
    memset(&s_svc, 0, sizeof(s_svc));
    s_svc.lock = xSemaphoreCreateMutex();
    if (!s_svc.lock) {
        return ESP_ERR_NO_MEM;
    }
    s_svc.last_activity_us = esp_timer_get_time();

    load_settings_from_nvs(&s_svc.settings);
    /* Keep expansion-slot rails up; some 4G fixtures share board power control. */
    if (bsp_power_init() == ESP_OK) {
        (void)bsp_power_set_vcc_3v3(true);
    }
    bsp_battery_init();
    if (bsp_imu_init() == ESP_OK) {
        (void)bsp_imu_start(NULL);
    }
    temperature_sensor_config_t tsens_cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 80);
    if (temperature_sensor_install(&tsens_cfg, &s_svc.tsens) == ESP_OK) {
        temperature_sensor_enable(s_svc.tsens);
    }

    s_svc.telemetry.valid = true;
    s_svc.telemetry.usb_connected = false;
    s_svc.telemetry.sim_ready = false;
    s_svc.telemetry.registered = false;
    s_svc.telemetry.link_up = false;
    snprintf(s_svc.telemetry.modem_fw, sizeof(s_svc.telemetry.modem_fw),
             "ML307C-STD");
    push_log_event_locked("系统初始化完成", "System initialized");
    push_log_event_locked("USB Host 驱动已就绪", "USB Host driver ready");
    push_log_event_locked("Wi-Fi 热点服务已启动", "Wi-Fi SoftAP started");
    format_wan_usage(s_svc.total_wan_bytes, s_svc.telemetry.monthly_usage,
                     sizeof(s_svc.telemetry.monthly_usage));
    poll_battery_and_temp();
    bsp_display_brightness_set(s_svc.settings.brightness);

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "esp_netif_init: %s", esp_err_to_name(err));
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "esp_event_loop_create_default: %s", esp_err_to_name(err));
    }

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                               wifi_and_net_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID,
                               wifi_and_net_event_handler, NULL);
    esp_event_handler_register(IOT_ETH_EVENT, ESP_EVENT_ANY_ID,
                               wifi_and_net_event_handler, NULL);

    s_svc.ap_netif = esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&wifi_init_cfg) == ESP_OK) {
        esp_wifi_set_storage(WIFI_STORAGE_RAM);
        esp_wifi_set_mode(WIFI_MODE_AP);
        apply_softap_config(&s_svc.settings, &s_svc.settings.actual_channel);
    }

    init_usb_rndis_host();

    const app_mifi_backend_t backend = {
        .ctx = &s_svc,
        .apply_settings = cb_apply_settings,
        .redial = cb_redial,
        .block_mac = cb_block_mac,
        .factory_reset = cb_factory_reset,
        .user_activity = cb_user_activity,
        .poll_telemetry = cb_poll_telemetry,
    };
    app_mifi_set_backend(ui_controller, &backend, &s_svc.settings);

    xTaskCreate(service_worker_task, "mifi_svc", 6144, NULL, 4, NULL);
    ESP_LOGI(TAG, "MiFi hardware service started (SoftAP + USB Host RNDIS)");
    return ESP_OK;
}
