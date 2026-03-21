#pragma once

#include "esp_zigbee_core.h"

esp_err_t ota_upgrade_status_handler(const esp_zb_zcl_ota_upgrade_value_message_t *message);
void ota_mark_valid(void);
