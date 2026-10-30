// SPDX-License-Identifier: Apache-2.0

#include "app_mifi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bundle_gsp.h"
#include "wifi_qr.inc"
#if defined(__has_include) && __has_include("freertos/FreeRTOS.h")
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define MIFI_YIELD_QUEUE() vTaskDelay(pdMS_TO_TICKS(5))
#else
#define MIFI_YIELD_QUEUE() ((void)0)
#endif

#define KEY_SHIFT 0x100u
#define KEY_SYMBOL 0x102u
#define KEY_LETTER 0x103u
#define KEY_DELETE 0x08u
#define KEY_OK 0x0Du

/* Soft-AP modes ESP32-S31 accepts, in dropdown order:
 * OPEN, WPA2_PSK, WPA3_PSK, WPA_WPA2_PSK, WPA2_WPA3_PSK.
 */
#define AUTH_MODE_COUNT 5
#define AUTH_MODE_DEFAULT 4
#define BRIGHTNESS_MIN 10
#define BRIGHTNESS_MAX 100
#define BRIGHTNESS_DEFAULT 70
#define BRIGHTNESS_POWER_SAVE 40
#define SLEEP_MODE_COUNT 5
#define SLEEP_MODE_DEFAULT 4
#define STATION_COUNT 8
#define EXTRA_BLOCKED_INDEX 0xFEu
#define SCHEDULE_MODE_COUNT 4
#define QUOTA_MODE_COUNT 4
#define MAX_CONN_MODE_COUNT 3
#define CHANNEL_MODE_COUNT 14
#define CHANNEL_MODE_DEFAULT 6

static const char *const max_conn_modes_zh[MAX_CONN_MODE_COUNT] = {
    "4 台  ›",
    "8 台  ›",
    "10 台  ›",
};

static const char *const max_conn_modes_en[MAX_CONN_MODE_COUNT] = {
    "4  ›",
    "8  ›",
    "10  ›",
};

static const char *const schedule_modes_zh[SCHEDULE_MODE_COUNT] = {
    "　　　 上网时间管理",
    "　　　 上网时间 · 夜间禁网",
    "　　　 上网时间 · 限 2h",
    "　　　 上网时间 · 限 1h",
};

static const char *const schedule_modes_en[SCHEDULE_MODE_COUNT] = {
    "　　　 Time schedule",
    "　　　 Time · 23-07 off",
    "　　　 Time · 2h limit",
    "　　　 Time · 1h limit",
};

static const char *const quota_modes_zh[QUOTA_MODE_COUNT] = {
    "不限时  ›",
    "限时 30 分钟  ›",
    "限时 1 小时  ›",
    "限时 2 小时  ›",
};

static const char *const quota_modes_en[QUOTA_MODE_COUNT] = {
    "No limit  ›",
    "30 min  ›",
    "1 hour  ›",
    "2 hours  ›",
};

static const char *const sleep_modes_zh[SLEEP_MODE_COUNT] = {
    "15 秒  ›",
    "30 秒  ›",
    "1 分钟  ›",
    "5 分钟  ›",
    "常亮  ›",
};

static const char *const sleep_modes_en[SLEEP_MODE_COUNT] = {
    "15 sec  ›",
    "30 sec  ›",
    "1 min  ›",
    "5 min  ›",
    "Never  ›",
};

static const char *const auth_modes[AUTH_MODE_COUNT] = {
    "Open System   ",
    "WPA2-Personal   ",
    "WPA3-Personal   ",
    "WPA/WPA2-Personal   ",
    "WPA2/WPA3-Personal   ",
};

#define ANIMATION_TICK_MS 200
#define SPEED_TICK_DIVISOR 5
/* While RNDIS is dialing, refresh telemetry less often so nav stays responsive. */
#define SPEED_TICK_DIVISOR_DIALING 15
#define WAVE_FRAME_COUNT 6
#define REDIAL_TICKS 12

typedef enum {
    PAGE_HOME,
    PAGE_WIFI,
    PAGE_DEVICES,
    PAGE_CELLULAR,
    PAGE_SYSTEM,
    PAGE_DIAGNOSTICS,
    PAGE_UPDATE,
    PAGE_LOGS,
    PAGE_ABOUT,
    PAGE_LICENSES,
    PAGE_PRIVACY,
    PAGE_HOTSPOT,
    PAGE_COUNT,
} mifi_page_t;

struct app_mifi {
    esp_gsp_handle_t ui;
    void *timer;
    mifi_page_t page;
    uint8_t animation_frame;
    uint8_t timer_ticks;
    uint8_t speed_sample;
    uint8_t redial_ticks;
    uint8_t diag_ticks;
    uint8_t update_ticks;
    uint16_t download_tenths_mbps;
    uint16_t upload_tenths_mbps;
    uint16_t speed_history[SPEED_HISTORY_LEN];
    bool chart_had_nonzero;
    bool hotspot_enabled;
    bool english;
    bool devices_dirty;
    bool password_visible;
    bool editing_password;
    bool keys_upper;
    bool security_open;
    bool channel_open;
    bool qr_modal_open;
    bool reset_modal_open;
    bool device_modal_open;
    bool blacklist_modal_open;
    bool power_save;
    bool roaming;
    bool all_paused;
    bool default_blocked_active;
    bool station_blocked[STATION_COUNT];
    uint8_t station_rate[STATION_COUNT];
    uint8_t station_quota[STATION_COUNT];
    uint8_t selected_station;
    uint8_t schedule_mode;
    uint8_t max_conn_mode;
    uint8_t channel_mode;
    uint8_t auth_mode;
    uint8_t brightness;
    uint8_t normal_brightness;
    uint8_t sleep_mode;
    uint8_t normal_sleep_mode;
    char hotspot_name[33];
    char hotspot_password[64];
    char draft_name[33];
    char draft_pass[64];
    uint8_t actual_channel;
    char blocked_hw_names[3][32];
    char blocked_hw_macs[3][18];
    uint8_t blocked_hw_count;
    app_mifi_backend_t backend;
    bool has_backend;
    app_mifi_telemetry_t telemetry;
    wifi_qr_t qr;
    esp_gsp_list_t devices;
    esp_gsp_err_t last_error;
};

typedef enum {
    STATION_UNLIMITED,
    STATION_PRIORITY,
    STATION_LIMIT,
} station_status_t;

typedef enum {
    DEVICE_PHONE,
    DEVICE_LAPTOP,
    DEVICE_TABLET,
    DEVICE_WATCH,
    DEVICE_CAMERA,
} device_kind_t;

typedef struct {
    const char *name_zh;
    const char *name_en;
    const char *mac;
    const char *ip;
    const char *usage;
    const char *online;
    const char *status_zh;
    const char *status_en;
    station_status_t status;
    device_kind_t kind;
} mifi_station_t;

static const mifi_station_t s_stations[STATION_COUNT] = {
    {"Mali 的手机", "Mali's Phone", "3C:22:FB:A1:10:21", "192.168.4.2", "620 MB", "1h 24m", "不限速", "Unlimited", STATION_UNLIMITED, DEVICE_PHONE},
    {"MacBook Pro", "MacBook Pro", "F0:18:98:44:2A:07", "192.168.4.3", "1.8 GB", "3h 10m", "优先", "Priority", STATION_PRIORITY, DEVICE_LAPTOP},
    {"客厅平板", "Living Room Tablet", "A4:83:E7:19:55:C2", "192.168.4.4", "340 MB", "48m", "限速 2M", "Limit 2M", STATION_LIMIT, DEVICE_TABLET},
    {"书房电脑", "Study PC", "B8:27:EB:03:11:90", "192.168.4.5", "2.4 GB", "5h 02m", "不限速", "Unlimited", STATION_UNLIMITED, DEVICE_LAPTOP},
    {"客厅手表", "Living Room Watch", "D0:03:4B:77:10:18", "192.168.4.6", "28 MB", "2h 15m", "限速 1M", "Limit 1M", STATION_LIMIT, DEVICE_WATCH},
    {"门口摄像头", "Door Camera", "28:6C:07:90:12:44", "192.168.4.7", "1.1 GB", "6h 40m", "优先", "Priority", STATION_PRIORITY, DEVICE_CAMERA},
    {"访客手机", "Guest Phone", "64:A2:F9:0C:33:AB", "192.168.4.8", "86 MB", "22m", "限速 2M", "Limit 2M", STATION_LIMIT, DEVICE_PHONE},
    {"iPad mini", "iPad mini", "3C:22:FB:B0:19:6E", "192.168.4.9", "510 MB", "1h 05m", "不限速", "Unlimited", STATION_UNLIMITED, DEVICE_TABLET},
};

#include "device_icons.inc"

typedef esp_gsp_err_t (*visible_setter_t)(esp_gsp_handle_t, bool);

static const uint16_t download_samples[] = {
    124, 182, 247, 319, 278, 361, 428, 387, 452, 405, 476, 438,
};
static const uint16_t upload_samples[] = {
    21, 32, 28, 45, 39, 53, 48, 61, 55, 47, 64, 58,
};

static visible_setter_t const page_visible[PAGE_COUNT] = {
    gsp_mifi_home_set_visible,
    gsp_mifi_wifi_set_visible,
    gsp_mifi_devices_set_visible,
    gsp_mifi_cellular_set_visible,
    gsp_mifi_system_set_visible,
    gsp_mifi_diagnostics_set_visible,
    gsp_mifi_update_set_visible,
    gsp_mifi_logs_set_visible,
    gsp_mifi_about_set_visible,
    gsp_mifi_licenses_set_visible,
    gsp_mifi_privacy_set_visible,
    gsp_mifi_hotspot_set_visible,
};

static void remember_error(struct app_mifi *state, esp_gsp_err_t err)
{
    if (err == ESP_GSP_ERR_TIMEOUT) {
        MIFI_YIELD_QUEUE();
        return;
    }
    if (err != ESP_GSP_OK) {
        state->last_error = err;
    }
}

#include "ui_strings.inc"

static const char *tr(const struct app_mifi *state, const char *zh, const char *en)
{
    return state->english ? en : zh;
}

static void update_network_status(struct app_mifi *state)
{
    const char *text = NULL;
    if (state->redial_ticks > 0) {
        text = tr(state, "●  正在重拨", "●  Redialing");
    } else if (state->has_backend && state->telemetry.valid) {
        if (!state->telemetry.usb_connected) {
            text = tr(state, "●  请接入 4G 子板", "●  Insert 4G module");
        } else if (!state->telemetry.sim_ready) {
            text = tr(state, "●  请插入 SIM 卡", "●  Insert SIM card");
        } else if (!state->telemetry.registered || !state->telemetry.link_up) {
            text = tr(state, "●  正在拨号...", "●  Dialing...");
        } else {
            text = tr(state, "●  网络良好", "●  Connected");
        }
    } else {
        text = tr(state, "●  网络良好", "●  Connected");
    }
    remember_error(state, gsp_mifi_network_status_set_text(state->ui, text));
}

