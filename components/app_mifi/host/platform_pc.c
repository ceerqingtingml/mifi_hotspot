// SPDX-License-Identifier: Apache-2.0

#include <stdio.h>

#include "app_mifi.h"
#include "gsp_sim_bridge.h"

static app_mifi_handle_t s_controller;

esp_gsp_err_t gsp_bridge_app_init(esp_gsp_handle_t ui)
{
    return app_mifi_create(ui, &s_controller);
}

void gsp_bridge_app_deinit(esp_gsp_handle_t ui)
{
    (void)ui;
    app_mifi_snapshot_t snapshot;
    if (app_mifi_read(s_controller, &snapshot) == ESP_GSP_OK) {
        fprintf(stderr,
                "mifi_backend: down=%u.%u up=%u.%u hotspot=%s error=%d\n",
                snapshot.download_tenths_mbps / 10,
                snapshot.download_tenths_mbps % 10,
                snapshot.upload_tenths_mbps / 10,
                snapshot.upload_tenths_mbps % 10,
                snapshot.hotspot_enabled ? "on" : "off",
                (int)snapshot.last_error);
    }
    const esp_gsp_err_t err = app_mifi_destroy(&s_controller);
    if (err != ESP_GSP_OK) {
        fprintf(stderr, "mifi_backend: cleanup failed (%d)\n", (int)err);
    }
}
