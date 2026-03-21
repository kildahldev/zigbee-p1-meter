#include "ota.h"
#include "led.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ota";

#define OTA_ELEMENT_HEADER_LEN 6  // 2-byte tag ID + 4-byte length

static esp_ota_handle_t s_ota_handle = 0;
static const esp_partition_t *s_ota_partition = NULL;
static uint32_t s_total_size = 0;
static uint32_t s_offset = 0;
static bool s_header_parsed = false;

// Sub-element parser matching official ESP Zigbee SDK example
static esp_err_t parse_ota_element(uint32_t total_size, const void *payload,
                                    uint16_t payload_size, const void **out_data,
                                    uint16_t *out_len)
{
    if (!s_header_parsed) {
        if (!payload || payload_size <= OTA_ELEMENT_HEADER_LEN) {
            ESP_LOGE(TAG, "Invalid OTA element format");
            return ESP_ERR_INVALID_ARG;
        }
        // First 2 bytes: tag ID (0x0000 = upgrade image), use memcpy for alignment safety
        uint16_t tag_id;
        uint32_t element_len;
        memcpy(&tag_id, payload, sizeof(tag_id));
        memcpy(&element_len, (const uint8_t *)payload + 2, sizeof(element_len));

        if ((element_len + OTA_ELEMENT_HEADER_LEN) != total_size) {
            ESP_LOGW(TAG, "Element length mismatch: %lu + %d != %lu",
                     (unsigned long)element_len, OTA_ELEMENT_HEADER_LEN,
                     (unsigned long)total_size);
        }

        if (tag_id != 0x0000) {
            ESP_LOGE(TAG, "Unsupported OTA element tag: 0x%04x", tag_id);
            return ESP_ERR_INVALID_ARG;
        }

        s_header_parsed = true;
        *out_data = (const uint8_t *)payload + OTA_ELEMENT_HEADER_LEN;
        *out_len = payload_size - OTA_ELEMENT_HEADER_LEN;
    } else {
        *out_data = payload;
        *out_len = payload_size;
    }
    return ESP_OK;
}

esp_err_t ota_upgrade_status_handler(const esp_zb_zcl_ota_upgrade_value_message_t *message)
{
    esp_err_t ret = ESP_OK;

    // Check message status before processing (per official example)
    if (message->info.status != ESP_ZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGW(TAG, "OTA message status: 0x%x", message->info.status);
        return ESP_OK;
    }

    switch (message->upgrade_status) {
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_START:
        ESP_LOGI(TAG, "OTA upgrade started");
        led_set_state(LED_STATE_OTA);

        s_ota_partition = esp_ota_get_next_update_partition(NULL);
        if (!s_ota_partition) {
            ESP_LOGE(TAG, "No OTA partition available");
            return ESP_FAIL;
        }

        ret = esp_ota_begin(s_ota_partition, 0, &s_ota_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(ret));
            return ret;
        }

        s_header_parsed = false;
        s_total_size = 0;
        s_offset = 0;
        ESP_LOGI(TAG, "OTA partition: %s", s_ota_partition->label);
        break;

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_RECEIVE: {
        s_total_size = message->ota_header.image_size;
        s_offset += message->payload_size;

        if (message->payload_size && message->payload) {
            const void *data = NULL;
            uint16_t data_len = 0;

            ret = parse_ota_element(s_total_size, message->payload,
                                     message->payload_size, &data, &data_len);
            if (ret != ESP_OK) {
                return ret;
            }

            ret = esp_ota_write(s_ota_handle, data, data_len);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(ret));
                return ret;
            }
        }

        // Log progress periodically
        if (s_total_size > 0 && (s_offset % (32 * 1024) < 256)) {
            ESP_LOGI(TAG, "OTA progress: %lu / %lu bytes",
                     (unsigned long)s_offset, (unsigned long)s_total_size);
        }
        break;
    }

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_APPLY:
        ESP_LOGI(TAG, "OTA apply");
        break;

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_CHECK:
        // Validate that we received the complete image
        ret = (s_offset == s_total_size) ? ESP_OK : ESP_FAIL;
        ESP_LOGI(TAG, "OTA check: %s (received %lu / %lu)",
                 esp_err_to_name(ret),
                 (unsigned long)s_offset, (unsigned long)s_total_size);
        // Reset state for next potential OTA
        s_offset = 0;
        s_total_size = 0;
        s_header_parsed = false;
        break;

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_FINISH: {
        ESP_LOGI(TAG, "OTA finished: version 0x%lx, size %lu bytes",
                 (unsigned long)message->ota_header.file_version,
                 (unsigned long)message->ota_header.image_size);

        ret = esp_ota_end(s_ota_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(ret));
            led_set_state(LED_STATE_ERROR);
            return ret;
        }

        ret = esp_ota_set_boot_partition(s_ota_partition);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(ret));
            led_set_state(LED_STATE_ERROR);
            return ret;
        }

        ESP_LOGW(TAG, "OTA complete, rebooting...");
        esp_restart();
        break;
    }

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_ABORT:
        ESP_LOGW(TAG, "OTA aborted");
        if (s_ota_handle) {
            esp_ota_abort(s_ota_handle);
            s_ota_handle = 0;
        }
        s_offset = 0;
        s_total_size = 0;
        s_header_parsed = false;
        led_set_state(LED_STATE_CONNECTED);
        break;

    default:
        ESP_LOGD(TAG, "OTA unknown status: %d", message->upgrade_status);
        break;
    }

    return ret;
}

void ota_mark_valid(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK) {
        if (state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "Marking current firmware as valid (cancelling rollback)");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }
}