static void show_brightness(struct app_mifi *state)
{
    char text[8];
    snprintf(text, sizeof(text), "%u%%", state->brightness);
    remember_error(state, gsp_mifi_brightness_value_set_text(state->ui, text));
}

static void refresh_power_controls(struct app_mifi *state)
{
    remember_error(state, gsp_mifi_power_save_on_set_visible(
                              state->ui, state->power_save));
    remember_error(state, gsp_mifi_power_save_off_set_visible(
                              state->ui, !state->power_save));
    remember_error(state, gsp_mifi_roaming_on_set_visible(
                              state->ui, state->roaming));
    remember_error(state, gsp_mifi_roaming_off_set_visible(
                              state->ui, !state->roaming));
    remember_error(state, gsp_mifi_sleep_value_set_text(
                              state->ui,
                              state->english
                                  ? sleep_modes_en[state->sleep_mode]
                                  : sleep_modes_zh[state->sleep_mode]));
}

static void set_security_menu(struct app_mifi *state, bool open)
{
    state->security_open = open;
    remember_error(state, gsp_mifi_security_menu_set_visible(state->ui, open));
}

static void set_channel_menu(struct app_mifi *state, bool open)
{
    state->channel_open = open;
    remember_error(state, gsp_mifi_channel_menu_set_visible(state->ui, open));
}

static void set_qr_modal(struct app_mifi *state, bool open)
{
    state->qr_modal_open = open;
    remember_error(state, gsp_mifi_qr_modal_set_visible(state->ui, open));
}

static void set_reset_modal(struct app_mifi *state, bool open)
{
    state->reset_modal_open = open;
    remember_error(state, gsp_mifi_reset_modal_set_visible(state->ui, open));
}

static void update_station_list(struct app_mifi *state);

static void set_device_modal(struct app_mifi *state, bool open)
{
    state->device_modal_open = open;
    remember_error(state, gsp_mifi_device_modal_set_visible(state->ui, open));
    update_station_list(state);
}

static void set_blacklist_modal(struct app_mifi *state, bool open)
{
    state->blacklist_modal_open = open;
    remember_error(state, gsp_mifi_blacklist_modal_set_visible(state->ui, open));
    update_station_list(state);
}

static void notify_backend_settings(struct app_mifi *state)
{
    if (!state->has_backend || !state->backend.apply_settings) {
        return;
    }
    app_mifi_settings_t cfg = {0};
    snprintf(cfg.ssid, sizeof(cfg.ssid), "%s", state->hotspot_name);
    snprintf(cfg.password, sizeof(cfg.password), "%s", state->hotspot_password);
    cfg.auth_mode = state->auth_mode;
    cfg.channel_mode = state->channel_mode;
    cfg.actual_channel = state->actual_channel ? state->actual_channel : 6;
    cfg.max_conn_mode = state->max_conn_mode;
    cfg.hotspot_enabled = state->hotspot_enabled;
    cfg.all_paused = state->all_paused;
    cfg.power_save = state->power_save;
    cfg.roaming = state->roaming;
    cfg.brightness = state->brightness;
    cfg.sleep_mode = state->sleep_mode;
    cfg.schedule_mode = state->schedule_mode;
    state->backend.apply_settings(state->backend.ctx, &cfg);
}

static uint32_t online_station_count(const struct app_mifi *state)
{
    if (!state->hotspot_enabled) {
        return 0;
    }
    if (state->has_backend && state->telemetry.valid) {
        return state->telemetry.station_count;
    }
    uint32_t count = 0;
    for (uint32_t i = 0; i < STATION_COUNT; ++i) {
        if (!state->station_blocked[i]) {
            ++count;
        }
    }
    return count;
}

static int station_index_for_row(const struct app_mifi *state, uint32_t row_item)
{
    if (state->has_backend && state->telemetry.valid) {
        return row_item < state->telemetry.station_count ? (int)row_item : -1;
    }
    uint32_t visible = 0;
    for (uint32_t i = 0; i < STATION_COUNT; ++i) {
        if (state->station_blocked[i]) {
            continue;
        }
        if (visible == row_item) {
            return (int)i;
        }
        ++visible;
    }
    return -1;
}

static uint32_t blocked_station_count(const struct app_mifi *state)
{
    if (state->has_backend) {
        return state->blocked_hw_count;
    }
    uint32_t count = state->default_blocked_active ? 1u : 0u;
    for (uint32_t i = 0; i < STATION_COUNT; ++i) {
        if (state->station_blocked[i]) {
            ++count;
        }
    }
    return count;
}

static void refresh_device_detail(struct app_mifi *state)
{
    char meta[56];
    char online[64];
    const uint8_t idx = state->selected_station;

    if (state->has_backend && state->telemetry.valid) {
        if (idx >= state->telemetry.station_count) {
            return;
        }
        const app_mifi_station_info_t *info = &state->telemetry.stations[idx];
        const char *name = info->name[0] ? info->name
                                         : tr(state, "接入设备", "Client");
        snprintf(meta, sizeof(meta), "MAC  %s  ·  %s", info->mac, info->ip);
        if (state->english) {
            snprintf(online, sizeof(online), "Online %s  ·  Used %s",
                     info->online, info->usage);
        } else {
            snprintf(online, sizeof(online), "已连 %s  ·  已用 %s",
                     info->online, info->usage);
        }
        remember_error(state, gsp_mifi_detail_name_set_text(state->ui, name));
        remember_error(state, gsp_mifi_detail_meta_set_text(state->ui, meta));
        remember_error(state, gsp_mifi_detail_online_set_text(state->ui, online));
        const uint8_t quota =
            state->station_quota[idx % STATION_COUNT] % QUOTA_MODE_COUNT;
        remember_error(state, gsp_mifi_detail_quota_value_set_text(
                                  state->ui,
                                  state->english ? quota_modes_en[quota]
                                                 : quota_modes_zh[quota]));
        return;
    }

    if (idx >= STATION_COUNT) {
        return;
    }
    const mifi_station_t *station = &s_stations[idx];
    snprintf(meta, sizeof(meta), "MAC  %s  ·  %s", station->mac, station->ip);
    if (state->english) {
        snprintf(online, sizeof(online), "Online %s  ·  Used %s",
                 station->online, station->usage);
    } else {
        snprintf(online, sizeof(online), "已连 %s  ·  已用 %s",
                 station->online, station->usage);
    }
    remember_error(state, gsp_mifi_detail_name_set_text(
                              state->ui, tr(state, station->name_zh, station->name_en)));
    remember_error(state, gsp_mifi_detail_meta_set_text(state->ui, meta));
    remember_error(state, gsp_mifi_detail_online_set_text(state->ui, online));
    const uint8_t quota = state->station_quota[idx] % QUOTA_MODE_COUNT;
    remember_error(state, gsp_mifi_detail_quota_value_set_text(
                              state->ui,
                              state->english ? quota_modes_en[quota]
                                             : quota_modes_zh[quota]));
}

static void blocked_slot_entry(const struct app_mifi *state, uint32_t slot,
                               uint8_t *out_code)
{
    uint32_t current = 0;
    if (state->default_blocked_active) {
        if (slot == 0) {
            *out_code = EXTRA_BLOCKED_INDEX;
            return;
        }
        current = 1;
    }
    for (uint8_t i = 0; i < STATION_COUNT; ++i) {
        if (!state->station_blocked[i]) {
            continue;
        }
        if (current == slot) {
            *out_code = i;
            return;
        }
        ++current;
    }
    *out_code = 0xFFu;
}

static void refresh_blacklist_modal(struct app_mifi *state)
{
    static esp_gsp_err_t (*const row_vis[3])(esp_gsp_handle_t, bool) = {
        gsp_mifi_bl_row_0_set_visible,
        gsp_mifi_bl_row_1_set_visible,
        gsp_mifi_bl_row_2_set_visible,
    };
    static esp_gsp_err_t (*const row_name[3])(esp_gsp_handle_t, const char *) = {
        gsp_mifi_bl_name_0_set_text,
        gsp_mifi_bl_name_1_set_text,
        gsp_mifi_bl_name_2_set_text,
    };
    static esp_gsp_err_t (*const row_mac[3])(esp_gsp_handle_t, const char *) = {
        gsp_mifi_bl_mac_0_set_text,
        gsp_mifi_bl_mac_1_set_text,
        gsp_mifi_bl_mac_2_set_text,
    };
    if (state->has_backend) {
        uint32_t shown = 0;
        for (uint32_t slot = 0; slot < 3; ++slot) {
            if (slot >= state->blocked_hw_count) {
                remember_error(state, row_vis[slot](state->ui, false));
                continue;
            }
            ++shown;
            char mac_line[32];
            snprintf(mac_line, sizeof(mac_line), "MAC  %s",
                     state->blocked_hw_macs[slot]);
            remember_error(state, row_vis[slot](state->ui, true));
            remember_error(state, row_name[slot](
                                      state->ui, state->blocked_hw_names[slot]));
            remember_error(state, row_mac[slot](state->ui, mac_line));
        }
        remember_error(state,
                       gsp_mifi_bl_empty_box_set_visible(state->ui, shown == 0));
        return;
    }
    uint32_t shown = 0;
    for (uint32_t slot = 0; slot < 3; ++slot) {
        uint8_t code = 0xFFu;
        blocked_slot_entry(state, slot, &code);
        if (code == 0xFFu) {
            remember_error(state, row_vis[slot](state->ui, false));
            continue;
        }
        ++shown;
        remember_error(state, row_vis[slot](state->ui, true));
        if (code == EXTRA_BLOCKED_INDEX) {
            remember_error(state, row_name[slot](
                                      state->ui, tr(state, "未知设备", "UnknownPhone")));
            remember_error(state, row_mac[slot](
                                      state->ui, "MAC  A8:9C:ED:41:02:19"));
        } else {
            char mac_line[32];
            snprintf(mac_line, sizeof(mac_line), "MAC  %s", s_stations[code].mac);
            remember_error(state, row_name[slot](
                                      state->ui,
                                      tr(state, s_stations[code].name_zh,
                                         s_stations[code].name_en)));
            remember_error(state, row_mac[slot](state->ui, mac_line));
        }
    }
    remember_error(state, gsp_mifi_bl_empty_box_set_visible(state->ui, shown == 0));
}

