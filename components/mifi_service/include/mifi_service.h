#pragma once

#include "app_mifi.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the MiFi hardware service (NVS settings, Wi-Fi SoftAP + NAPT,
 *        USB Host RNDIS + CDC AT, BSP battery & display backlight/auto-sleep)
 *        and bind it to the GSP UI controller.
 */
esp_err_t mifi_service_start(app_mifi_handle_t ui_controller);

#ifdef __cplusplus
}
#endif
