#pragma once

#include <stdint.h>
#include <stdbool.h>

// Raw parsed values from DSMR telegram (floating point, in original units)
typedef struct {
    bool valid;

    // Timestamp (YYMMDDhhmmssX, X=W/S)
    char timestamp[14];

    // Cumulative energy (kWh) — double for precision above ~16777 kWh
    double energy_delivered_kwh;  // 1-0:1.8.0
    double energy_returned_kwh;   // 1-0:2.8.0

    // Instantaneous power (kW)
    float power_import_kw;        // 1-0:1.7.0
    float power_export_kw;        // 1-0:2.7.0
    float power_import_l1_kw;     // 1-0:21.7.0
    float power_export_l1_kw;     // 1-0:22.7.0
    float power_import_l2_kw;     // 1-0:41.7.0
    float power_export_l2_kw;     // 1-0:42.7.0
    float power_import_l3_kw;     // 1-0:61.7.0
    float power_export_l3_kw;     // 1-0:62.7.0

    // Voltage (V)
    float voltage_l1;             // 1-0:32.7.0
    float voltage_l2;             // 1-0:52.7.0
    float voltage_l3;             // 1-0:72.7.0

    // Current (A)
    float current_l1;             // 1-0:31.7.0
    float current_l2;             // 1-0:51.7.0
    float current_l3;             // 1-0:71.7.0
} dsmr_telegram_t;

typedef void (*dsmr_callback_t)(const dsmr_telegram_t *telegram);

void dsmr_parser_init(dsmr_callback_t callback);