static void refresh_device_footer(struct app_mifi *state)
{
    char bl_text[40];
    const uint32_t blocked = blocked_station_count(state);
    snprintf(bl_text, sizeof(bl_text),
             state->english ? "　　　 Blocklist  %u" : "　　　 黑名单  %u",
             (unsigned)blocked);
    remember_error(state, gsp_mifi_blacklist_button_set_text(state->ui, bl_text));
    const uint8_t sched = state->schedule_mode % SCHEDULE_MODE_COUNT;
    remember_error(state, gsp_mifi_schedule_button_set_text(
                              state->ui,
                              state->english ? schedule_modes_en[sched]
                                             : schedule_modes_zh[sched]));
    remember_error(state, gsp_mifi_pause_button_set_text(
                              state->ui,
                              state->all_paused
                                  ? tr(state, "　　　恢复所有设备", "　　　Resume all")
                                  : tr(state, "　　　暂停所有设备", "　　　Pause all")));
}

static void refresh_security(struct app_mifi *state)
{
    static esp_gsp_err_t (*const selected[AUTH_MODE_COUNT])(esp_gsp_handle_t, bool) = {
        gsp_mifi_sec_on_0_set_visible,
        gsp_mifi_sec_on_1_set_visible,
        gsp_mifi_sec_on_2_set_visible,
        gsp_mifi_sec_on_3_set_visible,
        gsp_mifi_sec_on_4_set_visible,
    };
    remember_error(state, gsp_mifi_wifi_security_value_set_text(
                              state->ui, auth_modes[state->auth_mode]));
    for (uint8_t index = 0; index < AUTH_MODE_COUNT; ++index) {
        remember_error(state, selected[index](state->ui, index == state->auth_mode));
    }
}

static void refresh_max_conn(struct app_mifi *state)
{
    const uint8_t idx = state->max_conn_mode % MAX_CONN_MODE_COUNT;
    remember_error(state, gsp_mifi_wifi_max_conn_value_set_text(
                              state->ui,
                              state->english ? max_conn_modes_en[idx]
                                             : max_conn_modes_zh[idx]));
}

static void refresh_channel(struct app_mifi *state)
{
    static esp_gsp_err_t (*const selected[CHANNEL_MODE_COUNT])(esp_gsp_handle_t, bool) = {
        gsp_mifi_ch_on_0_set_visible,
        gsp_mifi_ch_on_1_set_visible,
        gsp_mifi_ch_on_2_set_visible,
        gsp_mifi_ch_on_3_set_visible,
        gsp_mifi_ch_on_4_set_visible,
        gsp_mifi_ch_on_5_set_visible,
        gsp_mifi_ch_on_6_set_visible,
        gsp_mifi_ch_on_7_set_visible,
        gsp_mifi_ch_on_8_set_visible,
        gsp_mifi_ch_on_9_set_visible,
        gsp_mifi_ch_on_10_set_visible,
        gsp_mifi_ch_on_11_set_visible,
        gsp_mifi_ch_on_12_set_visible,
        gsp_mifi_ch_on_13_set_visible,
    };
    const uint8_t ch = state->channel_mode % CHANNEL_MODE_COUNT;
    const uint8_t auto_ch = state->actual_channel ? state->actual_channel : 6;
    char text[24];
    if (ch == 0) {
        snprintf(text, sizeof(text),
                 state->english ? "Auto (%u)   " : "自动 (%u)   ",
                 (unsigned)auto_ch);
    } else {
        snprintf(text, sizeof(text),
                 state->english ? "Ch %u   " : "信道 %u   ", (unsigned)ch);
    }
    remember_error(state, gsp_mifi_wifi_channel_value_set_text(state->ui, text));
    for (uint8_t i = 0; i < CHANNEL_MODE_COUNT; ++i) {
        remember_error(state, selected[i](state->ui, i == ch));
    }
}

static void update_wifi_qr(struct app_mifi *state)
{
    char payload[128];
    if (state->auth_mode == 0) {
        snprintf(payload, sizeof(payload), "WIFI:T:nopass;S:%s;;",
                 state->hotspot_name);
    } else {
        snprintf(payload, sizeof(payload), "WIFI:T:WPA;S:%s;P:%s;;",
                 state->hotspot_name, state->hotspot_password);
    }
    wifi_qr_encode(&state->qr, payload);
    uint8_t *qoi = malloc(12288);
    if (qoi) {
        size_t qoi_len = wifi_qr_to_qoi(&state->qr, qoi, 12288);
        if (qoi_len > 0) {
            remember_error(state, gsp_mifi_wifi_qr_surface_set_image(
                                      state->ui, qoi, qoi_len));
        }
        free(qoi);
    }
    uint8_t *large_qoi = malloc(49152);
    if (large_qoi) {
        size_t large_len = wifi_qr_to_qoi_sized(&state->qr, 200, large_qoi, 49152);
        if (large_len > 0) {
            remember_error(state, gsp_mifi_wifi_qr_large_surface_set_image(
                                      state->ui, large_qoi, large_len));
        }
        free(large_qoi);
    }
}

static void set_page(struct app_mifi *state, mifi_page_t page)
{
    for (unsigned i = 0; i < PAGE_COUNT; ++i) {
        remember_error(state, page_visible[i](state->ui, i == (unsigned)page));
    }
    state->page = page;
    if (page != PAGE_WIFI) {
        set_security_menu(state, false);
        set_channel_menu(state, false);
        set_qr_modal(state, false);
    }
    if (page != PAGE_SYSTEM) {
        set_reset_modal(state, false);
    }
    if (page != PAGE_DEVICES) {
        set_device_modal(state, false);
        set_blacklist_modal(state, false);
    }
    update_station_list(state);
}

static void update_speed_chart(struct app_mifi *state)
{
    for (int i = 0; i < SPEED_HISTORY_LEN - 1; ++i) {
        state->speed_history[i] = state->speed_history[i + 1];
    }
    state->speed_history[SPEED_HISTORY_LEN - 1] = state->download_tenths_mbps;

    if (state->page != PAGE_HOME) {
        return;
    }

    bool any_nonzero = false;
    for (int i = 0; i < SPEED_HISTORY_LEN; ++i) {
        if (state->speed_history[i] > 0) {
            any_nonzero = true;
            break;
        }
    }
    /* Skip repeat submissions while the live history stays at 0.0 Mbps. */
    if (!any_nonzero && !state->chart_had_nonzero) {
        return;
    }
    state->chart_had_nonzero = any_nonzero;

    uint8_t *qoi = malloc(20480);
    if (!qoi) {
        return;
    }
    size_t qoi_len = speed_chart_to_qoi(state->speed_history, qoi, 20480);
    if (qoi_len > 0) {
        remember_error(state, gsp_mifi_speed_chart_set_image(
                                  state->ui, qoi, qoi_len));
    }
    free(qoi);
}

static void update_hotspot(struct app_mifi *state)
{
    const uint32_t online = online_station_count(state);
    char home_count[16];
    char badge_count[8];
    if (state->english) {
        snprintf(home_count, sizeof(home_count), "%u", (unsigned)online);
    } else {
        snprintf(home_count, sizeof(home_count), "%u 台", (unsigned)online);
    }
    snprintf(badge_count, sizeof(badge_count), "%u", (unsigned)online);

    remember_error(state, gsp_mifi_hotspot_toggle_on_set_visible(
                              state->ui, state->hotspot_enabled));
    remember_error(state, gsp_mifi_hotspot_toggle_off_set_visible(
                              state->ui, !state->hotspot_enabled));
    remember_error(state, gsp_mifi_hotspot_status_set_text(
                              state->ui,
                              state->hotspot_enabled
                                  ? tr(state, "4G 热点已开启", "4G hotspot is on")
                                  : tr(state, "4G 热点已关闭", "4G hotspot is off")));
    remember_error(state, gsp_mifi_device_count_set_text(
                              state->ui, home_count));
    remember_error(state, gsp_mifi_online_count_set_text(
                              state->ui, badge_count));
    remember_error(state, gsp_mifi_online_count_en_set_text(
                              state->ui, badge_count));
    remember_error(state, gsp_mifi_online_badge_zh_set_visible(
                              state->ui, !state->english));
    remember_error(state, gsp_mifi_online_badge_en_set_visible(
                              state->ui, state->english));
    remember_error(state, gsp_mifi_share_wifi_set_text(
                              state->ui,
                              state->hotspot_enabled
                                  ? tr(state, "　分享 Wi-Fi", "　Share Wi-Fi")
                                  : tr(state, "　热点已关闭", "　Hotspot off")));
    remember_error(state, gsp_mifi_wifi_enabled_set_text(
                              state->ui,
                              state->hotspot_enabled
                                  ? tr(state, "已开启", "On")
                                  : tr(state, "已关闭", "Off")));
    remember_error(state, gsp_mifi_devices_empty_set_visible(
                              state->ui, online == 0));
}

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    size_t index = 0;
    if (!src) {
        src = "";
    }
    while (src[index] && index + 1 < dst_size) {
        dst[index] = src[index];
        ++index;
    }
    dst[index] = '\0';
}

static size_t text_length(const char *text)
{
    size_t length = 0;
    if (!text) {
        return 0;
    }
    while (text[length]) {
        ++length;
    }
    return length;
}

static bool printable_text(const char *text, size_t min_len, size_t max_len)
{
    size_t length = 0;
    if (!text) {
        return false;
    }
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor; ++cursor) {
        if (*cursor < 0x20 || *cursor > 0x7e) {
            return false;
        }
        if (++length > max_len) {
            return false;
        }
    }
    return length >= min_len;
}

static void refresh_hotspot_labels(struct app_mifi *state)
{
    char line[96];
    char mask[40];
    size_t dots = text_length(state->hotspot_password);
    if (dots > 12) {
        dots = 12;
    }
    mask[0] = '\0';
    for (size_t index = 0; index < dots; ++index) {
        strncat(mask, "•", sizeof(mask) - strlen(mask) - 1);
    }
    snprintf(line, sizeof(line), "%s  %s", tr(state, "密码", "Password"),
             state->password_visible ? state->hotspot_password : mask);
    remember_error(state, gsp_mifi_hotspot_ssid_set_text(state->ui, state->hotspot_name));
    remember_error(state, gsp_mifi_wifi_ssid_set_text(state->ui, state->hotspot_name));
    remember_error(state, gsp_mifi_qr_modal_ssid_set_text(state->ui, state->hotspot_name));
    remember_error(state, gsp_mifi_wifi_name_value_set_text(state->ui, state->hotspot_name));
    remember_error(state, gsp_mifi_wifi_password_set_text(state->ui, line));
}

