#include "dsmr_parser.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "dsmr";

#define UART_PORT       UART_NUM_1
#define UART_RX_PIN     GPIO_NUM_4
#define UART_TX_PIN     GPIO_NUM_3
#define UART_BUF_SIZE   2048
#define TELEGRAM_BUF    2048

static dsmr_callback_t s_callback;
static char s_telegram[TELEGRAM_BUF];
static int s_telegram_len;

typedef enum {
    PARSE_IDLE,
    PARSE_RECEIVING,
} parse_state_t;

static parse_state_t s_state = PARSE_IDLE;

/* ---------- OBIS code parsing ---------- */

typedef struct {
    const char *obis;
    size_t offset;      // offset into dsmr_telegram_t
    bool is_double;     // true for double fields, false for float
} obis_mapping_t;

#define FIELD_OFF(field) offsetof(dsmr_telegram_t, field)

static const obis_mapping_t s_obis_map[] = {
    { "1-0:1.8.0",  FIELD_OFF(energy_delivered_kwh), true },
    { "1-0:2.8.0",  FIELD_OFF(energy_returned_kwh),  true },
    { "1-0:1.7.0",  FIELD_OFF(power_import_kw),      false },
    { "1-0:2.7.0",  FIELD_OFF(power_export_kw),      false },
    { "1-0:21.7.0", FIELD_OFF(power_import_l1_kw),   false },
    { "1-0:22.7.0", FIELD_OFF(power_export_l1_kw),   false },
    { "1-0:41.7.0", FIELD_OFF(power_import_l2_kw),   false },
    { "1-0:42.7.0", FIELD_OFF(power_export_l2_kw),   false },
    { "1-0:61.7.0", FIELD_OFF(power_import_l3_kw),   false },
    { "1-0:62.7.0", FIELD_OFF(power_export_l3_kw),   false },
    { "1-0:32.7.0", FIELD_OFF(voltage_l1),           false },
    { "1-0:52.7.0", FIELD_OFF(voltage_l2),           false },
    { "1-0:72.7.0", FIELD_OFF(voltage_l3),           false },
    { "1-0:31.7.0", FIELD_OFF(current_l1),           false },
    { "1-0:51.7.0", FIELD_OFF(current_l2),           false },
    { "1-0:71.7.0", FIELD_OFF(current_l3),           false },
};
#define OBIS_MAP_COUNT (sizeof(s_obis_map) / sizeof(s_obis_map[0]))

static void parse_obis_line(const char *line, dsmr_telegram_t *telegram)
{
    // Match timestamp: 0-0:1.0.0(YYMMDDhhmmssX)
    if (strncmp(line, "0-0:1.0.0(", 10) == 0) {
        const char *start = line + 10;
        const char *end = strchr(start, ')');
        if (end && (end - start) < (int)sizeof(telegram->timestamp)) {
            size_t len = end - start;
            memcpy(telegram->timestamp, start, len);
            telegram->timestamp[len] = '\0';
        }
        return;
    }

    // Find OBIS code (everything before the first '(')
    const char *paren = strchr(line, '(');
    if (!paren) return;

    size_t obis_len = paren - line;

    for (int i = 0; i < OBIS_MAP_COUNT; i++) {
        if (strlen(s_obis_map[i].obis) == obis_len &&
            strncmp(line, s_obis_map[i].obis, obis_len) == 0) {
            // Extract value between '(' and '*'
            const char *val_start = paren + 1;
            const char *star = strchr(val_start, '*');
            if (!star) return;

            if (s_obis_map[i].is_double) {
                double value = strtod(val_start, NULL);
                double *field = (double *)((char *)telegram + s_obis_map[i].offset);
                *field = value;
            } else {
                float value = strtof(val_start, NULL);
                float *field = (float *)((char *)telegram + s_obis_map[i].offset);
                *field = value;
            }
            return;
        }
    }
}

