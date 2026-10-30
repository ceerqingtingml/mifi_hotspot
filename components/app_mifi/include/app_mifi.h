#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_gsp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct app_mifi *app_mifi_handle_t;

#define APP_MIFI_MAX_STATIONS 10

typedef struct {
    char name[32];
    char mac[18];
    char ip[16];
    char usage[16];
    char online[16];
    uint8_t kind; /* 0:phone, 1:laptop, 2:tablet, 3:watch, 4:camera */
} app_mifi_station_info_t;

typedef struct {
    bool valid;
    bool usb_connected;
    bool sim_ready;
    bool registered;
    bool link_up;
    char operator_zh[32];
    char operator_en[32];
    char band_info[32];
    char wan_ip[16];
    int16_t rsrp_dbm;
    int16_t sinr_db;
    uint16_t download_tenths_mbps;
    uint16_t upload_tenths_mbps;
    char monthly_usage[20];
    uint8_t battery_percent;
    int16_t battery_mv;
    int16_t battery_current_ma;
    int16_t temperature_c;
    uint16_t time_to_empty_min;
    uint16_t time_to_full_min;
    bool charging;
    uint32_t uptime_sec;
    uint8_t drop_count;
    char modem_fw[24];
    char logs_zh[4][48];
    char logs_en[4][48];
    uint8_t station_count;
    app_mifi_station_info_t stations[APP_MIFI_MAX_STATIONS];
} app_mifi_telemetry_t;

typedef struct {
    char ssid[33];
    char password[64];
    uint8_t auth_mode;     /* 0:Open, 1:WPA2, 2:WPA3, 3:WPA/WPA2, 4:WPA2/WPA3 */
    uint8_t channel_mode;  /* 0:Auto, 1..13:Channel 1..13 */
    uint8_t actual_channel;
    uint8_t max_conn_mode; /* 0:4, 1:8, 2:10 */
    bool hotspot_enabled;
    bool all_paused;
    bool power_save;
    bool roaming;
    uint8_t brightness;
    uint8_t sleep_mode;    /* 0:15s, 1:30s, 2:1m, 3:5m, 4:Never */
    uint8_t schedule_mode;
} app_mifi_settings_t;

typedef struct {
    void *ctx;
    void (*apply_settings)(void *ctx, const app_mifi_settings_t *settings);
    void (*redial)(void *ctx);
    void (*block_mac)(void *ctx, const char *mac, bool blocked);
    void (*factory_reset)(void *ctx, app_mifi_settings_t *out_defaults);
    void (*user_activity)(void *ctx);
    bool (*poll_telemetry)(void *ctx, app_mifi_telemetry_t *out_telemetry,
                           uint8_t *out_actual_channel);
} app_mifi_backend_t;

typedef struct {
    uint16_t download_tenths_mbps;
    uint16_t upload_tenths_mbps;
    bool hotspot_enabled;
    bool redialing;
    esp_gsp_err_t last_error;
} app_mifi_snapshot_t;

esp_gsp_err_t app_mifi_create(esp_gsp_handle_t ui, app_mifi_handle_t *out_handle);
void app_mifi_set_backend(app_mifi_handle_t handle,
                          const app_mifi_backend_t *backend,
                          const app_mifi_settings_t *initial_settings);
esp_gsp_err_t app_mifi_read(app_mifi_handle_t handle,
                            app_mifi_snapshot_t *out_snapshot);
esp_gsp_err_t app_mifi_destroy(app_mifi_handle_t *handle);

#ifdef __cplusplus
}
#endif