static void set_setup_note(struct app_mifi *state, const char *zh, const char *en)
{
    remember_error(state, gsp_mifi_setup_note_set_text(state->ui, tr(state, zh, en)));
}

static void show_drafts(struct app_mifi *state)
{
    remember_error(state, gsp_mifi_hotspot_name_input_set_text(
                              state->ui, state->draft_name));
    remember_error(state, gsp_mifi_hotspot_pass_input_set_text(
                              state->ui, state->draft_pass));
}

static void show_keypad(struct app_mifi *state, bool upper, bool symbols)
{
    state->keys_upper = upper && !symbols;
    remember_error(state, gsp_mifi_keys_lower_set_visible(
                              state->ui, !upper && !symbols));
    remember_error(state, gsp_mifi_keys_upper_set_visible(
                              state->ui, upper && !symbols));
    remember_error(state, gsp_mifi_keys_symbol_set_visible(state->ui, symbols));
}

static void focus_hotspot_field(struct app_mifi *state, bool password)
{
    state->editing_password = password;
    set_setup_note(state, password ? "正在编辑密码" : "正在编辑名称",
                   password ? "Editing password" : "Editing name");
}

static char *active_draft(struct app_mifi *state, size_t *capacity)
{
    if (state->editing_password) {
        *capacity = sizeof(state->draft_pass);
        return state->draft_pass;
    }
    *capacity = sizeof(state->draft_name);
    return state->draft_name;
}

static void append_draft(struct app_mifi *state, char value)
{
    size_t capacity = 0;
    char *draft = active_draft(state, &capacity);
    size_t length = text_length(draft);
    size_t limit = state->editing_password ? 63 : 32;
    if (length >= limit || length + 1 >= capacity) {
        if (!state->editing_password) {
            set_setup_note(state, "名称过长", "Name is too long");
        }
        return;
    }
    draft[length] = value;
    draft[length + 1] = '\0';
    show_drafts(state);
}

static void delete_draft(struct app_mifi *state)
{
    size_t capacity = 0;
    char *draft = active_draft(state, &capacity);
    size_t length = text_length(draft);
    (void)capacity;
    if (length > 0) {
        draft[length - 1] = '\0';
        show_drafts(state);
    }
}

static void commit_hotspot_edit(struct app_mifi *state)
{
    if (!printable_text(state->draft_name, 1, 32)) {
        set_setup_note(state, state->draft_name[0] ? "名称过长" : "名称不能为空",
                       state->draft_name[0] ? "Name is too long" : "Name cannot be empty");
        state->editing_password = false;
        return;
    }
    if (!printable_text(state->draft_pass, 8, 63)) {
        set_setup_note(state, "密码至少 8 位", "Use at least 8 characters");
        state->editing_password = true;
        return;
    }
    copy_text(state->hotspot_name, sizeof(state->hotspot_name), state->draft_name);
    copy_text(state->hotspot_password, sizeof(state->hotspot_password), state->draft_pass);
    refresh_hotspot_labels(state);
    update_wifi_qr(state);
    notify_backend_settings(state);
    set_setup_note(state, "已保存", "Saved");
}

static void on_hotspot_key(struct app_mifi *state, uint32_t arg)
{
    if (arg == KEY_SHIFT) {
        show_keypad(state, !state->keys_upper, false);
    } else if (arg == KEY_SYMBOL) {
        show_keypad(state, false, true);
    } else if (arg == KEY_LETTER) {
        show_keypad(state, false, false);
    } else if (arg == KEY_DELETE) {
        delete_draft(state);
    } else if (arg == KEY_OK) {
        commit_hotspot_edit(state);
    } else if (arg >= 0x20 && arg <= 0x7e) {
        append_draft(state, (char)arg);
    }
}

static uint32_t rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return ((uint32_t)(red >> 3) << 11) |
           ((uint32_t)(green >> 2) << 5) |
           (uint32_t)(blue >> 3);
}

static gsp_err_t bind_station(esp_gsp_handle_t ui, esp_gsp_row_t row,
                              uint32_t item, void *user_ctx)
{
    struct app_mifi *state = user_ctx;
    const char *name_text = NULL;
    const char *mac_raw = NULL;
    const char *ip_raw = NULL;
    const char *usage_raw = NULL;
    const char *online_raw = NULL;
    device_kind_t kind = DEVICE_PHONE;

    if (state->has_backend && state->telemetry.valid) {
        if (item >= state->telemetry.station_count) {
            return ESP_GSP_ERR_INVALID_ARG;
        }
        const app_mifi_station_info_t *info = &state->telemetry.stations[item];
        name_text = info->name[0] ? info->name : tr(state, "接入设备", "Client");
        mac_raw = info->mac;
        ip_raw = info->ip;
        usage_raw = info->usage;
        online_raw = info->online;
        kind = (device_kind_t)(info->kind % 5);
    } else {
        int real_index = station_index_for_row(state, item);
        if (real_index < 0 || real_index >= STATION_COUNT) {
            return ESP_GSP_ERR_INVALID_ARG;
        }
        const mifi_station_t *station = &s_stations[real_index];
        name_text = tr(state, station->name_zh, station->name_en);
        mac_raw = station->mac;
        ip_raw = station->ip;
        usage_raw = station->usage;
        online_raw = station->online;
        kind = station->kind;
    }

    char mac[40];
    char address[56];
    snprintf(mac, sizeof(mac), "MAC   %s", mac_raw);
    snprintf(address, sizeof(address), "%s  ·  %s  ·  %s",
             ip_raw, usage_raw, online_raw);
    uint32_t accent = state->all_paused
                          ? rgb565(0xF1, 0xAD, 0x45)
                          : rgb565(0x3F, 0xD2, 0x74);
    const char *status_text = state->all_paused
                                  ? tr(state, "● 已暂停", "● Paused")
                                  : tr(state, "● 在线", "● Online");
    esp_gsp_err_t err = gsp_mifi_device_row_row_set_device_name_text(
        ui, row, name_text);
    if (err == ESP_GSP_OK) {
        err = gsp_mifi_device_row_row_set_device_mac_text(ui, row, mac);
    }
    if (err == ESP_GSP_OK) {
        err = gsp_mifi_device_row_row_set_device_ip_text(ui, row, address);
    }
    if (err == ESP_GSP_OK) {
        err = gsp_mifi_device_row_row_set_device_status_text(ui, row, status_text);
    }
    if (err == ESP_GSP_OK) {
        err = gsp_mifi_device_row_row_set_device_status_fg_color(ui, row, accent);
    }
    if (err == ESP_GSP_OK) {
        const uint8_t *icon = icon_phone_png;
        size_t icon_size = sizeof(icon_phone_png);
        if (kind == DEVICE_LAPTOP) {
            icon = icon_laptop_png;
            icon_size = sizeof(icon_laptop_png);
        } else if (kind == DEVICE_TABLET) {
            icon = icon_tablet_png;
            icon_size = sizeof(icon_tablet_png);
        } else if (kind == DEVICE_WATCH) {
            icon = icon_watch_png;
            icon_size = sizeof(icon_watch_png);
        } else if (kind == DEVICE_CAMERA) {
            icon = icon_camera_png;
            icon_size = sizeof(icon_camera_png);
        }
        err = gsp_mifi_device_row_row_set_device_icon_resource(
            ui, row, icon, icon_size);
    }
    if (err != ESP_GSP_OK) {
        state->last_error = err;
    }
    /* Keep text already published if a later field update fails. */
    return ESP_GSP_OK;
}

static void update_station_list(struct app_mifi *state)
{
    if (state->devices == ESP_GSP_LIST_NONE) {
        return;
    }
    const bool show_rows = (state->page == PAGE_DEVICES) &&
                           !state->device_modal_open &&
                           !state->blacklist_modal_open;
    const uint32_t total = show_rows ? online_station_count(state) : 0;
    remember_error(state, gsp_mifi_online_devices_set_total(
                              state->ui, state->devices, total));
    remember_error(state, gsp_mifi_online_devices_refresh(state->ui, state->devices));
}

static void init_station_defaults(struct app_mifi *state)
{
    state->default_blocked_active = true;
    state->all_paused = false;
    state->schedule_mode = 0;
    for (uint8_t i = 0; i < STATION_COUNT; ++i) {
        state->station_blocked[i] = false;
        state->station_quota[i] = 0;
        if (s_stations[i].status == STATION_PRIORITY) {
            state->station_rate[i] = 1;
        } else if (s_stations[i].status == STATION_LIMIT) {
            state->station_rate[i] = (i == 4) ? 3 : 2;
        } else {
            state->station_rate[i] = 0;
        }
    }
}

static void update_speed(struct app_mifi *state)
{
    char download[16];
    char upload[24];

    if (state->has_backend && state->telemetry.valid) {
        if (!state->hotspot_enabled || state->redial_ticks > 0 || state->all_paused) {
            state->download_tenths_mbps = 0;
            state->upload_tenths_mbps = 0;
        } else {
            state->download_tenths_mbps = state->telemetry.download_tenths_mbps;
            state->upload_tenths_mbps = state->telemetry.upload_tenths_mbps;
        }
    } else if (!state->has_backend && state->hotspot_enabled &&
               state->redial_ticks == 0 && !state->all_paused) {
        /* Host simulator preview before hardware backend is bound */
        state->download_tenths_mbps = download_samples[state->speed_sample];
        state->upload_tenths_mbps = upload_samples[state->speed_sample];
        state->speed_sample =
            (state->speed_sample + 1) %
            (sizeof(download_samples) / sizeof(download_samples[0]));
    } else {
        state->download_tenths_mbps = 0;
        state->upload_tenths_mbps = 0;
    }

    snprintf(download, sizeof(download), "%u.%u",
             state->download_tenths_mbps / 10,
             state->download_tenths_mbps % 10);
    snprintf(upload, sizeof(upload), "↑ %u.%u Mbps",
             state->upload_tenths_mbps / 10,
             state->upload_tenths_mbps % 10);
    remember_error(state, gsp_mifi_download_speed_set_text(state->ui, download));
    remember_error(state, gsp_mifi_upload_speed_set_text(state->ui, upload));
    update_speed_chart(state);
}

