cmake_minimum_required(VERSION 3.16)

# Exclude components not needed for a Zigbee-only device
set(EXCLUDE_COMPONENTS
    esp_http_client esp_http_server esp_https_ota esp_https_server
    esp_wifi wifi_provisioning wpa_supplicant
    esp_eth mqtt protocomm esp_local_ctrl
    esp_hid usb openthread
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(zigbee_p1_meter)
