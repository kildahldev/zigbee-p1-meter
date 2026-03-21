#pragma once

#include <stdint.h>

typedef enum {
    LED_STATE_OFF,
    LED_STATE_PAIRING,      // Blue breathing
    LED_STATE_CONNECTED,    // Green solid
    LED_STATE_TELEGRAM_RX,  // Green blink (auto-reverts to CONNECTED)
    LED_STATE_OTA,          // Purple pulsing
    LED_STATE_ERROR,        // Red solid
} led_state_t;

void led_init(void);
void led_set_state(led_state_t state);