static void apply_telemetry(struct app_mifi *state)
{
    if (!state->has_backend || !state->backend.poll_telemetry) {
        return;
    }
    uint8_t prev_count = state->telemetry.station_count;
    uint8_t actual_ch = state->actual_channel;
    if (!state->backend.poll_telemetry(state->backend.ctx, &state->telemetry,
                                       &actual_ch)) {
        return;
    }
    if (actual_ch != state->actual_channel) {
        state->actual_channel = actual_ch;
        refresh_channel(state);
    }
    if (!state->telemetry.valid) {
        return;
    }

    update_network_status(state);

    /* 1. 4G USB module & Cellular status */
    if (!state->telemetry.usb_connected) {
        const char *no_mod = tr(state, "未接入 4G 子板", "No 4G module");
        remember_error(state, gsp_mifi_carrier_set_text(state->ui, no_mod));
        remember_error(state, gsp_mifi_title_wifi_carrier_set_text(state->ui, no_mod));
        remember_error(state, gsp_mifi_devices_carrier_set_text(state->ui, no_mod));
        remember_error(state, gsp_mifi_title_cellular_carrier_set_text(state->ui, no_mod));
        remember_error(state, gsp_mifi_system_carrier_set_text(state->ui, no_mod));
        remember_error(state, gsp_mifi_cell_operator_set_text(
                                  state->ui, tr(state, "未检测到 4G 模组", "No 4G module")));
        remember_error(state, gsp_mifi_cell_link_set_text(
                                  state->ui,
                                  tr(state, "请接入 USB 4G 子板", "Connect USB 4G board")));
        remember_error(state, gsp_mifi_cell_registered_set_text(
                                  state->ui, tr(state, "● 未接入", "● Unplugged")));
        remember_error(state, gsp_mifi_cell_sig_value_set_text(
                                  state->ui, tr(state, "未接入", "None")));
        remember_error(state, gsp_mifi_cell_ip_value_set_text(
                                  state->ui, tr(state, "未连接", "Disconnected")));
        remember_error(state, gsp_mifi_sim_card_value_set_text(
                                  state->ui, tr(state, "未检测", "No SIM")));
        remember_error(state, gsp_mifi_diag_row_set_text(
                                  state->ui, tr(state, "　　　 网络诊断   未接入模组",
                                                "　　　 Diagnostics   Unplugged")));
        remember_error(state, gsp_mifi_diag_ok_set_text(
                                  state->ui, tr(state, "4G 模组未接入",
                                                "4G module unplugged")));
        remember_error(state, gsp_mifi_diag_modem_detail_set_text(
                                  state->ui,
                                  tr(state, "未连接 (请接入 4G 子板)",
                                     "Unplugged")));
        remember_error(state, gsp_mifi_diag_cell_detail_set_text(
                                  state->ui, tr(state, "未就绪", "Not ready")));
        remember_error(state, gsp_mifi_diag_rndis_detail_set_text(
                                  state->ui, tr(state, "未就绪", "Not ready")));
        remember_error(state, gsp_mifi_diag_net_detail_set_text(
                                  state->ui, tr(state, "未就绪", "Not ready")));
        remember_error(state, gsp_mifi_diag_rsrp_set_text(
                                  state->ui, "RSRP  -- dBm"));
        remember_error(state, gsp_mifi_diag_sinr_set_text(
                                  state->ui, "SINR  -- dB"));
    } else if (!state->telemetry.sim_ready) {
        const char *no_sim = tr(state, "无 SIM 卡", "No SIM card");
        remember_error(state, gsp_mifi_carrier_set_text(state->ui, no_sim));
        remember_error(state, gsp_mifi_title_wifi_carrier_set_text(state->ui, no_sim));
        remember_error(state, gsp_mifi_devices_carrier_set_text(state->ui, no_sim));
        remember_error(state, gsp_mifi_title_cellular_carrier_set_text(state->ui, no_sim));
        remember_error(state, gsp_mifi_system_carrier_set_text(state->ui, no_sim));
        remember_error(state, gsp_mifi_cell_operator_set_text(
                                  state->ui, tr(state, "未检测到 SIM 卡", "No SIM card")));
        remember_error(state, gsp_mifi_cell_link_set_text(
                                  state->ui, tr(state, "请插入 4G SIM 卡", "Insert 4G SIM card")));
        remember_error(state, gsp_mifi_cell_registered_set_text(
                                  state->ui, tr(state, "● 未插卡", "● No SIM")));
        remember_error(state, gsp_mifi_cell_sig_value_set_text(
                                  state->ui, tr(state, "无信号", "No signal")));
        remember_error(state, gsp_mifi_cell_ip_value_set_text(
                                  state->ui, tr(state, "未连接", "Disconnected")));
        remember_error(state, gsp_mifi_sim_card_value_set_text(
                                  state->ui, tr(state, "未插卡", "No SIM")));
        remember_error(state, gsp_mifi_diag_row_set_text(
                                  state->ui, tr(state, "　　　 网络诊断   无 SIM 卡",
                                                "　　　 Diagnostics   No SIM")));
        remember_error(state, gsp_mifi_diag_ok_set_text(
                                  state->ui, tr(state, "请插入 SIM 卡",
                                                "Insert SIM card")));
        remember_error(state, gsp_mifi_diag_modem_detail_set_text(
                                  state->ui, tr(state, "已连接 (ML307C)",
                                                "Connected (ML307C)")));
        remember_error(state, gsp_mifi_diag_cell_detail_set_text(
                                  state->ui, tr(state, "未插 SIM 卡",
                                                "SIM not inserted")));
        remember_error(state, gsp_mifi_diag_rndis_detail_set_text(
                                  state->ui, tr(state, "等待拨号", "Waiting")));
        remember_error(state, gsp_mifi_diag_net_detail_set_text(
                                  state->ui, tr(state, "未就绪", "Not ready")));
        remember_error(state, gsp_mifi_diag_rsrp_set_text(
                                  state->ui, "RSRP  -- dBm"));
        remember_error(state, gsp_mifi_diag_sinr_set_text(
                                  state->ui, "SINR  -- dB"));
    } else {
        const char *op = state->english ? state->telemetry.operator_en
                                        : state->telemetry.operator_zh;
        if (!op[0]) {
            op = tr(state, "正在识别 4G...", "Init 4G...");
        }
        remember_error(state, gsp_mifi_carrier_set_text(state->ui, op));
        remember_error(state, gsp_mifi_title_wifi_carrier_set_text(state->ui, op));
        remember_error(state, gsp_mifi_devices_carrier_set_text(state->ui, op));
        remember_error(state, gsp_mifi_title_cellular_carrier_set_text(state->ui, op));
        remember_error(state, gsp_mifi_system_carrier_set_text(state->ui, op));
        remember_error(state, gsp_mifi_cell_operator_set_text(state->ui, op));
        remember_error(state, gsp_mifi_diag_modem_detail_set_text(
                                  state->ui, tr(state, "已连接 (ML307C)",
                                                "Connected (ML307C)")));
        remember_error(state, gsp_mifi_diag_cell_detail_set_text(state->ui, op));

        if (state->telemetry.registered && state->telemetry.link_up &&
            state->telemetry.wan_ip[0] &&
            strcmp(state->telemetry.wan_ip, "0.0.0.0") != 0) {
            remember_error(state, gsp_mifi_diag_row_set_text(
                                      state->ui, tr(state, "　　　 网络诊断   全部正常",
                                                    "　　　 Diagnostics   All healthy")));
            remember_error(state, gsp_mifi_diag_ok_set_text(
                                      state->ui, tr(state, "网络连接正常",
                                                    "Connection is healthy")));
            remember_error(state, gsp_mifi_cell_ip_value_set_text(
                                      state->ui, state->telemetry.wan_ip));
            remember_error(state, gsp_mifi_diag_rndis_detail_set_text(
                                      state->ui, state->telemetry.wan_ip));
            remember_error(state, gsp_mifi_diag_net_detail_set_text(
                                      state->ui, tr(state, "DNS 与公网正常",
                                                    "DNS and WAN OK")));
            remember_error(state, gsp_mifi_cell_link_set_text(
                                      state->ui,
                                      state->telemetry.band_info[0]
                                          ? state->telemetry.band_info
                                          : tr(state, "LTE Band 3 · 已联网",
                                               "LTE Band 3 · Online")));
        } else {
            remember_error(state, gsp_mifi_diag_row_set_text(
                                      state->ui, tr(state, "　　　 网络诊断   正在拨号",
                                                    "　　　 Diagnostics   Dialing")));
            remember_error(state, gsp_mifi_diag_ok_set_text(
                                      state->ui, tr(state, "正在拨号连接...",
                                                    "Dialing...")));
            remember_error(state, gsp_mifi_cell_ip_value_set_text(
                                      state->ui, tr(state, "未连接", "Disconnected")));
            remember_error(state, gsp_mifi_diag_rndis_detail_set_text(
                                      state->ui, tr(state, "等待拨号", "Waiting")));
            remember_error(state, gsp_mifi_diag_net_detail_set_text(
                                      state->ui, tr(state, "等待 WAN 地址",
                                                    "Waiting for WAN")));
            remember_error(state, gsp_mifi_cell_link_set_text(
                                      state->ui,
                                      tr(state, "RNDIS 正在拨号...", "RNDIS dialing...")));
        }

        remember_error(state, gsp_mifi_sim_card_value_set_text(
                                  state->ui, tr(state, "已就绪", "Ready")));
        remember_error(state, gsp_mifi_cell_registered_set_text(
                                  state->ui,
                                  state->telemetry.registered
                                      ? tr(state, "● 已注册", "● Registered")
                                      : tr(state, "● 搜索中", "● Searching")));
        if (state->telemetry.rsrp_dbm != 0) {
            char sig_buf[32];
            char rsrp_buf[24];
            char sinr_buf[24];
            const bool good = state->telemetry.rsrp_dbm >= -100;
            snprintf(sig_buf, sizeof(sig_buf), "%s (%ddBm)",
                     good ? tr(state, "良好", "Good") : tr(state, "一般", "Fair"),
                     (int)state->telemetry.rsrp_dbm);
            snprintf(rsrp_buf, sizeof(rsrp_buf), "RSRP  %d dBm",
                     (int)state->telemetry.rsrp_dbm);
            int sinr = state->telemetry.sinr_db != 0
                           ? (int)state->telemetry.sinr_db
                           : (good ? 15 : 8);
            snprintf(sinr_buf, sizeof(sinr_buf), "SINR  %d dB", sinr);
            remember_error(state, gsp_mifi_cell_sig_value_set_text(state->ui, sig_buf));
            remember_error(state, gsp_mifi_diag_rsrp_set_text(state->ui, rsrp_buf));
            remember_error(state, gsp_mifi_diag_sinr_set_text(state->ui, sinr_buf));
        } else {
            remember_error(state, gsp_mifi_cell_sig_value_set_text(
                                      state->ui, tr(state, "无信号", "No signal")));
            remember_error(state, gsp_mifi_diag_rsrp_set_text(
                                      state->ui, "RSRP  -- dBm"));
            remember_error(state, gsp_mifi_diag_sinr_set_text(
                                      state->ui, "SINR  -- dB"));
        }
    }

    char drop_buf[28];
    if (state->telemetry.drop_count == 0) {
        snprintf(drop_buf, sizeof(drop_buf), "%s",
                 tr(state, "最近掉线  无", "Last drop  none"));
    } else {
        snprintf(drop_buf, sizeof(drop_buf),
                 state->english ? "Drops  %u" : "最近掉线  %u 次",
                 (unsigned)state->telemetry.drop_count);
    }
    remember_error(state, gsp_mifi_diag_drop_set_text(state->ui, drop_buf));

    /* Live runtime logs & About device fields */
    static esp_gsp_err_t (*const log_setters[4])(esp_gsp_handle_t, const char *) = {
        gsp_mifi_log_0_set_text,
        gsp_mifi_log_1_set_text,
        gsp_mifi_log_2_set_text,
        gsp_mifi_log_3_set_text,
    };
    for (int i = 0; i < 4; ++i) {
        const char *msg = state->english ? state->telemetry.logs_en[i]
                                         : state->telemetry.logs_zh[i];
        if (msg[0]) {
            remember_error(state, log_setters[i](state->ui, msg));
        }
    }
    if (state->telemetry.modem_fw[0]) {
        remember_error(state, gsp_mifi_about_fw_value_set_text(
                                  state->ui, state->telemetry.modem_fw));
    }
    {
        char up_buf[36];
        uint32_t s = state->telemetry.uptime_sec;
        uint32_t d = s / 86400u;
        uint32_t h = (s % 86400u) / 3600u;
        uint32_t m = (s % 3600u) / 60u;
        if (d > 0) {
            snprintf(up_buf, sizeof(up_buf),
                     state->english ? "%u d %02u h" : "%u 天 %02u 小时",
                     (unsigned)d, (unsigned)h);
        } else if (h > 0) {
            snprintf(up_buf, sizeof(up_buf),
                     state->english ? "%u h %02u min" : "%u 小时 %02u 分",
                     (unsigned)h, (unsigned)m);
        } else {
            snprintf(up_buf, sizeof(up_buf),
                     state->english ? "%u min" : "%u 分钟",
                     (unsigned)(m > 0 ? m : 1));
        }
        remember_error(state, gsp_mifi_about_uptime_value_set_text(state->ui, up_buf));
    }

    if (state->telemetry.monthly_usage[0]) {
        remember_error(state, gsp_mifi_home_month_value_set_text(
                                  state->ui, state->telemetry.monthly_usage));
        remember_error(state, gsp_mifi_cell_data_value_set_text(
                                  state->ui, state->telemetry.monthly_usage));
    }

    /* 2. Real-time Battery percentage, Charging Bolt & Runtime estimation */
    char pct_buf[12];
    uint8_t pct = state->telemetry.battery_percent;
    if (pct > 100) {
        pct = 100;
    }
    snprintf(pct_buf, sizeof(pct_buf), "%u%%", (unsigned)pct);
    remember_error(state, gsp_mifi_bat_home_set_text(state->ui, pct_buf));
    remember_error(state, gsp_mifi_bat_title_wifi_set_text(state->ui, pct_buf));
    remember_error(state, gsp_mifi_bat_devices_set_text(state->ui, pct_buf));
    remember_error(state, gsp_mifi_bat_title_cellular_set_text(state->ui, pct_buf));
    remember_error(state, gsp_mifi_bat_system_set_text(state->ui, pct_buf));

    const bool chg = state->telemetry.charging;
    const int16_t cur_ma = state->telemetry.battery_current_ma;
    const bool ext_pwr = chg || (state->telemetry.battery_mv > 0 && cur_ma >= -20);
    remember_error(state, gsp_mifi_bolt_home_set_visible(state->ui, ext_pwr));
    remember_error(state, gsp_mifi_bolt_title_wifi_set_visible(state->ui, ext_pwr));
    remember_error(state, gsp_mifi_bolt_devices_set_visible(state->ui, ext_pwr));
    remember_error(state, gsp_mifi_bolt_title_cellular_set_visible(state->ui, ext_pwr));
    remember_error(state, gsp_mifi_bolt_system_set_visible(state->ui, ext_pwr));
    remember_error(state, gsp_mifi_bolt_sys_card_set_visible(state->ui, ext_pwr));

    char summary_buf[48];
    char home_rt_buf[20];
    if (chg && cur_ma > 5) {
        if (pct >= 99) {
            snprintf(summary_buf, sizeof(summary_buf), "%u%% · %s",
                     (unsigned)pct, tr(state, "已充满", "Full"));
        } else if (state->telemetry.time_to_full_min > 0 &&
                   state->telemetry.time_to_full_min < 600) {
            snprintf(summary_buf, sizeof(summary_buf),
                     state->english ? "%u%% · Charging (full in %u min)"
                                    : "%u%% · 充电中 (预计 %u 分钟充满)",
                     (unsigned)pct, (unsigned)state->telemetry.time_to_full_min);
        } else {
            snprintf(summary_buf, sizeof(summary_buf), "%u%% · %s",
                     (unsigned)pct, tr(state, "充电中", "Charging"));
        }
        snprintf(home_rt_buf, sizeof(home_rt_buf), "%s",
                 tr(state, "充电中", "Chg"));
    } else if (ext_pwr) {
        snprintf(summary_buf, sizeof(summary_buf), "%u%% · %s",
                 (unsigned)pct, tr(state, "外部供电中", "Ext Power"));
        snprintf(home_rt_buf, sizeof(home_rt_buf), "%s",
                 tr(state, "已接电", "PWR"));
    } else {
        uint16_t mins = state->telemetry.time_to_empty_min;
        if (mins == 0 || mins > 2400) {
            /* Fallback estimate from SOC% */
            mins = (uint16_t)((pct * 360u) / 100u);
        }
        uint16_t hrs = mins / 60;
        uint16_t rem_m = mins % 60;
        if (hrs > 0) {
            snprintf(summary_buf, sizeof(summary_buf),
                     state->english ? "%u%% · about %u h %u min"
                                    : "%u%% · 约 %u 小时 %u 分",
                     (unsigned)pct, (unsigned)hrs, (unsigned)rem_m);
            snprintf(home_rt_buf, sizeof(home_rt_buf),
                     state->english ? "~%uh" : "约 %uh", (unsigned)hrs);
        } else {
            snprintf(summary_buf, sizeof(summary_buf),
                     state->english ? "%u%% · about %u min"
                                    : "%u%% · 约 %u 分钟",
                     (unsigned)pct, (unsigned)rem_m);
            snprintf(home_rt_buf, sizeof(home_rt_buf), "%um", (unsigned)rem_m);
        }
    }
    remember_error(state, gsp_mifi_battery_summary_set_text(state->ui, summary_buf));
    remember_error(state, gsp_mifi_runtime_value_set_text(state->ui, home_rt_buf));

    /* 3. Real-time Internal Board Temperature (fused ESP32-S31 + BMI270 + BQ27220) */
    char temp_buf[40];
    const bool warm = state->telemetry.temperature_c >= 48;
    snprintf(temp_buf, sizeof(temp_buf), "%s %d°C  %s",
             tr(state, "内部温度", "Board"),
             (int)state->telemetry.temperature_c,
             warm ? tr(state, "偏高", "Warm") : tr(state, "正常", "Normal"));
    remember_error(state, gsp_mifi_battery_detail_set_text(state->ui, temp_buf));

    if (state->telemetry.battery_mv == 0) {
        remember_error(state, gsp_mifi_battery_hw_set_text(
                                  state->ui, tr(state, "USB 外部供电", "USB Powered")));
    } else {
        char hw_buf[44];
        const char *mode_str = NULL;
        if (chg && cur_ma > 5) {
            mode_str = tr(state, "充电中", "Charging");
        } else if (ext_pwr) {
            mode_str = tr(state, "外部供电", "Ext Power");
        } else {
            mode_str = tr(state, "电池供电", "Battery");
        }
        snprintf(hw_buf, sizeof(hw_buf), "%s · %d.%02d V (%+dmA)",
                 mode_str,
                 state->telemetry.battery_mv / 1000,
                 (state->telemetry.battery_mv % 1000) / 10,
                 (int)cur_ma);
        remember_error(state, gsp_mifi_battery_hw_set_text(state->ui, hw_buf));
    }

    if (state->telemetry.station_count != prev_count ||
        state->page == PAGE_DEVICES) {
        update_hotspot(state);
        update_station_list(state);
    }
}

