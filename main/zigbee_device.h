#pragma once

#include <stdint.h>
#include "esp_zigbee_core.h"

#define ZB_ENDPOINT         1
#define MANUFACTURER_NAME   "\x05Homey"
#define MODEL_IDENTIFIER    "\x07P1Meter"

#define FW_VERSION_MAJOR    1
#define FW_VERSION_MINOR    0
#define FW_VERSION_PATCH    8
#define FW_VERSION_STRING   "1.0.8"

#define OTA_MANUFACTURER_CODE   0x1337
#define OTA_IMAGE_TYPE          0x0001
#define OTA_FW_VERSION          ((FW_VERSION_MAJOR << 24) | (FW_VERSION_MINOR << 16) | FW_VERSION_PATCH)

// Sensor data from DSMR telegram, pre-converted to Zigbee integer formats
typedef struct {
    // Electrical Measurement cluster values
    uint16_t voltage_l1;        // 0.1V units (231.6V → 2316)
    uint16_t voltage_l2;
    uint16_t voltage_l3;
    uint16_t current_l1;        // mA (1.1A → 1100)
    uint16_t current_l2;
    uint16_t current_l3;
    int16_t  power_l1;          // W
    int16_t  power_l2;
    int16_t  power_l3;
    int32_t  power_total;       // W (import - export)

    // Metering cluster values
    uint64_t energy_delivered;  // Wh (kWh * 1000)
    uint64_t energy_received;   // Wh
} zigbee_sensor_data_t;

void zigbee_device_init(void);
void zigbee_update_attributes(const zigbee_sensor_data_t *data);
bool zigbee_is_connected(void);
