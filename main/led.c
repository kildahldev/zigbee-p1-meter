#include "led.h"
#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <math.h>

#define LED_GPIO            2
#define LED_RMT_RES_HZ      (10 * 1000 * 1000)
#define BREATHE_PERIOD_MS   2000
#define BLINK_DURATION_MS   200

static const char *TAG = "led";
static led_strip_handle_t s_strip;
static volatile led_state_t s_state = LED_STATE_OFF;

static void led_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
}

static void led_off_pixel(void)
{
    led_strip_clear(s_strip);
}

static float breathe_brightness(void)
{
    uint32_t ms = (xTaskGetTickCount() * portTICK_PERIOD_MS) % BREATHE_PERIOD_MS;
    float phase = (float)ms / BREATHE_PERIOD_MS;
    return (sinf(phase * 2.0f * M_PI - M_PI / 2.0f) + 1.0f) / 2.0f;
}

static void led_task(void *arg)
{
    while (1) {
        led_state_t state = s_state;

        switch (state) {
        case LED_STATE_OFF:
            led_off_pixel();
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case LED_STATE_PAIRING: {
            uint8_t b = (uint8_t)(breathe_brightness() * 60);
            led_set_rgb(0, 0, b);
            vTaskDelay(pdMS_TO_TICKS(20));
            break;
        }

        case LED_STATE_CONNECTED:
            led_set_rgb(0, 30, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case LED_STATE_TELEGRAM_RX:
            led_set_rgb(0, 80, 0);
            vTaskDelay(pdMS_TO_TICKS(BLINK_DURATION_MS));
            s_state = LED_STATE_CONNECTED;
            break;

        case LED_STATE_OTA: {
            uint8_t val = (uint8_t)(breathe_brightness() * 60);
            led_set_rgb(val, 0, val);
            vTaskDelay(pdMS_TO_TICKS(20));
            break;
        }

        case LED_STATE_ERROR:
            led_set_rgb(60, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            break;
        }
    }
}

void led_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = 1,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = LED_RMT_RES_HZ,
        .flags.with_dma = false,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip));
    led_strip_clear(s_strip);

    xTaskCreate(led_task, "led", 2048, NULL, 2, NULL);
    ESP_LOGI(TAG, "LED initialized on GPIO%d", LED_GPIO);
}

void led_set_state(led_state_t state)
{
    s_state = state;
}