static void animation_tick(esp_gsp_handle_t ui, void *ctx)
{
    (void)ui;
    struct app_mifi *state = ctx;
    const bool dialing =
        state->redial_ticks > 0 ||
        (state->has_backend && state->telemetry.valid &&
         state->telemetry.usb_connected && !state->telemetry.link_up);

    if (state->devices_dirty && state->page == PAGE_DEVICES) {
        update_station_list(state);
        state->devices_dirty = false;
    }

    const uint8_t tick_div =
        dialing ? SPEED_TICK_DIVISOR_DIALING : SPEED_TICK_DIVISOR;
    if (++state->timer_ticks >= tick_div) {
        state->timer_ticks = 0;
        apply_telemetry(state);
        update_speed(state);
    }

    if (state->redial_ticks > 0 && --state->redial_ticks == 0) {
        update_network_status(state);
        update_speed(state);
    }
    if (state->diag_ticks > 0 && --state->diag_ticks == 0) {
        remember_error(state, gsp_mifi_diag_when_set_text(
                                  state->ui,
                                  tr(state, "检测完成 · 刚刚", "Checked · just now")));
    }
    if (state->update_ticks > 0 && --state->update_ticks == 0) {
        remember_error(state, gsp_mifi_update_status_set_text(
                                  state->ui,
                                  tr(state, "已是最新版本", "Already up to date")));
    }
}

static void on_pointer(esp_gsp_handle_t ui, int32_t x, int32_t y,
                       bool pressed, void *ctx)
{
    (void)ui;
    (void)x;
    (void)y;
    struct app_mifi *state = ctx;
    if (pressed && state && state->has_backend && state->backend.user_activity) {
        state->backend.user_activity(state->backend.ctx);
    }
}

