#include "led.h"
#include "dsmr_parser.h"
#include "zigbee_device.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "main";

#define P1_ENABLE_GPIO  GPIO_NUM_23
#define NO_DATA_TIMEOUT_MS  60000

static volatile TickType_t s_last_telegram_time = 0;
static bool s_error_reported = false;

static void on_telegram(const dsmr_telegram_t *telegram)
{
    if (!telegram->valid) return;

    s_last_telegram_time = xTaskGetTickCount();
    s_error_reported = false;

    // Convert DSMR values to Zigbee integer formats (round, don't truncate)
    zigbee_sensor_data_t data = {
        // Voltage in 0.1V units
        .voltage_l1 = (uint16_t)lroundf(telegram->voltage_l1 * 10.0f),
        .voltage_l2 = (uint16_t)lroundf(telegram->voltage_l2 * 10.0f),
        .voltage_l3 = (uint16_t)lroundf(telegram->voltage_l3 * 10.0f),

        // Current in mA
        .current_l1 = (uint16_t)lroundf(telegram->current_l1 * 1000.0f),
        .current_l2 = (uint16_t)lroundf(telegram->current_l2 * 1000.0f),
        .current_l3 = (uint16_t)lroundf(telegram->current_l3 * 1000.0f),

        // Power in W (import - export per phase)
        .power_l1 = (int16_t)lroundf((telegram->power_import_l1_kw - telegram->power_export_l1_kw) * 1000.0f),
        .power_l2 = (int16_t)lroundf((telegram->power_import_l2_kw - telegram->power_export_l2_kw) * 1000.0f),
        .power_l3 = (int16_t)lroundf((telegram->power_import_l3_kw - telegram->power_export_l3_kw) * 1000.0f),
        .power_total = (int32_t)lroundf((telegram->power_import_kw - telegram->power_export_kw) * 1000.0f),

        // Energy in Wh (double precision for large meter readings)
        .energy_delivered = (uint64_t)llround(telegram->energy_delivered_kwh * 1000.0),
        .energy_received  = (uint64_t)llround(telegram->energy_returned_kwh * 1000.0),
    };

    if (zigbee_is_connected()) {
        zigbee_update_attributes(&data);
        led_set_state(LED_STATE_TELEGRAM_RX);
    }
}

static void watchdog_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));

        if (!zigbee_is_connected()) continue;
        if (s_last_telegram_time == 0) continue;

        TickType_t elapsed = xTaskGetTickCount() - s_last_telegram_time;
        if (elapsed > pdMS_TO_TICKS(NO_DATA_TIMEOUT_MS) && !s_error_reported) {
            ESP_LOGW(TAG, "No P1 data for %d seconds", NO_DATA_TIMEOUT_MS / 1000);
            led_set_state(LED_STATE_ERROR);
            s_error_reported = true;
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Zigbee P1 Meter v" FW_VERSION_STRING " starting...");

    // Initialize NVS (required for Zigbee credentials)
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Enable P1 data reception
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << P1_ENABLE_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    ESP_ERROR_CHECK(gpio_set_level(P1_ENABLE_GPIO, 1));
    ESP_LOGI(TAG, "P1 enable GPIO%d set HIGH", P1_ENABLE_GPIO);

    // Init subsystems
    led_init();
    led_set_state(LED_STATE_PAIRING);

    zigbee_device_init();
    dsmr_parser_init(on_telegram);

    // Watchdog for missing P1 data
    xTaskCreate(watchdog_task, "watchdog", 2048, NULL, 2, NULL);

    ESP_LOGI(TAG, "All tasks started");
}