// CRC16/ARC (x^16 + x^15 + x^2 + 1) used by DSMR
static uint16_t crc16(const char *buf, int len)
{
    uint16_t crc = 0;
    for (int i = 0; i < len; i++) {
        crc ^= (uint8_t)buf[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

static void parse_telegram(const char *buf, int len)
{
    // Find '!' and validate CRC: everything from '/' to '!' (inclusive) is checksummed
    const char *excl = NULL;
    for (int i = len - 1; i >= 0; i--) {
        if (buf[i] == '!') { excl = &buf[i]; break; }
    }
    if (!excl) {
        ESP_LOGW(TAG, "Malformed telegram: missing '!' end marker");
        return;
    }

    int crc_data_len = (excl - buf) + 1;  // includes '!'
    uint16_t computed = crc16(buf, crc_data_len);
    unsigned int expected = 0;
    if (sscanf(excl + 1, "%4X", &expected) != 1) {
        ESP_LOGW(TAG, "Malformed telegram: missing CRC after '!'");
        return;
    }
    if (computed != (uint16_t)expected) {
        ESP_LOGW(TAG, "CRC mismatch: computed 0x%04X, expected 0x%04X", computed, expected);
        return;
    }

    dsmr_telegram_t telegram = {0};

    const char *p = buf;
    const char *end = buf + len;

    while (p < end) {
        const char *line_end = memchr(p, '\n', end - p);
        if (!line_end) line_end = end;

        // Skip \r
        const char *le = line_end;
        if (le > p && *(le - 1) == '\r') le--;

        // Create null-terminated line
        int line_len = le - p;
        if (line_len > 0 && line_len < 128) {
            char line[128];
            memcpy(line, p, line_len);
            line[line_len] = '\0';

            parse_obis_line(line, &telegram);
        }

        p = line_end + 1;
    }

    telegram.valid = true;
    ESP_LOGI(TAG, "Telegram parsed: %.3f kWh delivered, %.1f W total power",
             telegram.energy_delivered_kwh, telegram.power_import_kw * 1000.0f);

    if (s_callback) {
        s_callback(&telegram);
    }
}

/* ---------- UART reading task ---------- */

static void uart_task(void *arg)
{
    uint8_t buf[256];

    while (1) {
        int len = uart_read_bytes(UART_PORT, buf, sizeof(buf), pdMS_TO_TICKS(100));
        if (len <= 0) continue;

        for (int i = 0; i < len; i++) {
            char c = (char)buf[i];

            switch (s_state) {
            case PARSE_IDLE:
                if (c == '/') {
                    s_telegram_len = 0;
                    s_telegram[s_telegram_len++] = c;
                    s_state = PARSE_RECEIVING;
                }
                break;

            case PARSE_RECEIVING:
                if (s_telegram_len < TELEGRAM_BUF - 1) {
                    s_telegram[s_telegram_len++] = c;
                }

                // Check for end of telegram: '!' followed by 4 hex CRC + \r\n
                // We detect when we see \n after '!'
                if (c == '\n' && s_telegram_len >= 6) {
                    // Look back for !XXXX\r\n pattern
                    // Find the '!' in the last few characters
                    for (int j = s_telegram_len - 2; j >= s_telegram_len - 8 && j >= 0; j--) {
                        if (s_telegram[j] == '!') {
                            s_telegram[s_telegram_len] = '\0';
                            parse_telegram(s_telegram, s_telegram_len);
                            s_state = PARSE_IDLE;
                            break;
                        }
                    }
                }

                // Overflow protection
                if (s_telegram_len >= TELEGRAM_BUF - 1) {
                    ESP_LOGW(TAG, "Telegram buffer overflow, resetting");
                    s_state = PARSE_IDLE;
                }
                break;
            }
        }
    }
}

/* ---------- Init ---------- */

void dsmr_parser_init(dsmr_callback_t callback)
{
    s_callback = callback;

    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, UART_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_set_line_inverse(UART_PORT,
                                           UART_SIGNAL_RXD_INV | UART_SIGNAL_TXD_INV));

    xTaskCreate(uart_task, "dsmr", 4096, NULL, 4, NULL);
    ESP_LOGI(TAG, "DSMR parser initialized (RX=GPIO%d, TX=GPIO%d)", UART_RX_PIN, UART_TX_PIN);
}