static void on_event(esp_gsp_handle_t ui, const esp_gsp_event_t *event, void *ctx)
{
    (void)ui;
    struct app_mifi *state = ctx;
    if (state->has_backend && state->backend.user_activity) {
        state->backend.user_activity(state->backend.ctx);
    }

    if (gsp_mifi_event_is_nav_home(event)) {
        set_page(state, PAGE_HOME);
    } else if (gsp_mifi_event_is_nav_wifi(event) ||
               gsp_mifi_event_is_share_wifi(event)) {
        set_page(state, PAGE_WIFI);
    } else if (gsp_mifi_event_is_nav_devices(event)) {
        set_page(state, PAGE_DEVICES);
        state->devices_dirty = true;
    } else if (gsp_mifi_event_is_nav_cellular(event)) {
        set_page(state, PAGE_CELLULAR);
    } else if (gsp_mifi_event_is_nav_system(event)) {
        set_page(state, PAGE_SYSTEM);
    } else if (gsp_mifi_event_is_open_diagnostics(event)) {
        set_page(state, PAGE_DIAGNOSTICS);
    } else if (gsp_mifi_event_is_open_update(event)) {
        set_page(state, PAGE_UPDATE);
    } else if (gsp_mifi_event_is_open_logs(event)) {
        set_page(state, PAGE_LOGS);
    } else if (gsp_mifi_event_is_open_about(event)) {
        set_page(state, PAGE_ABOUT);
    } else if (gsp_mifi_event_is_open_licenses(event)) {
        set_page(state, PAGE_LICENSES);
    } else if (gsp_mifi_event_is_open_privacy(event)) {
        set_page(state, PAGE_PRIVACY);
    } else if (gsp_mifi_event_is_open_security(event)) {
        set_channel_menu(state, false);
        set_qr_modal(state, false);
        set_security_menu(state, !state->security_open);
    } else if (gsp_mifi_event_is_set_security(event) &&
               event->arg < AUTH_MODE_COUNT) {
        state->auth_mode = (uint8_t)event->arg;
        refresh_security(state);
        update_wifi_qr(state);
        notify_backend_settings(state);
        set_security_menu(state, false);
    } else if (gsp_mifi_event_is_open_channel(event)) {
        set_security_menu(state, false);
        set_qr_modal(state, false);
        set_channel_menu(state, !state->channel_open);
    } else if (gsp_mifi_event_is_open_qr_modal(event)) {
        set_security_menu(state, false);
        set_channel_menu(state, false);
        set_qr_modal(state, true);
    } else if (gsp_mifi_event_is_close_qr_modal(event)) {
        set_qr_modal(state, false);
    } else if (gsp_mifi_event_is_set_channel(event) &&
               event->arg < CHANNEL_MODE_COUNT) {
        state->channel_mode = (uint8_t)event->arg;
        refresh_channel(state);
        notify_backend_settings(state);
        set_channel_menu(state, false);
    } else if (gsp_mifi_event_is_cycle_max_conn(event)) {
        state->max_conn_mode =
            (uint8_t)((state->max_conn_mode + 1) % MAX_CONN_MODE_COUNT);
        refresh_max_conn(state);
        notify_backend_settings(state);
    } else if (gsp_mifi_event_is_set_brightness(event) &&
               event->arg >= BRIGHTNESS_MIN && event->arg <= BRIGHTNESS_MAX) {
        state->brightness = (uint8_t)event->arg;
        if (!state->power_save) {
            state->normal_brightness = state->brightness;
        }
        show_brightness(state);
        notify_backend_settings(state);
    } else if (gsp_mifi_event_is_toggle_power_save(event)) {
        state->power_save = !state->power_save;
        if (state->power_save) {
            state->normal_brightness = state->brightness;
            state->normal_sleep_mode = state->sleep_mode;
            if (state->brightness > BRIGHTNESS_POWER_SAVE) {
                state->brightness = BRIGHTNESS_POWER_SAVE;
            }
            state->sleep_mode = 0;
        } else {
            state->brightness = state->normal_brightness;
            state->sleep_mode = state->normal_sleep_mode;
        }
        remember_error(state, gsp_mifi_brightness_slider_set_value(
                                  state->ui, state->brightness));
        show_brightness(state);
        refresh_power_controls(state);
        notify_backend_settings(state);
    } else if (gsp_mifi_event_is_cycle_sleep(event)) {
        state->sleep_mode = (uint8_t)((state->sleep_mode + 1) % SLEEP_MODE_COUNT);
        if (!state->power_save) {
            state->normal_sleep_mode = state->sleep_mode;
        }
        refresh_power_controls(state);
        notify_backend_settings(state);
    } else if (gsp_mifi_event_is_toggle_roaming(event)) {
        state->roaming = !state->roaming;
        refresh_power_controls(state);
        notify_backend_settings(state);
    } else if (gsp_mifi_event_is_open_device_detail(event)) {
        int real_index = station_index_for_row(state, event->item);
        if (real_index < 0 && event->arg < APP_MIFI_MAX_STATIONS) {
            real_index = station_index_for_row(state, event->arg);
        }
        if (real_index >= 0) {
            state->selected_station = (uint8_t)real_index;
            set_blacklist_modal(state, false);
            refresh_device_detail(state);
            set_device_modal(state, true);
        }
    } else if (gsp_mifi_event_is_close_device_modal(event)) {
        set_device_modal(state, false);
    } else if (gsp_mifi_event_is_cycle_device_quota(event)) {
        uint8_t idx = state->selected_station % STATION_COUNT;
        state->station_quota[idx] =
            (uint8_t)((state->station_quota[idx] + 1) % QUOTA_MODE_COUNT);
        refresh_device_detail(state);
    } else if (gsp_mifi_event_is_block_selected_device(event)) {
        if (state->has_backend && state->telemetry.valid) {
            if (state->selected_station < state->telemetry.station_count) {
                const app_mifi_station_info_t *info =
                    &state->telemetry.stations[state->selected_station];
                if (state->blocked_hw_count < 3) {
                    uint8_t slot = state->blocked_hw_count++;
                    copy_text(state->blocked_hw_names[slot],
                              sizeof(state->blocked_hw_names[slot]),
                              info->name[0] ? info->name : "Client");
                    copy_text(state->blocked_hw_macs[slot],
                              sizeof(state->blocked_hw_macs[slot]), info->mac);
                }
                if (state->backend.block_mac) {
                    state->backend.block_mac(state->backend.ctx, info->mac, true);
                }
            }
            set_device_modal(state, false);
            apply_telemetry(state);
            update_hotspot(state);
            refresh_device_footer(state);
            refresh_blacklist_modal(state);
            update_station_list(state);
        } else if (state->selected_station < STATION_COUNT) {
            state->station_blocked[state->selected_station] = true;
            set_device_modal(state, false);
            update_hotspot(state);
            refresh_device_footer(state);
            refresh_blacklist_modal(state);
            update_station_list(state);
        }
    } else if (gsp_mifi_event_is_open_blacklist(event)) {
        set_device_modal(state, false);
        refresh_blacklist_modal(state);
        set_blacklist_modal(state, !state->blacklist_modal_open);
    } else if (gsp_mifi_event_is_close_blacklist_modal(event)) {
        set_blacklist_modal(state, false);
    } else if (gsp_mifi_event_is_unblock_slot(event) && event->arg < 3) {
        if (state->has_backend) {
            uint8_t slot = (uint8_t)event->arg;
            if (slot < state->blocked_hw_count) {
                if (state->backend.block_mac) {
                    state->backend.block_mac(
                        state->backend.ctx, state->blocked_hw_macs[slot], false);
                }
                for (uint8_t i = slot; i + 1 < state->blocked_hw_count; ++i) {
                    memcpy(state->blocked_hw_names[i],
                           state->blocked_hw_names[i + 1],
                           sizeof(state->blocked_hw_names[i]));
                    memcpy(state->blocked_hw_macs[i],
                           state->blocked_hw_macs[i + 1],
                           sizeof(state->blocked_hw_macs[i]));
                }
                --state->blocked_hw_count;
            }
        } else {
            uint8_t code = 0xFFu;
            blocked_slot_entry(state, event->arg, &code);
            if (code == EXTRA_BLOCKED_INDEX) {
                state->default_blocked_active = false;
            } else if (code < STATION_COUNT) {
                state->station_blocked[code] = false;
            }
        }
        update_hotspot(state);
        refresh_device_footer(state);
        refresh_blacklist_modal(state);
        update_station_list(state);
    } else if (gsp_mifi_event_is_cycle_schedule(event)) {
        state->schedule_mode =
            (uint8_t)((state->schedule_mode + 1) % SCHEDULE_MODE_COUNT);
        refresh_device_footer(state);
        notify_backend_settings(state);
    } else if (gsp_mifi_event_is_toggle_pause_all(event)) {
        state->all_paused = !state->all_paused;
        refresh_device_footer(state);
        update_station_list(state);
        update_speed(state);
        notify_backend_settings(state);
    } else if (gsp_mifi_event_is_open_reset_confirm(event)) {
        set_reset_modal(state, true);
    } else if (gsp_mifi_event_is_cancel_factory_reset(event)) {
        set_reset_modal(state, false);
    } else if (gsp_mifi_event_is_confirm_factory_reset(event)) {
        copy_text(state->hotspot_name, sizeof(state->hotspot_name), "Mosaico_4G");
        copy_text(state->hotspot_password, sizeof(state->hotspot_password), "mifi2026");
        state->hotspot_enabled = true;
        state->password_visible = false;
        state->power_save = false;
        state->roaming = false;
        state->auth_mode = AUTH_MODE_DEFAULT;
        state->brightness = BRIGHTNESS_DEFAULT;
        state->normal_brightness = BRIGHTNESS_DEFAULT;
        state->sleep_mode = SLEEP_MODE_DEFAULT;
        state->normal_sleep_mode = SLEEP_MODE_DEFAULT;
        state->max_conn_mode = 1;
        state->channel_mode = CHANNEL_MODE_DEFAULT;
        init_station_defaults(state);
        if (state->has_backend) {
            state->default_blocked_active = false;
            state->blocked_hw_count = 0;
        }
        if (state->has_backend && state->backend.factory_reset) {
            app_mifi_settings_t defaults = {0};
            state->backend.factory_reset(state->backend.ctx, &defaults);
        } else {
            notify_backend_settings(state);
        }
        update_hotspot(state);
        refresh_hotspot_labels(state);
        refresh_security(state);
        refresh_channel(state);
        refresh_max_conn(state);
        update_wifi_qr(state);
        remember_error(state, gsp_mifi_brightness_slider_set_value(
                                  state->ui, state->brightness));
        show_brightness(state);
        refresh_power_controls(state);
        refresh_device_footer(state);
        refresh_blacklist_modal(state);
        state->devices_dirty = true;
        update_station_list(state);
        update_speed(state);
        set_reset_modal(state, false);
    } else if (gsp_mifi_event_is_open_hotspot(event)) {
        copy_text(state->draft_name, sizeof(state->draft_name), state->hotspot_name);
        copy_text(state->draft_pass, sizeof(state->draft_pass), state->hotspot_password);
        state->editing_password = false;
        set_page(state, PAGE_HOTSPOT);
        show_keypad(state, false, false);
        show_drafts(state);
        focus_hotspot_field(state, false);
    } else if (gsp_mifi_event_is_edit_hotspot_name(event)) {
        focus_hotspot_field(state, false);
    } else if (gsp_mifi_event_is_edit_hotspot_pass(event)) {
        focus_hotspot_field(state, true);
    } else if (gsp_mifi_event_is_hotspot_key(event)) {
        on_hotspot_key(state, event->arg);
    } else if (gsp_mifi_event_is_toggle_password(event)) {
        state->password_visible = !state->password_visible;
        refresh_hotspot_labels(state);
    } else if (gsp_mifi_event_is_back(event)) {
        if (state->page == PAGE_HOTSPOT) {
            set_page(state, PAGE_WIFI);
        } else {
            set_page(state,
                     state->page == PAGE_LICENSES || state->page == PAGE_PRIVACY
                         ? PAGE_ABOUT
                         : PAGE_SYSTEM);
        }
    } else if (gsp_mifi_event_is_toggle_hotspot(event)) {
        state->hotspot_enabled = !state->hotspot_enabled;
        update_hotspot(state);
        state->devices_dirty = true;
        update_station_list(state);
        update_speed(state);
        notify_backend_settings(state);
    } else if (gsp_mifi_event_is_redial(event)) {
        state->redial_ticks = REDIAL_TICKS;
        update_network_status(state);
        update_speed(state);
        if (state->has_backend && state->backend.redial) {
            state->backend.redial(state->backend.ctx);
        }
    } else if (gsp_mifi_event_is_recheck_diagnostics(event)) {
        state->diag_ticks = 6;
        remember_error(state, gsp_mifi_diag_when_set_text(
                                  state->ui,
                                  tr(state, "正在检测...", "Checking...")));
        apply_telemetry(state);
    } else if (gsp_mifi_event_is_check_update(event)) {
        state->update_ticks = 6;
        remember_error(state, gsp_mifi_update_status_set_text(
                                  state->ui,
                                  tr(state, "正在检查更新...", "Checking...")));
    } else if (gsp_mifi_event_is_toggle_language(event)) {
        state->english = !state->english;
        apply_scene_strings(state);
        update_hotspot(state);
        refresh_hotspot_labels(state);
        refresh_security(state);
        refresh_channel(state);
        refresh_max_conn(state);
        show_brightness(state);
        refresh_power_controls(state);
        refresh_device_footer(state);
        refresh_device_detail(state);
        refresh_blacklist_modal(state);
        apply_telemetry(state);
        update_network_status(state);
        state->devices_dirty = true;
        update_station_list(state);
    }
}

