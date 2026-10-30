idf_component_register(
    SRCS "mifi_service.c"
    INCLUDE_DIRS "include"
    REQUIRES app_mifi esp_wifi esp_netif esp_event nvs_flash esp_timer
             esp_driver_tsens esp-mosaico-bsp iot_usbh_rndis iot_eth iot_usbh_cdc usb lwip
)