esp_gsp_err_t app_mifi_create(esp_gsp_handle_t ui,
                              app_mifi_handle_t *out_handle)
{
    if (!ui || !out_handle) {
        return ESP_GSP_ERR_INVALID_ARG;
    }
    *out_handle = NULL;

    struct app_mifi *state = calloc(1, sizeof(*state));
    if (!state) {
        return ESP_GSP_ERR_NO_MEM;
    }
    state->ui = ui;
    state->hotspot_enabled = true;
    state->devices = ESP_GSP_LIST_NONE;
    copy_text(state->hotspot_name, sizeof(state->hotspot_name), "Mosaico_4G");
    copy_text(state->hotspot_password, sizeof(state->hotspot_password), "mifi2026");
    state->auth_mode = AUTH_MODE_DEFAULT;
    state->brightness = BRIGHTNESS_DEFAULT;
    state->normal_brightness = BRIGHTNESS_DEFAULT;
    state->sleep_mode = SLEEP_MODE_DEFAULT;
    state->normal_sleep_mode = SLEEP_MODE_DEFAULT;
    state->max_conn_mode = 1;
    state->channel_mode = CHANNEL_MODE_DEFAULT;
    state->actual_channel = 6;
    init_station_defaults(state);

    set_page(state, PAGE_HOME);
    MIFI_YIELD_QUEUE();
    apply_scene_strings(state);
    MIFI_YIELD_QUEUE();
    update_hotspot(state);
    refresh_hotspot_labels(state);
    refresh_security(state);
    refresh_channel(state);
    refresh_max_conn(state);
    MIFI_YIELD_QUEUE();
    update_wifi_qr(state);
    remember_error(state, gsp_mifi_brightness_slider_set_value(
                              state->ui, state->brightness));
    show_brightness(state);
    refresh_power_controls(state);
    refresh_device_footer(state);
    refresh_blacklist_modal(state);
    update_network_status(state);
    remember_error(state, gsp_mifi_download_speed_set_text(state->ui, "0.0"));
    remember_error(state, gsp_mifi_upload_speed_set_text(state->ui, "↑ 0.0 Mbps"));
    state->devices_dirty = true;
    state->devices = gsp_mifi_online_devices_bind(ui, bind_station, state);
    if (state->devices == ESP_GSP_LIST_NONE) {
        state->last_error = ESP_GSP_ERR_NO_MEM;
    } else {
        update_station_list(state);
    }
    esp_gsp_err_t err = state->last_error;
    if (err == ESP_GSP_OK) {
        err = esp_gsp_on_event(ui, on_event, state);
    }
    if (err == ESP_GSP_OK) {
        esp_gsp_set_pointer_observer(ui, on_pointer, state);
    }
    if (err == ESP_GSP_OK) {
        state->timer =
            esp_gsp_timer_create(ui, ANIMATION_TICK_MS, animation_tick, state);
        if (!state->timer) {
            err = ESP_GSP_ERR_NO_MEM;
        }
    }
    if (err != ESP_GSP_OK) {
        esp_gsp_on_event(ui, NULL, NULL);
        free(state);
        return err;
    }

    *out_handle = state;
    return ESP_GSP_OK;
}

void app_mifi_set_backend(app_mifi_handle_t handle,
                          const app_mifi_backend_t *backend,
                          const app_mifi_settings_t *initial_settings)
{
    if (!handle) {
        return;
    }
    if (!backend) {
        handle->has_backend = false;
        memset(&handle->backend, 0, sizeof(handle->backend));
        return;
    }
    handle->backend = *backend;
    handle->has_backend = true;
    handle->default_blocked_active = false;
    handle->blocked_hw_count = 0;
    if (initial_settings) {
        if (initial_settings->ssid[0]) {
            copy_text(handle->hotspot_name, sizeof(handle->hotspot_name),
                      initial_settings->ssid);
        }
        if (initial_settings->password[0]) {
            copy_text(handle->hotspot_password, sizeof(handle->hotspot_password),
                      initial_settings->password);
        }
        if (initial_settings->auth_mode < AUTH_MODE_COUNT) {
            handle->auth_mode = initial_settings->auth_mode;
        }
        if (initial_settings->channel_mode < CHANNEL_MODE_COUNT) {
            handle->channel_mode = initial_settings->channel_mode;
        }
        if (initial_settings->actual_channel >= 1 &&
            initial_settings->actual_channel <= 13) {
            handle->actual_channel = initial_settings->actual_channel;
        }
        if (initial_settings->max_conn_mode < MAX_CONN_MODE_COUNT) {
            handle->max_conn_mode = initial_settings->max_conn_mode;
        }
        handle->hotspot_enabled = initial_settings->hotspot_enabled;
        handle->all_paused = initial_settings->all_paused;
        handle->power_save = initial_settings->power_save;
        handle->roaming = initial_settings->roaming;
        if (initial_settings->brightness >= BRIGHTNESS_MIN &&
            initial_settings->brightness <= BRIGHTNESS_MAX) {
            handle->brightness = initial_settings->brightness;
            handle->normal_brightness = initial_settings->brightness;
        }
        if (initial_settings->sleep_mode < SLEEP_MODE_COUNT) {
            handle->sleep_mode = initial_settings->sleep_mode;
            handle->normal_sleep_mode = initial_settings->sleep_mode;
        }
        if (initial_settings->schedule_mode < SCHEDULE_MODE_COUNT) {
            handle->schedule_mode = initial_settings->schedule_mode;
        }
        update_hotspot(handle);
        refresh_hotspot_labels(handle);
        refresh_security(handle);
        refresh_channel(handle);
        refresh_max_conn(handle);
        update_wifi_qr(handle);
        remember_error(handle, gsp_mifi_brightness_slider_set_value(
                                   handle->ui, handle->brightness));
        show_brightness(handle);
        refresh_power_controls(handle);
        refresh_device_footer(handle);
        refresh_blacklist_modal(handle);
    }
    apply_telemetry(handle);
    update_hotspot(handle);
    update_station_list(handle);
    update_speed(handle);
}

esp_gsp_err_t app_mifi_read(app_mifi_handle_t handle,
                            app_mifi_snapshot_t *out_snapshot)
{
    if (!handle || !out_snapshot) {
        return ESP_GSP_ERR_INVALID_ARG;
    }
    *out_snapshot = (app_mifi_snapshot_t) {
        .download_tenths_mbps = handle->download_tenths_mbps,
        .upload_tenths_mbps = handle->upload_tenths_mbps,
        .hotspot_enabled = handle->hotspot_enabled,
        .redialing = handle->redial_ticks > 0,
        .last_error = handle->last_error,
    };
    return ESP_GSP_OK;
}

esp_gsp_err_t app_mifi_destroy(app_mifi_handle_t *handle)
{
    if (!handle) {
        return ESP_GSP_ERR_INVALID_ARG;
    }
    struct app_mifi *state = *handle;
    if (!state) {
        return ESP_GSP_OK;
    }

    esp_gsp_set_pointer_observer(state->ui, NULL, NULL);
    esp_gsp_err_t err = esp_gsp_on_event(state->ui, NULL, NULL);
    if (err != ESP_GSP_OK) {
        return err;
    }
    if (state->timer) {
        err = esp_gsp_timer_delete(state->ui, state->timer);
        if (err != ESP_GSP_OK) {
            return err;
        }
    }
    free(state);
    *handle = NULL;
    return ESP_GSP_OK;
}
